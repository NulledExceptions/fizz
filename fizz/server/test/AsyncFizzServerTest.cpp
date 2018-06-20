/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree.
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fizz/server/AsyncFizzServer.h>

#include <fizz/extensions/tokenbinding/Types.h>
#include <fizz/server/test/Mocks.h>
#include <folly/io/async/test/MockAsyncTransport.h>

namespace fizz {
namespace server {
namespace test {

using namespace fizz::extensions;
using namespace folly;
using namespace folly::test;
using namespace testing;

template <typename... Args>
AsyncActions actions(Args&&... act) {
  return fizz::server::detail::actions(std::forward<Args>(act)...);
}

class MockServerStateMachineInstance : public MockServerStateMachine {
 public:
  MockServerStateMachineInstance() {
    instance = this;
  }
  static MockServerStateMachineInstance* instance;
};
MockServerStateMachineInstance* MockServerStateMachineInstance::instance;

class AsyncFizzServerTest : public Test {
 public:
  void SetUp() override {
    context_ = std::make_shared<FizzServerContext>();
    socket_ = new MockAsyncTransport();
    auto transport = AsyncTransportWrapper::UniquePtr(socket_);
    server_.reset(new AsyncFizzServerT<MockServerStateMachineInstance>(
        std::move(transport),
        context_,
        std::make_shared<MockServerExtensions>()));
    machine_ = MockServerStateMachineInstance::instance;
    ON_CALL(*socket_, good()).WillByDefault(Return(true));
    ON_CALL(readCallback_, isBufferMovable_()).WillByDefault(Return(true));
  }

 protected:
  void expectTransportReadCallback() {
    EXPECT_CALL(*socket_, setReadCB(_))
        .WillRepeatedly(SaveArg<0>(&socketReadCallback_));
  }

  void expectAppClose() {
    EXPECT_CALL(*machine_, _processAppClose(_))
        .WillOnce(InvokeWithoutArgs([]() {
          WriteToSocket write;
          write.data = IOBuf::copyBuffer("closenotify");
          return detail::actions(
              [](State& newState) { newState.state() = StateEnum::Error; },
              std::move(write));
        }));
  }

  void accept() {
    expectTransportReadCallback();
    EXPECT_CALL(*socket_, getEventBase()).WillOnce(Return(&evb_));
    EXPECT_CALL(*machine_, _processAccept(_, &evb_, _, _))
        .WillOnce(InvokeWithoutArgs([]() { return actions(); }));
    server_->accept(&handshakeCallback_);
  }

  void fullHandshakeSuccess(
      std::shared_ptr<const Cert> clientCert = nullptr,
      std::shared_ptr<const Cert> serverCert = nullptr) {
    EXPECT_CALL(*machine_, _processSocketData(_, _))
        .WillOnce(InvokeWithoutArgs([clientCert,
                                     serverCert,
                                     cipher = negotiatedCipher_,
                                     protocolVersion = protocolVersion_]() {
          auto addExporterToState = [=](State& newState) {
            auto exporterMaster =
                folly::IOBuf::copyBuffer("12345678901234567890123456789012");
            newState.exporterMasterSecret() = std::move(exporterMaster);
            newState.cipher() = cipher;
            newState.version() = protocolVersion;
            newState.clientCert() = clientCert;
            newState.serverCert() = serverCert;
          };
          return actions(
              std::move(addExporterToState),
              ReportHandshakeSuccess(),
              WaitForData());
        }));
    socketReadCallback_->readBufferAvailable(IOBuf::copyBuffer("ClientHello"));
  }

  void completeHandshake() {
    accept();
    EXPECT_CALL(handshakeCallback_, _fizzHandshakeSuccess());
    fullHandshakeSuccess();
  }

