/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree.
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fizz/client/AsyncFizzClient.h>

#include <fizz/client/test/Mocks.h>
#include <fizz/protocol/test/Mocks.h>
#include <folly/io/async/test/AsyncSocketTest.h>
#include <folly/io/async/test/MockAsyncSocket.h>
#include <folly/io/async/test/MockAsyncTransport.h>

namespace fizz {
namespace client {
namespace test {

using namespace fizz::test;
using namespace folly;
using namespace folly::test;
using namespace testing;

class MockClientStateMachineInstance : public MockClientStateMachine {
 public:
  MockClientStateMachineInstance() {
    instance = this;
  }
  static MockClientStateMachineInstance* instance;
};
MockClientStateMachineInstance* MockClientStateMachineInstance::instance;

class MockConnectCallback : public AsyncSocket::ConnectCallback {
 public:
  MOCK_METHOD0(_connectSuccess, void());
  MOCK_METHOD1(_connectErr, void(const AsyncSocketException&));

  void connectSuccess() noexcept override {
    _connectSuccess();
  }

  void connectErr(const AsyncSocketException& ex) noexcept override {
    _connectErr(ex);
  }
};

class AsyncFizzClientTest : public Test {
 public:
  void SetUp() override {
    context_ = std::make_shared<FizzClientContext>();
    context_->setSendEarlyData(true);
    context_->setPskCache(std::make_shared<BasicPskCache>());
    socket_ = new MockAsyncTransport();
    auto transport = AsyncTransportWrapper::UniquePtr(socket_);
    client_.reset(new AsyncFizzClientT<MockClientStateMachineInstance>(
        std::move(transport), context_));
    machine_ = MockClientStateMachineInstance::instance;
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

  void connect() {
    expectTransportReadCallback();
    EXPECT_CALL(*machine_, _processConnect(_, _, _, _, _, _))
        .WillOnce(InvokeWithoutArgs([]() { return Actions(); }));
    const auto sni = std::string("www.example.com");
    client_->connect(&handshakeCallback_, nullptr, sni, sni);
  }

  void fullHandshakeSuccess(
      bool acceptEarlyData,
      std::string alpn = "h2",
      std::shared_ptr<const Cert> clientCert = nullptr,
      std::shared_ptr<const Cert> serverCert = nullptr,
      bool pskResumed = false) {
    EXPECT_CALL(*machine_, _processSocketData(_, _))
        .WillOnce(InvokeWithoutArgs([=]() {
          auto addToState = [=](State& newState) {
            newState.exporterMasterSecret() =
                folly::IOBuf::copyBuffer("12345678901234567890123456789012");
            newState.cipher() = CipherSuite::TLS_AES_128_GCM_SHA256;
            newState.version() = ProtocolVersion::tls_1_3;
            if (alpn.empty()) {
              newState.alpn() = none;
            } else {
              newState.alpn() = alpn;
            }
            newState.clientCert() = clientCert;
            newState.serverCert() = serverCert;

            if (acceptEarlyData || pskResumed) {
              newState.pskMode() = PskKeyExchangeMode::psk_ke;
              newState.pskType() = PskType::Resumption;
            }
          };
          ReportHandshakeSuccess reportSuccess;
          reportSuccess.earlyDataAccepted = acceptEarlyData;
          return detail::actions(
              std::move(addToState), std::move(reportSuccess), WaitForData());
        }));
    socketReadCallback_->readBufferAvailable(IOBuf::copyBuffer("ServerData"));
  }

  void completeHandshake() {
    connect();
    EXPECT_CALL(handshakeCallback_, _fizzHandshakeSuccess());
    fullHandshakeSuccess(false);
  }

  static EarlyDataParams getEarlyDataParams() {
    EarlyDataParams params;
    params.version = ProtocolVersion::tls_1_3;
    params.cipher = CipherSuite::TLS_AES_128_GCM_SHA256;
    params.alpn = "h2";
    return params;
  }

  void completeEarlyHandshake(EarlyDataParams params = getEarlyDataParams()) {
    connect();
    EXPECT_CALL(*machine_, _processSocketData(_, _))
        .WillOnce(InvokeWithoutArgs([&params]() {
          auto addParamsToState =
              [params = std::move(params)](State& newState) mutable {
                newState.earlyDataParams() = std::move(params);
              };
          ReportEarlyHandshakeSuccess reportSuccess;
          reportSuccess.maxEarlyDataSize = 1000;
          return detail::actions(
              std::move(addParamsToState),
              std::move(reportSuccess),
              WaitForData());
        }));
    EXPECT_CALL(handshakeCallback_, _fizzHandshakeSuccess());
    socketReadCallback_->readBufferAvailable(IOBuf::copyBuffer("ServerData"));
    EXPECT_FALSE(client_->isReplaySafe());
  }

  AsyncFizzClientT<MockClientStateMachineInstance>::UniquePtr client_;
  std::shared_ptr<FizzClientContext> context_;
  MockAsyncTransport* socket_;
  MockClientStateMachineInstance* machine_;
  AsyncTransportWrapper::ReadCallback* socketReadCallback_;
  MockHandshakeCallbackT<MockClientStateMachineInstance> handshakeCallback_;
  MockReadCallback readCallback_;
  MockWriteCallback writeCallback_;
  EventBase evb_;
  MockReplaySafetyCallback mockReplayCallback_;
};

MATCHER_P(BufMatches, expected, "") {
  folly::IOBufEqualTo eq;
  return eq(*arg, *expected);
}

TEST_F(AsyncFizzClientTest, TestConnect) {
  connect();
}

TEST_F(AsyncFizzClientTest, TestReadSingle) {
  connect();
  EXPECT_CALL(*machine_, _processSocketData(_, _))
      .WillOnce(
          InvokeWithoutArgs([]() { return detail::actions(WaitForData()); }));
  socketReadCallback_->readBufferAvailable(IOBuf::copyBuffer("ClientHello"));
}

TEST_F(AsyncFizzClientTest, TestReadMulti) {
  connect();
  EXPECT_CALL(*machine_, _processSocketData(_, _))
      .WillOnce(InvokeWithoutArgs([]() { return detail::actions(); }))
      .WillOnce(
          InvokeWithoutArgs([]() { return detail::actions(WaitForData()); }));
  socketReadCallback_->readBufferAvailable(IOBuf::copyBuffer("ClientHello"));
}

TEST_F(AsyncFizzClientTest, TestWrite) {
  connect();
  EXPECT_CALL(*machine_, _processAppWrite(_, _))
      .WillOnce(InvokeWithoutArgs([]() { return detail::actions(); }));
  client_->writeChain(nullptr, IOBuf::copyBuffer("HTTP GET"));
}

TEST_F(AsyncFizzClientTest, TestWriteMulti) {
  connect();
  EXPECT_CALL(*machine_, _processAppWrite(_, _))
      .WillOnce(InvokeWithoutArgs([]() { return detail::actions(); }));
  client_->writeChain(nullptr, IOBuf::copyBuffer("HTTP GET"));
  EXPECT_CALL(*machine_, _processAppWrite(_, _))
      .WillOnce(InvokeWithoutArgs([]() { return detail::actions(); }));
  client_->writeChain(nullptr, IOBuf::copyBuffer("HTTP POST"));
}

TEST_F(AsyncFizzClientTest, TestWriteErrorState) {
  connect();
  ON_CALL(*socket_, error()).WillByDefault(Return(true));
  EXPECT_CALL(writeCallback_, writeErr_(0, _));
  client_->writeChain(&writeCallback_, IOBuf::copyBuffer("test"));
}

TEST_F(AsyncFizzClientTest, TestHandshake) {
  completeHandshake();
  EXPECT_TRUE(client_->isReplaySafe());
}

TEST_F(AsyncFizzClientTest, TestExporterAPI) {
  EXPECT_THROW(
      client_->getEkm("EXPORTER-Some-Label", nullptr, 32), std::runtime_error);
  completeHandshake();
  client_->getEkm("EXPORTER-Some-Label", nullptr, 32);
}

TEST_F(AsyncFizzClientTest, TestHandshakeError) {
  connect();
  EXPECT_CALL(*machine_, _processSocketData(_, _))
      .WillOnce(InvokeWithoutArgs([]() {
        return detail::actions(ReportError("unit test"), WaitForData());
      }));
  EXPECT_CALL(handshakeCallback_, _fizzHandshakeError(_));
  socketReadCallback_->readBufferAvailable(IOBuf::copyBuffer("ClientHello"));
}

TEST_F(AsyncFizzClientTest, TestHandshakeErrorDelete) {
  connect();
  EXPECT_CALL(*machine_, _processSocketData(_, _))
      .WillOnce(InvokeWithoutArgs([]() {
        return detail::actions(ReportError("unit test"), WaitForData());
      }));
  EXPECT_CALL(handshakeCallback_, _fizzHandshakeError(_))
      .WillOnce(InvokeWithoutArgs([this]() { client_.reset(); }));
  socketReadCallback_->readBufferAvailable(IOBuf::copyBuffer("ClientHello"));
}

TEST_F(AsyncFizzClientTest, TestDeliverAppData) {
  completeHandshake();
  client_->setReadCB(&readCallback_);
  EXPECT_CALL(*machine_, _processSocketData(_, _))
      .WillOnce(InvokeWithoutArgs([]() {
        return detail::actions(
            DeliverAppData{IOBuf::copyBuffer("HI")}, WaitForData());
      }));
  EXPECT_CALL(readCallback_, readBufferAvailable_(_));
  socketReadCallback_->readBufferAvailable(IOBuf::copyBuffer("ClientHello"));
}

TEST_F(AsyncFizzClientTest, TestWriteToSocket) {
  completeHandshake();
  client_->setReadCB(&readCallback_);
  EXPECT_CALL(*machine_, _processSocketData(_, _))
      .WillOnce(InvokeWithoutArgs([]() {
        WriteToSocket write;
        write.data = IOBuf::copyBuffer("XYZ");
        return detail::actions(std::move(write), WaitForData());
      }));
  EXPECT_CALL(*socket_, writeChain(_, _, _));
  socketReadCallback_->readBufferAvailable(IOBuf::copyBuffer("ClientHello"));
}

TEST_F(AsyncFizzClientTest, TestMutateState) {
  completeHandshake();
  client_->setReadCB(&readCallback_);
  uint32_t numTimesRun = 0;
  EXPECT_CALL(*machine_, _processSocketData(_, _))
      .WillOnce(InvokeWithoutArgs([&numTimesRun]() {
        return detail::actions(
            [&numTimesRun](State& newState) {
              numTimesRun++;
              newState.state() = StateEnum::Error;
            },
            WaitForData());
      }));
  socketReadCallback_->readBufferAvailable(IOBuf::copyBuffer("ClientHello"));
  EXPECT_EQ(client_->getState().state(), StateEnum::Error);
  EXPECT_EQ(numTimesRun, 1);
}

TEST_F(AsyncFizzClientTest, TestCloseHandshake) {
  connect();
  expectAppClose();
  EXPECT_CALL(handshakeCallback_, _fizzHandshakeError(_));
  EXPECT_CALL(*socket_, closeNow()).Times(AtLeast(1));
  client_->closeNow();
}

TEST_F(AsyncFizzClientTest, TestConnecting) {
  ON_CALL(*socket_, connecting()).WillByDefault(Return(true));
  EXPECT_TRUE(client_->connecting());
  ON_CALL(*socket_, connecting()).WillByDefault(Return(false));
  connect();
  EXPECT_TRUE(client_->connecting());
  EXPECT_CALL(*machine_, _processSocketData(_, _))
      .WillOnce(InvokeWithoutArgs([]() {
        return detail::actions(ReportHandshakeSuccess(), WaitForData());
      }));
  EXPECT_CALL(handshakeCallback_, _fizzHandshakeSuccess());
  socketReadCallback_->readBufferAvailable(IOBuf::copyBuffer("ClientHello"));
  EXPECT_FALSE(client_->connecting());
}

TEST_F(AsyncFizzClientTest, TestGoodSocket) {
  connect();
  ON_CALL(*socket_, good()).WillByDefault(Return(true));
  EXPECT_TRUE(client_->good());
  ON_CALL(*socket_, good()).WillByDefault(Return(false));
  EXPECT_FALSE(client_->good());
}

TEST_F(AsyncFizzClientTest, TestGoodState) {
  completeHandshake();
  ON_CALL(*socket_, good()).WillByDefault(Return(true));
  EXPECT_TRUE(client_->good());
  EXPECT_CALL(*machine_, _processSocketData(_, _))
      .WillOnce(InvokeWithoutArgs([]() {
        return detail::actions(
            [](State& newState) { newState.state() = StateEnum::Error; });
      }));
  socketReadCallback_->readBufferAvailable(IOBuf::copyBuffer("Data"));
  EXPECT_FALSE(client_->good());
}

TEST_F(AsyncFizzClientTest, TestSocketConnect) {
  MockConnectCallback cb;
  EventBase evb;
  auto evbClient = AsyncFizzClientT<MockClientStateMachineInstance>::UniquePtr(
      new AsyncFizzClientT<MockClientStateMachineInstance>(&evb, context_));

  machine_ = MockClientStateMachineInstance::instance;
  auto server = std::make_unique<TestServer>();

  EXPECT_CALL(*machine_, _processConnect(_, _, _, _, _, _))
      .WillOnce(InvokeWithoutArgs([]() {
        return detail::actions(ReportHandshakeSuccess(), WaitForData());
      }));
  EXPECT_CALL(cb, _connectSuccess()).WillOnce(Invoke([&evbClient]() {
    evbClient->closeNow();
  }));

  evbClient->connect(
      server->getAddress(),
      &cb,
      nullptr,
      std::string("www.example.com"),
      std::string("pskid"));

  evb.loop();
}

TEST_F(AsyncFizzClientTest, TestSocketConnectWithUnsupportedTransport) {
  MockConnectCallback cb;
  EXPECT_CALL(cb, _connectErr(_))
      .WillOnce(Invoke([](const AsyncSocketException& ex) {
        EXPECT_THAT(ex.what(), HasSubstr("could not find underlying socket"));
      }));
  EXPECT_CALL(*socket_, getWrappedTransport()).WillOnce(Return(nullptr));
  client_->connect(
      SocketAddress(),
      &cb,
      nullptr,
      std::string("www.example.com"),
      std::string("pskid"));
}

TEST_F(AsyncFizzClientTest, TestHandshakeConnectWithUnopenedSocket) {
  client_.reset();
  EventBase evb;
  auto evbClient = AsyncFizzClientT<MockClientStateMachineInstance>::UniquePtr(
      new AsyncFizzClientT<MockClientStateMachineInstance>(&evb, context_));
  machine_ = MockClientStateMachineInstance::instance;
  EXPECT_CALL(handshakeCallback_, _fizzHandshakeError(_))
      .WillOnce(Invoke([](exception_wrapper ex) {
        EXPECT_THAT(
            ex.what().toStdString(),
            HasSubstr("handshake connect called but socket isn't open"));
      }));
  EXPECT_CALL(*machine_, _processConnect(_, _, _, _, _, _)).Times(0);
  evbClient->connect(
      &handshakeCallback_,
      nullptr,
      std::string("www.example.com"),
      std::string("pskid"));
  EXPECT_FALSE(evbClient->good());
}

TEST_F(AsyncFizzClientTest, TestSocketConnectWithOpenSocket) {
  MockConnectCallback cb;
  EXPECT_CALL(cb, _connectErr(_))
      .WillOnce(Invoke([](const AsyncSocketException& ex) {
        EXPECT_THAT(ex.what(), HasSubstr("socket already open"));
      }));
  EventBase evb;
  MockAsyncSocket mockSocket(&evb);
  EXPECT_CALL(*socket_, getWrappedTransport()).WillOnce(Return(&mockSocket));
  EXPECT_CALL(mockSocket, connect_(_, _, _, _, _))
      .WillOnce(Invoke([](AsyncSocket::ConnectCallback* cb,
                          const SocketAddress&,
                          int,
                          const AsyncSocket::OptionMap&,
                          const SocketAddress&) {
        cb->connectErr(AsyncSocketException(
            AsyncSocketException::ALREADY_OPEN, "socket already open"));
      }));
  EXPECT_CALL(*machine_, _processConnect(_, _, _, _, _, _)).Times(0);
  client_->connect(
      SocketAddress(),
      &cb,
      nullptr,
      std::string("www.example.com"),
      std::string("pskid"));
}

TEST_F(AsyncFizzClientTest, TestApplicationProtocol) {
  completeHandshake();
  EXPECT_EQ(client_->getApplicationProtocol(), "h2");
}

TEST_F(AsyncFizzClientTest, TestApplicationProtocolNone) {
  connect();
  EXPECT_CALL(handshakeCallback_, _fizzHandshakeSuccess());
  fullHandshakeSuccess(false, "");
  EXPECT_EQ(client_->getApplicationProtocol(), "");
}

TEST_F(AsyncFizzClientTest, TestPskResumed) {
  connect();
  EXPECT_CALL(handshakeCallback_, _fizzHandshakeSuccess());
  fullHandshakeSuccess(false, "h2", nullptr, nullptr, true);
  EXPECT_TRUE(client_->pskResumed());
}

TEST_F(AsyncFizzClientTest, TestNoPskResumption) {
  connect();
  EXPECT_CALL(handshakeCallback_, _fizzHandshakeSuccess());
  fullHandshakeSuccess(false, "h2", nullptr, nullptr, false);
  EXPECT_FALSE(client_->pskResumed());
}

TEST_F(AsyncFizzClientTest, TestGetCertsNone) {
  completeHandshake();
  EXPECT_EQ(client_->getSelfCert(), nullptr);
  EXPECT_EQ(client_->getPeerCert(), nullptr);
}

TEST_F(AsyncFizzClientTest, TestGetCerts) {
  auto clientCert = std::make_shared<MockCert>();
  auto serverCert = std::make_shared<MockCert>();
  connect();
  EXPECT_CALL(handshakeCallback_, _fizzHandshakeSuccess());
  fullHandshakeSuccess(false, "h2", clientCert, serverCert);
  EXPECT_CALL(*clientCert, getX509());
  EXPECT_EQ(client_->getSelfCert(), nullptr);
  EXPECT_CALL(*serverCert, getX509());
  EXPECT_EQ(client_->getPeerCert(), nullptr);
}

TEST_F(AsyncFizzClientTest, TestEarlyHandshake) {
  completeEarlyHandshake();
  fullHandshakeSuccess(true);
  EXPECT_TRUE(client_->isReplaySafe());
  EXPECT_TRUE(client_->pskResumed());
}

TEST_F(AsyncFizzClientTest, TestEarlyParams) {
  auto clientCert = std::make_shared<MockCert>();
  auto serverCert = std::make_shared<MockCert>();
  auto params = getEarlyDataParams();
  params.clientCert = clientCert;
  params.serverCert = serverCert;
  completeEarlyHandshake(std::move(params));
  EXPECT_EQ(client_->getApplicationProtocol(), "h2");
  EXPECT_CALL(*clientCert, getX509());
  EXPECT_EQ(client_->getSelfCert(), nullptr);
  EXPECT_CALL(*serverCert, getX509());
  EXPECT_EQ(client_->getPeerCert(), nullptr);
}

TEST_F(AsyncFizzClientTest, TestEarlyApplicationProtocolNone) {
  auto params = getEarlyDataParams();
  params.alpn = none;
  completeEarlyHandshake(std::move(params));
  EXPECT_EQ(client_->getApplicationProtocol(), "");
}

TEST_F(AsyncFizzClientTest, TestEarlyHandshakeWrite) {
  completeEarlyHandshake();

  EXPECT_CALL(*machine_, _processEarlyAppWrite(_, _))
      .WillOnce(InvokeWithoutArgs([]() { return detail::actions(); }));
  client_->writeChain(nullptr, IOBuf::copyBuffer("HTTP GET"));

  fullHandshakeSuccess(true);

  EXPECT_CALL(*machine_, _processAppWrite(_, _))
      .WillOnce(InvokeWithoutArgs([]() { return detail::actions(); }));
  client_->writeChain(nullptr, IOBuf::copyBuffer("HTTP POST"));
}

TEST_F(AsyncFizzClientTest, TestEarlyHandshakeReplaySafeCallback) {
  completeEarlyHandshake();
  client_->setReplaySafetyCallback(&mockReplayCallback_);

  EXPECT_CALL(*machine_, _processAppWrite(_, _))
      .WillOnce(InvokeWithoutArgs([]() { return detail::actions(); }));
  EXPECT_CALL(mockReplayCallback_, onReplaySafe_()).WillOnce(Invoke([this]() {
    client_->writeChain(nullptr, IOBuf::copyBuffer("HTTP POST"));
  }));
  fullHandshakeSuccess(true);
}

TEST_F(AsyncFizzClientTest, TestEarlyHandshakeReplaySafeCallbackRemoved) {
  completeEarlyHandshake();
  client_->setReplaySafetyCallback(&mockReplayCallback_);
  client_->setReplaySafetyCallback(nullptr);

  EXPECT_CALL(mockReplayCallback_, onReplaySafe_()).Times(0);
  fullHandshakeSuccess(true);
}

TEST_F(AsyncFizzClientTest, TestEarlyHandshakeOverLimit) {
  completeEarlyHandshake();
  client_->setReplaySafetyCallback(&mockReplayCallback_);

  auto earlyWrite = IOBuf::copyBuffer("earlywrite");
  auto longWrite = IOBuf::create(2000);
  std::memset(longWrite->writableData(), 'a', 2000);
  longWrite->append(2000);
  auto shortWrite = IOBuf::copyBuffer("shortwrite");
  auto replaySafeWrite = IOBuf::copyBuffer("replaysafe");

  EXPECT_CALL(*machine_, _processEarlyAppWrite(_, _))
      .WillOnce(Invoke([&earlyWrite](const State&, EarlyAppWrite& write) {
        EXPECT_TRUE(IOBufEqualTo()(write.data, earlyWrite));
        return detail::actions();
      }));
  client_->writeChain(nullptr, earlyWrite->clone());
  client_->writeChain(nullptr, longWrite->clone());
  client_->writeChain(nullptr, shortWrite->clone());

  Sequence s;
  EXPECT_CALL(*machine_, _processAppWrite(_, _))
      .InSequence(s)
      .WillOnce(Invoke([&longWrite](const State&, AppWrite& write) {
        EXPECT_TRUE(IOBufEqualTo()(write.data, longWrite));
        return detail::actions();
      }));
  EXPECT_CALL(*machine_, _processAppWrite(_, _))
      .InSequence(s)
      .WillOnce(Invoke([&shortWrite](const State&, AppWrite& write) {
        EXPECT_TRUE(IOBufEqualTo()(write.data, shortWrite));
        return detail::actions();
      }));
  EXPECT_CALL(*machine_, _processAppWrite(_, _))
      .InSequence(s)
      .WillOnce(Invoke([&replaySafeWrite](const State&, AppWrite& write) {
        EXPECT_TRUE(IOBufEqualTo()(write.data, replaySafeWrite));
        return detail::actions();
      }));

  EXPECT_CALL(mockReplayCallback_, onReplaySafe_())
      .WillOnce(Invoke([this, &replaySafeWrite]() {
        client_->writeChain(nullptr, replaySafeWrite->clone());
      }));
  fullHandshakeSuccess(true);
}

TEST_F(AsyncFizzClientTest, TestEarlyHandshakeAllOverLimit) {
  completeEarlyHandshake();
  client_->setReplaySafetyCallback(&mockReplayCallback_);

  auto buf = IOBuf::create(2000);
  std::memset(buf->writableData(), 'a', 2000);
  buf->append(2000);
  client_->writeChain(nullptr, buf->clone());

  EXPECT_CALL(*machine_, _processAppWrite(_, _))
      .WillOnce(Invoke([&buf](const State&, AppWrite& write) {
        EXPECT_TRUE(IOBufEqualTo()(write.data, buf));
        return detail::actions();
      }));
  EXPECT_CALL(mockReplayCallback_, onReplaySafe_());
  fullHandshakeSuccess(true);
}

TEST_F(AsyncFizzClientTest, TestEarlyHandshakeRejectedFatalError) {
  client_->setEarlyDataRejectionPolicy(
      EarlyDataRejectionPolicy::FatalConnectionError);
  completeEarlyHandshake();

  auto buf = IOBuf::create(2000);
  std::memset(buf->writableData(), 'a', 2000);
  buf->append(2000);
  client_->writeChain(nullptr, std::move(buf));
  client_->writeChain(&writeCallback_, IOBuf::copyBuffer("write"));

  EXPECT_CALL(writeCallback_, writeErr_(0, _));
  EXPECT_CALL(*socket_, closeNow()).Times(AtLeast(1));
  fullHandshakeSuccess(false);
}

TEST_F(AsyncFizzClientTest, TestEarlyHandshakeRejectedPendingWriteError) {
  client_->setEarlyDataRejectionPolicy(
      EarlyDataRejectionPolicy::FatalConnectionError);
  completeEarlyHandshake();
  client_->setReplaySafetyCallback(&mockReplayCallback_);
  client_->setReadCB(&readCallback_);
  EXPECT_CALL(readCallback_, readErr_(_))
      .WillOnce(Invoke([](const AsyncSocketException& ex) {
        EXPECT_EQ(ex.getType(), AsyncSocketException::EARLY_DATA_REJECTED);
      }));
  EXPECT_CALL(*socket_, closeNow()).Times(AtLeast(1));
  EXPECT_CALL(mockReplayCallback_, onReplaySafe_()).Times(0);
  fullHandshakeSuccess(false);
}

TEST_F(AsyncFizzClientTest, TestEarlyHandshakeRejectedAutoResendNoData) {
  client_->setEarlyDataRejectionPolicy(
      EarlyDataRejectionPolicy::AutomaticResend);
  completeEarlyHandshake();
  client_->setReplaySafetyCallback(&mockReplayCallback_);
  EXPECT_CALL(mockReplayCallback_, onReplaySafe_());
  fullHandshakeSuccess(false);
}

TEST_F(AsyncFizzClientTest, TestEarlyHandshakeRejectedAutoResend) {
  client_->setEarlyDataRejectionPolicy(
      EarlyDataRejectionPolicy::AutomaticResend);
  completeEarlyHandshake();

  EXPECT_CALL(*machine_, _processEarlyAppWrite(_, _))
      .WillOnce(Invoke([](const State&, EarlyAppWrite& write) {
        EXPECT_TRUE(IOBufEqualTo()(write.data, IOBuf::copyBuffer("aaaa")));
        return detail::actions();
      }));
  client_->writeChain(nullptr, IOBuf::copyBuffer("aaaa"));
  EXPECT_CALL(*machine_, _processEarlyAppWrite(_, _))
      .WillOnce(Invoke([](const State&, EarlyAppWrite& write) {
        EXPECT_TRUE(IOBufEqualTo()(write.data, IOBuf::copyBuffer("bbbb")));
        return detail::actions();
      }));
  client_->writeChain(nullptr, IOBuf::copyBuffer("bbbb"));

  EXPECT_CALL(*machine_, _processAppWrite(_, _))
      .WillOnce(Invoke([](const State&, AppWrite& write) {
        EXPECT_TRUE(IOBufEqualTo()(write.data, IOBuf::copyBuffer("aaaabbbb")));
        return detail::actions();
      }));
  fullHandshakeSuccess(false);
}

TEST_F(AsyncFizzClientTest, TestEarlyHandshakeRejectedAutoResendOrder) {
  client_->setEarlyDataRejectionPolicy(
      EarlyDataRejectionPolicy::AutomaticResend);
  completeEarlyHandshake();
  client_->setReplaySafetyCallback(&mockReplayCallback_);

  EXPECT_CALL(*machine_, _processEarlyAppWrite(_, _))
      .WillOnce(Invoke([](const State&, EarlyAppWrite& write) {
        EXPECT_TRUE(IOBufEqualTo()(write.data, IOBuf::copyBuffer("aaaa")));
        return detail::actions();
      }));
  client_->writeChain(nullptr, IOBuf::copyBuffer("aaaa"));
  auto buf = IOBuf::create(2000);
  std::memset(buf->writableData(), 'b', 2000);
  buf->append(2000);
  client_->writeChain(nullptr, buf->clone());

  Sequence s;
  EXPECT_CALL(*machine_, _processAppWrite(_, _))
      .InSequence(s)
      .WillOnce(Invoke([](const State&, AppWrite& write) {
        EXPECT_TRUE(IOBufEqualTo()(write.data, IOBuf::copyBuffer("aaaa")));
        return detail::actions();
      }));
  EXPECT_CALL(*machine_, _processAppWrite(_, _))
      .InSequence(s)
      .WillOnce(Invoke([&buf](const State&, AppWrite& write) {
        EXPECT_TRUE(IOBufEqualTo()(write.data, buf));
        return detail::actions();
      }));
  EXPECT_CALL(*machine_, _processAppWrite(_, _))
      .InSequence(s)
      .WillOnce(Invoke([](const State&, AppWrite& write) {
        EXPECT_TRUE(IOBufEqualTo()(write.data, IOBuf::copyBuffer("cccc")));
        return detail::actions();
      }));

  EXPECT_CALL(mockReplayCallback_, onReplaySafe_()).WillOnce(Invoke([this]() {
    client_->writeChain(nullptr, IOBuf::copyBuffer("cccc"));
  }));
  fullHandshakeSuccess(false);
}

TEST_F(AsyncFizzClientTest, TestEarlyHandshakeRejectedAutoResendDeletedBuffer) {
  client_->setEarlyDataRejectionPolicy(
      EarlyDataRejectionPolicy::AutomaticResend);
  completeEarlyHandshake();

  auto buf = IOBuf::copyBuffer("aaaa");
  EXPECT_CALL(*machine_, _processEarlyAppWrite(_, _))
      .WillOnce(Invoke([&buf](const State&, EarlyAppWrite& write) {
        EXPECT_TRUE(IOBufEqualTo()(write.data, IOBuf::copyBuffer("aaaa")));
        buf.reset();
        return detail::actions();
      }));
  client_->write(nullptr, buf->data(), buf->length());

  EXPECT_CALL(*machine_, _processAppWrite(_, _))
      .WillOnce(Invoke([](const State&, AppWrite& write) {
        EXPECT_TRUE(IOBufEqualTo()(write.data, IOBuf::copyBuffer("aaaa")));
        return detail::actions();
      }));
  fullHandshakeSuccess(false);
}

TEST_F(AsyncFizzClientTest, TestEarlyRejectResendDifferentAlpn) {
  client_->setEarlyDataRejectionPolicy(
      EarlyDataRejectionPolicy::AutomaticResend);
  completeEarlyHandshake();
  client_->setReplaySafetyCallback(&mockReplayCallback_);
  client_->setReadCB(&readCallback_);
  EXPECT_CALL(readCallback_, readErr_(_))
      .WillOnce(Invoke([](const AsyncSocketException& ex) {
        EXPECT_EQ(ex.getType(), AsyncSocketException::EARLY_DATA_REJECTED);
      }));
  EXPECT_CALL(*socket_, closeNow()).Times(AtLeast(1));
  EXPECT_CALL(mockReplayCallback_, onReplaySafe_()).Times(0);
  fullHandshakeSuccess(false, "h3");
}

TEST_F(AsyncFizzClientTest, TestEarlyRejectResendDifferentNoAlpn) {
  client_->setEarlyDataRejectionPolicy(
      EarlyDataRejectionPolicy::AutomaticResend);
  completeEarlyHandshake();
  client_->setReplaySafetyCallback(&mockReplayCallback_);
  client_->setReadCB(&readCallback_);
  EXPECT_CALL(readCallback_, readErr_(_))
      .WillOnce(Invoke([](const AsyncSocketException& ex) {
        EXPECT_EQ(ex.getType(), AsyncSocketException::EARLY_DATA_REJECTED);
      }));
  EXPECT_CALL(*socket_, closeNow()).Times(AtLeast(1));
  EXPECT_CALL(mockReplayCallback_, onReplaySafe_()).Times(0);
  fullHandshakeSuccess(false, "h3");
}

TEST_F(AsyncFizzClientTest, TestEarlyRejectResendDifferentVersion) {
  client_->setEarlyDataRejectionPolicy(
      EarlyDataRejectionPolicy::AutomaticResend);
  auto params = getEarlyDataParams();
  params.version = ProtocolVersion::tls_1_2;
  completeEarlyHandshake(std::move(params));
  client_->setReplaySafetyCallback(&mockReplayCallback_);
  client_->setReadCB(&readCallback_);
  EXPECT_CALL(readCallback_, readErr_(_))
      .WillOnce(Invoke([](const AsyncSocketException& ex) {
        EXPECT_EQ(ex.getType(), AsyncSocketException::EARLY_DATA_REJECTED);
      }));
  EXPECT_CALL(*socket_, closeNow()).Times(AtLeast(1));
  EXPECT_CALL(mockReplayCallback_, onReplaySafe_()).Times(0);
  fullHandshakeSuccess(false);
}

TEST_F(AsyncFizzClientTest, TestEarlyRejectResendDifferentCipher) {
  client_->setEarlyDataRejectionPolicy(
      EarlyDataRejectionPolicy::AutomaticResend);
  auto params = getEarlyDataParams();
  params.cipher = CipherSuite::TLS_AES_256_GCM_SHA384;
  completeEarlyHandshake(std::move(params));
  client_->setReplaySafetyCallback(&mockReplayCallback_);
  client_->setReadCB(&readCallback_);
  EXPECT_CALL(readCallback_, readErr_(_))
      .WillOnce(Invoke([](const AsyncSocketException& ex) {
        EXPECT_EQ(ex.getType(), AsyncSocketException::EARLY_DATA_REJECTED);
      }));
  EXPECT_CALL(*socket_, closeNow()).Times(AtLeast(1));
  EXPECT_CALL(mockReplayCallback_, onReplaySafe_()).Times(0);
  fullHandshakeSuccess(false);
}

TEST_F(AsyncFizzClientTest, TestEarlyRejectNoClientCert) {
  client_->setEarlyDataRejectionPolicy(
      EarlyDataRejectionPolicy::AutomaticResend);
  auto params = getEarlyDataParams();
  params.clientCert = std::make_shared<MockCert>();
  completeEarlyHandshake(std::move(params));
  client_->setReplaySafetyCallback(&mockReplayCallback_);
  client_->setReadCB(&readCallback_);
  EXPECT_CALL(readCallback_, readErr_(_))
      .WillOnce(Invoke([](const AsyncSocketException& ex) {
        EXPECT_EQ(ex.getType(), AsyncSocketException::EARLY_DATA_REJECTED);
      }));
  EXPECT_CALL(*socket_, closeNow()).Times(AtLeast(1));
  EXPECT_CALL(mockReplayCallback_, onReplaySafe_()).Times(0);
  fullHandshakeSuccess(false);
}

TEST_F(AsyncFizzClientTest, TestEarlyRejectNoServerCert) {
  client_->setEarlyDataRejectionPolicy(
      EarlyDataRejectionPolicy::AutomaticResend);
  auto params = getEarlyDataParams();
  params.clientCert = std::make_shared<MockCert>();
  completeEarlyHandshake(std::move(params));
  client_->setReplaySafetyCallback(&mockReplayCallback_);
  client_->setReadCB(&readCallback_);
  EXPECT_CALL(readCallback_, readErr_(_))
      .WillOnce(Invoke([](const AsyncSocketException& ex) {
        EXPECT_EQ(ex.getType(), AsyncSocketException::EARLY_DATA_REJECTED);
      }));
  EXPECT_CALL(*socket_, closeNow()).Times(AtLeast(1));
  EXPECT_CALL(mockReplayCallback_, onReplaySafe_()).Times(0);
  fullHandshakeSuccess(false);
}

TEST_F(AsyncFizzClientTest, TestEarlyRejectDifferentServerIdentity) {
  client_->setEarlyDataRejectionPolicy(
      EarlyDataRejectionPolicy::AutomaticResend);
  auto cert1 = std::make_shared<MockCert>();
  auto cert2 = std::make_shared<MockCert>();
  auto params = getEarlyDataParams();
  params.serverCert = cert1;
  completeEarlyHandshake(std::move(params));
  client_->setReplaySafetyCallback(&mockReplayCallback_);
  client_->setReadCB(&readCallback_);
  EXPECT_CALL(readCallback_, readErr_(_))
      .WillOnce(Invoke([](const AsyncSocketException& ex) {
        EXPECT_EQ(ex.getType(), AsyncSocketException::EARLY_DATA_REJECTED);
      }));
  EXPECT_CALL(*socket_, closeNow()).Times(AtLeast(1));
  EXPECT_CALL(mockReplayCallback_, onReplaySafe_()).Times(0);
  EXPECT_CALL(*cert1, getIdentity()).WillOnce(Return("id1"));
  EXPECT_CALL(*cert2, getIdentity()).WillOnce(Return("id2"));
  fullHandshakeSuccess(false, "h2", nullptr, cert2);
}

TEST_F(AsyncFizzClientTest, TestEarlyRejectSameServerIdentity) {
  client_->setEarlyDataRejectionPolicy(
      EarlyDataRejectionPolicy::AutomaticResend);
  auto cert1 = std::make_shared<MockCert>();
  auto cert2 = std::make_shared<MockCert>();
  auto params = getEarlyDataParams();
  params.serverCert = cert1;
  completeEarlyHandshake(std::move(params));
  client_->setReplaySafetyCallback(&mockReplayCallback_);
  EXPECT_CALL(mockReplayCallback_, onReplaySafe_());
  EXPECT_CALL(*cert1, getIdentity()).WillOnce(Return("id"));
  EXPECT_CALL(*cert2, getIdentity()).WillOnce(Return("id"));
  fullHandshakeSuccess(false, "h2", nullptr, cert2);
}

TEST_F(AsyncFizzClientTest, TestEarlyRejectDifferentClientIdentity) {
  client_->setEarlyDataRejectionPolicy(
      EarlyDataRejectionPolicy::AutomaticResend);
  auto cert1 = std::make_shared<MockCert>();
  auto cert2 = std::make_shared<MockCert>();
  auto params = getEarlyDataParams();
  params.clientCert = cert1;
  completeEarlyHandshake(std::move(params));
  client_->setReplaySafetyCallback(&mockReplayCallback_);
  client_->setReadCB(&readCallback_);
  EXPECT_CALL(readCallback_, readErr_(_))
      .WillOnce(Invoke([](const AsyncSocketException& ex) {
        EXPECT_EQ(ex.getType(), AsyncSocketException::EARLY_DATA_REJECTED);
      }));
  EXPECT_CALL(*socket_, closeNow()).Times(AtLeast(1));
  EXPECT_CALL(mockReplayCallback_, onReplaySafe_()).Times(0);
  EXPECT_CALL(*cert1, getIdentity()).WillOnce(Return("id1"));
  EXPECT_CALL(*cert2, getIdentity()).WillOnce(Return("id2"));
  fullHandshakeSuccess(false, "h2", cert2, nullptr);
}

TEST_F(AsyncFizzClientTest, TestEarlyRejectSameClientIdentity) {
  client_->setEarlyDataRejectionPolicy(
      EarlyDataRejectionPolicy::AutomaticResend);
  auto cert1 = std::make_shared<MockCert>();
  auto cert2 = std::make_shared<MockCert>();
  auto params = getEarlyDataParams();
  params.clientCert = cert1;
  completeEarlyHandshake(std::move(params));
  client_->setReplaySafetyCallback(&mockReplayCallback_);
  EXPECT_CALL(mockReplayCallback_, onReplaySafe_());
  EXPECT_CALL(*cert1, getIdentity()).WillOnce(Return("id"));
  EXPECT_CALL(*cert2, getIdentity()).WillOnce(Return("id"));
  fullHandshakeSuccess(false, "h2", cert2, nullptr);
}

TEST_F(AsyncFizzClientTest, TestEarlyRejectRemovePsk) {
  context_->putPsk("www.example.com", CachedPsk());
  EXPECT_TRUE(context_->getPsk("www.example.com").hasValue());
  completeEarlyHandshake();
  fullHandshakeSuccess(false);
  EXPECT_FALSE(context_->getPsk("www.example.com").hasValue());
}

TEST_F(AsyncFizzClientTest, TestEarlyWriteRejected) {
  completeEarlyHandshake();
  EXPECT_CALL(*machine_, _processEarlyAppWrite(_, _))
      .WillOnce(Invoke([](const State&, EarlyAppWrite& write) {
        ReportEarlyWriteFailed failed;
        failed.write = std::move(write);
        return detail::actions(std::move(failed));
      }));
  EXPECT_CALL(writeCallback_, writeSuccess_());
  client_->writeChain(&writeCallback_, IOBuf::copyBuffer("HTTP GET"));
}

TEST_F(AsyncFizzClientTest, TestEarlyWriteRejectedNullCallback) {
  completeEarlyHandshake();
  EXPECT_CALL(*machine_, _processEarlyAppWrite(_, _))
      .WillOnce(Invoke([](const State&, EarlyAppWrite& write) {
        ReportEarlyWriteFailed failed;
        failed.write = std::move(write);
        return detail::actions(std::move(failed));
      }));
  client_->writeChain(nullptr, IOBuf::copyBuffer("HTTP GET"));
}

TEST_F(AsyncFizzClientTest, TestErrorStopsActions) {
  completeHandshake();
  client_->setReadCB(&readCallback_);
  EXPECT_CALL(*machine_, _processSocketData(_, _))
      .WillOnce(InvokeWithoutArgs(
          []() { return detail::actions(ReportError("unit test")); }));
  EXPECT_FALSE(client_->error());
  socketReadCallback_->readBufferAvailable(IOBuf::copyBuffer("Data"));
  EXPECT_TRUE(client_->error());
}
} // namespace test
} // namespace client
} // namespace fizz