  AsyncFizzServerT<MockServerStateMachineInstance>::UniquePtr server_;
  std::shared_ptr<FizzServerContext> context_;
  MockAsyncTransport* socket_;
  MockServerStateMachineInstance* machine_;
  AsyncTransportWrapper::ReadCallback* socketReadCallback_;
  MockHandshakeCallbackT<MockServerStateMachineInstance> handshakeCallback_;
  MockReadCallback readCallback_;
  MockWriteCallback writeCallback_;
  EventBase evb_;
  CipherSuite negotiatedCipher_ = CipherSuite::TLS_AES_128_GCM_SHA256;
  ProtocolVersion protocolVersion_ = ProtocolVersion::tls_1_3;
};

MATCHER_P(BufMatches, expected, "") {
  folly::IOBufEqualTo eq;
  return eq(*arg, *expected);
}

TEST_F(AsyncFizzServerTest, TestAccept) {
  accept();
}

TEST_F(AsyncFizzServerTest, TestReadSingle) {
  accept();
  EXPECT_CALL(*machine_, _processSocketData(_, _))
      .WillOnce(InvokeWithoutArgs([]() { return actions(WaitForData()); }));
  socketReadCallback_->readBufferAvailable(IOBuf::copyBuffer("ClientHello"));
}

TEST_F(AsyncFizzServerTest, TestReadMulti) {
  accept();
  EXPECT_CALL(*machine_, _processSocketData(_, _))
      .WillOnce(InvokeWithoutArgs([]() { return actions(); }))
      .WillOnce(InvokeWithoutArgs([]() { return actions(WaitForData()); }));
  socketReadCallback_->readBufferAvailable(IOBuf::copyBuffer("ClientHello"));
}

TEST_F(AsyncFizzServerTest, TestWrite) {
  accept();
  EXPECT_CALL(*machine_, _processAppWrite(_, _))
      .WillOnce(InvokeWithoutArgs([]() { return actions(); }));
  server_->writeChain(nullptr, IOBuf::copyBuffer("HTTP GET"));
}

TEST_F(AsyncFizzServerTest, TestWriteMulti) {
  accept();
  EXPECT_CALL(*machine_, _processAppWrite(_, _))
      .WillOnce(InvokeWithoutArgs([]() { return actions(); }));
  server_->writeChain(nullptr, IOBuf::copyBuffer("HTTP GET"));
  EXPECT_CALL(*machine_, _processAppWrite(_, _))
      .WillOnce(InvokeWithoutArgs([]() { return actions(); }));
  server_->writeChain(nullptr, IOBuf::copyBuffer("HTTP POST"));
}

TEST_F(AsyncFizzServerTest, TestWriteErrorState) {
  accept();
  ON_CALL(*socket_, error()).WillByDefault(Return(true));
  EXPECT_CALL(writeCallback_, writeErr_(0, _));
  server_->writeChain(&writeCallback_, IOBuf::copyBuffer("test"));
}

TEST_F(AsyncFizzServerTest, TestHandshake) {
  completeHandshake();
}

TEST_F(AsyncFizzServerTest, TestExporterAPISimple) {
  completeHandshake();
  server_->getEkm(kTokenBindingExporterLabel, nullptr, 32);
}

TEST_F(AsyncFizzServerTest, TestExporterAPIIncompleteHandshake) {
  EXPECT_THROW(
      server_->getEkm(kTokenBindingExporterLabel, nullptr, 32),
      std::runtime_error);
}

TEST_F(AsyncFizzServerTest, TestHandshakeError) {
  accept();
  EXPECT_CALL(*machine_, _processSocketData(_, _))
      .WillOnce(InvokeWithoutArgs(
          []() { return actions(ReportError("unit test"), WaitForData()); }));
  EXPECT_CALL(handshakeCallback_, _fizzHandshakeError(_));
  socketReadCallback_->readBufferAvailable(IOBuf::copyBuffer("ClientHello"));
}

TEST_F(AsyncFizzServerTest, TestDeliverAppData) {
  completeHandshake();
  server_->setReadCB(&readCallback_);
  EXPECT_CALL(*machine_, _processSocketData(_, _))
      .WillOnce(InvokeWithoutArgs([]() {
        return actions(DeliverAppData{IOBuf::copyBuffer("HI")}, WaitForData());
      }));
  EXPECT_CALL(readCallback_, readBufferAvailable_(_));
  socketReadCallback_->readBufferAvailable(IOBuf::copyBuffer("ClientHello"));
}

TEST_F(AsyncFizzServerTest, TestWriteToSocket) {
  completeHandshake();
  server_->setReadCB(&readCallback_);
  EXPECT_CALL(*machine_, _processSocketData(_, _))
      .WillOnce(InvokeWithoutArgs([]() {
        WriteToSocket write;
        write.data = IOBuf::copyBuffer("XYZ");
        return actions(std::move(write), WaitForData());
      }));
  EXPECT_CALL(*socket_, writeChain(_, _, _));
  socketReadCallback_->readBufferAvailable(IOBuf::copyBuffer("ClientHello"));
}

TEST_F(AsyncFizzServerTest, TestMutateState) {
  completeHandshake();
  server_->setReadCB(&readCallback_);
  uint32_t numTimesRun = 0;
  EXPECT_CALL(*machine_, _processSocketData(_, _))
      .WillOnce(InvokeWithoutArgs([&numTimesRun]() {
        return actions(
            [&numTimesRun](State& newState) {
              numTimesRun++;
              newState.state() = StateEnum::Error;
            },
            WaitForData());
      }));
  socketReadCallback_->readBufferAvailable(IOBuf::copyBuffer("ClientHello"));
  EXPECT_EQ(server_->getState().state(), StateEnum::Error);
  EXPECT_EQ(numTimesRun, 1);
}

TEST_F(AsyncFizzServerTest, TestAttemptVersionFallback) {
  accept();
  EXPECT_CALL(*machine_, _processSocketData(_, _))
      .WillOnce(InvokeWithoutArgs([]() {
        return actions(
            [](State& newState) { newState.state() = StateEnum::Error; },
            AttemptVersionFallback{IOBuf::copyBuffer("ClientHello")});
      }));
  EXPECT_CALL(handshakeCallback_, _fizzHandshakeAttemptFallback(_))
      .WillOnce(Invoke([&](std::unique_ptr<IOBuf>& clientHello) {
        // The mock machine does not move the read buffer so there will be a 2nd
        // ClientHello.
        EXPECT_TRUE(IOBufEqualTo()(
            clientHello, IOBuf::copyBuffer("ClientHelloClientHello")));
        server_.reset();
      }));
  socketReadCallback_->readBufferAvailable(IOBuf::copyBuffer("ClientHello"));
}

TEST_F(AsyncFizzServerTest, TestDeleteAsyncEvent) {
  accept();
  Promise<Actions> p1;
  EXPECT_CALL(*machine_, _processSocketData(_, _))
      .WillOnce(
          InvokeWithoutArgs([&p1]() { return AsyncActions(p1.getFuture()); }));
  socketReadCallback_->readBufferAvailable(IOBuf::copyBuffer("ClientHello"));
  server_.reset();
  Promise<Actions> p2;
  EXPECT_CALL(*machine_, _processSocketData(_, _))
      .WillOnce(
          InvokeWithoutArgs([&p2]() { return AsyncActions(p2.getFuture()); }));
  p1.setValue(detail::actions());
  p2.setValue(detail::actions(WaitForData()));
}

TEST_F(AsyncFizzServerTest, TestCloseHandshake) {
  accept();
  expectAppClose();
  EXPECT_CALL(handshakeCallback_, _fizzHandshakeError(_));
  EXPECT_CALL(*socket_, closeNow()).Times(AtLeast(1));
  server_->closeNow();
}

TEST_F(AsyncFizzServerTest, TestCloseNowInFlightAction) {
  completeHandshake();
  server_->setReadCB(&readCallback_);
  Promise<Actions> p;
  EXPECT_CALL(*machine_, _processSocketData(_, _))
      .WillOnce(
          InvokeWithoutArgs([&p]() { return AsyncActions(p.getFuture()); }));
  socketReadCallback_->readBufferAvailable(IOBuf::copyBuffer("Data"));
  server_->writeChain(&writeCallback_, IOBuf::copyBuffer("queued write"));
  EXPECT_CALL(writeCallback_, writeErr_(0, _));
  EXPECT_CALL(readCallback_, readEOF_());
  EXPECT_CALL(*socket_, closeNow()).Times(AtLeast(1));
  server_->closeNow();
  p.setValue(detail::actions(WaitForData()));
}

TEST_F(AsyncFizzServerTest, TestCloseInFlightAction) {
  completeHandshake();
  server_->setReadCB(&readCallback_);
  Promise<Actions> p;
  EXPECT_CALL(*machine_, _processSocketData(_, _))
      .WillOnce(
          InvokeWithoutArgs([&p]() { return AsyncActions(p.getFuture()); }));
  socketReadCallback_->readBufferAvailable(IOBuf::copyBuffer("Data"));
  server_->writeChain(&writeCallback_, IOBuf::copyBuffer("queued write"));
  server_->close();

  EXPECT_CALL(*machine_, _processAppWrite(_, _))
      .WillOnce(InvokeWithoutArgs([]() { return actions(); }));
  expectAppClose();
  p.setValue(detail::actions(WaitForData()));
}

TEST_F(AsyncFizzServerTest, TestIsDetachable) {
  completeHandshake();
  AsyncTransportWrapper::ReadCallback* readCb = socketReadCallback_;
  ON_CALL(*socket_, isDetachable()).WillByDefault(Return(false));
  EXPECT_FALSE(server_->isDetachable());
  ON_CALL(*socket_, isDetachable()).WillByDefault(Return(true));
  EXPECT_TRUE(server_->isDetachable());
  Promise<Actions> p;

  EXPECT_CALL(*machine_, _processSocketData(_, _))
      .WillOnce(
          InvokeWithoutArgs([&p]() { return AsyncActions(p.getFuture()); }));
  socket_->setReadCB(readCb);
  socketReadCallback_->readBufferAvailable(IOBuf::copyBuffer("Data"));
  EXPECT_FALSE(server_->isDetachable());
  p.setValue(detail::actions(WaitForData()));
  EXPECT_TRUE(server_->isDetachable());
}

TEST_F(AsyncFizzServerTest, TestConnecting) {
  ON_CALL(*socket_, connecting()).WillByDefault(Return(true));
  EXPECT_TRUE(server_->connecting());
  ON_CALL(*socket_, connecting()).WillByDefault(Return(false));
  accept();
  EXPECT_TRUE(server_->connecting());
  EXPECT_CALL(*machine_, _processSocketData(_, _))
      .WillOnce(InvokeWithoutArgs(
          []() { return actions(ReportHandshakeSuccess(), WaitForData()); }));
  EXPECT_CALL(handshakeCallback_, _fizzHandshakeSuccess());
  socketReadCallback_->readBufferAvailable(IOBuf::copyBuffer("ClientHello"));
  EXPECT_FALSE(server_->connecting());
}

TEST_F(AsyncFizzServerTest, TestGoodSocket) {
  accept();
  ON_CALL(*socket_, good()).WillByDefault(Return(true));
  EXPECT_TRUE(server_->good());
  ON_CALL(*socket_, good()).WillByDefault(Return(false));
  EXPECT_FALSE(server_->good());
}

TEST_F(AsyncFizzServerTest, TestGoodState) {
  completeHandshake();
  ON_CALL(*socket_, good()).WillByDefault(Return(true));
  EXPECT_TRUE(server_->good());
  EXPECT_CALL(*machine_, _processSocketData(_, _))
      .WillOnce(InvokeWithoutArgs([]() {
        return actions(
            [](State& newState) { newState.state() = StateEnum::Error; });
      }));
  socketReadCallback_->readBufferAvailable(IOBuf::copyBuffer("Data"));
  EXPECT_FALSE(server_->good());
}

TEST_F(AsyncFizzServerTest, TestEarlySuccess) {
  accept();
  EXPECT_CALL(*machine_, _processSocketData(_, _))
      .WillOnce(InvokeWithoutArgs([]() {
        return actions(ReportEarlyHandshakeSuccess(), WaitForData());
      }));
  EXPECT_CALL(handshakeCallback_, _fizzHandshakeSuccess());
  socketReadCallback_->readBufferAvailable(IOBuf::copyBuffer("ClientHello"));

  fullHandshakeSuccess();
}

TEST_F(AsyncFizzServerTest, TestErrorStopsActions) {
  completeHandshake();
  server_->setReadCB(&readCallback_);
  EXPECT_CALL(*machine_, _processSocketData(_, _))
      .WillOnce(InvokeWithoutArgs(
          []() { return actions(ReportError("unit test")); }));
  EXPECT_FALSE(server_->error());
  socketReadCallback_->readBufferAvailable(IOBuf::copyBuffer("Data"));
  EXPECT_TRUE(server_->error());
}

TEST_F(AsyncFizzServerTest, TestGetCertsNone) {
  completeHandshake();
  EXPECT_EQ(server_->getSelfCert(), nullptr);
  EXPECT_EQ(server_->getPeerCert(), nullptr);
}

TEST_F(AsyncFizzServerTest, TestGetCerts) {
  auto clientCert = std::make_shared<MockCert>();
  auto serverCert = std::make_shared<MockCert>();
  accept();
  EXPECT_CALL(handshakeCallback_, _fizzHandshakeSuccess());
  fullHandshakeSuccess(clientCert, serverCert);
  EXPECT_CALL(*serverCert, getX509());
  EXPECT_EQ(server_->getSelfCert(), nullptr);
  EXPECT_CALL(*clientCert, getX509());
  EXPECT_EQ(server_->getPeerCert(), nullptr);
}
} // namespace test
} // namespace server
} // namespace fizz