/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fizz/protocol/AsyncFizzBase.h>
#include <fizz/protocol/Exporter.h>
#include <fizz/server/FizzServer.h>
#include <fizz/server/FizzServerContext.h>
#include <fizz/server/ServerProtocol.h>

namespace fizz {
namespace server {

template <typename SM>
class AsyncFizzServerT : public AsyncFizzBase {
 public:
  class HandshakeCallback {
   public:
    virtual ~HandshakeCallback() = default;

    virtual void fizzHandshakeSuccess(AsyncFizzServerT* transport) noexcept = 0;

    virtual void fizzHandshakeError(
        AsyncFizzServerT* transport,
        folly::exception_wrapper ex) noexcept = 0;

    virtual void fizzHandshakeAttemptFallback(
        std::unique_ptr<folly::IOBuf> clientHello) = 0;
  };

  using UniquePtr =
      std::unique_ptr<AsyncFizzServerT, folly::DelayedDestruction::Destructor>;

  AsyncFizzServerT(
      folly::AsyncTransportWrapper::UniquePtr socket,
      const std::shared_ptr<FizzServerContext>& fizzContext,
      const std::shared_ptr<ServerExtensions>& extensions = nullptr);

  virtual void accept(HandshakeCallback* callback);

  bool good() const override;
  bool readable() const override;
  bool connecting() const override;
  bool error() const override;
  bool isDetachable() const override;
  void attachEventBase(folly::EventBase* evb) override;

  folly::ssl::X509UniquePtr getPeerCert() const override;
  const X509* getSelfCert() const override;
  bool isReplaySafe() const override;
  void setReplaySafetyCallback(
      folly::AsyncTransport::ReplaySafetyCallback* callback) override;
  std::string getApplicationProtocol() noexcept override;

  void close() override;
  void closeWithReset() override;
  void closeNow() override;

  /**
   * Internal state access for logging/testing.
   */
  const State& getState() const {
    return state_;
  }

  virtual Buf getEkm(
      folly::StringPiece label,
      const Buf& hashedContext,
      uint16_t length) const;

  virtual Buf getEarlyEkm(
      folly::StringPiece label,
      const Buf& hashedContext,
      uint16_t length) const;

  const Cert* getPeerCertificate() const override;
  const Cert* getSelfCertificate() const override;

 protected:
  void writeAppData(
      folly::AsyncTransportWrapper::WriteCallback* callback,
      std::unique_ptr<folly::IOBuf>&& buf,
      folly::WriteFlags flags = folly::WriteFlags::NONE) override;

  void transportError(const folly::AsyncSocketException& ex) override;

  void transportDataAvailable() override;

 private:
  void deliverAllErrors(
      const folly::AsyncSocketException& ex,
      bool closeTransport = true);
  void deliverHandshakeError(folly::exception_wrapper ex);

  class ActionMoveVisitor : public boost::static_visitor<> {
   public:
    explicit ActionMoveVisitor(AsyncFizzServerT<SM>& server)
        : server_(server) {}

    void operator()(DeliverAppData&);
    void operator()(WriteToSocket&);
    void operator()(ReportEarlyHandshakeSuccess&);
    void operator()(ReportHandshakeSuccess&);
    void operator()(ReportError&);
    void operator()(WaitForData&);
    void operator()(MutateState&);
    void operator()(AttemptVersionFallback&);

   private:
    AsyncFizzServerT<SM>& server_;
  };

  HandshakeCallback* handshakeCallback_{nullptr};

  std::shared_ptr<FizzServerContext> fizzContext_;

  std::shared_ptr<ServerExtensions> extensions_;

  State state_;

  ActionMoveVisitor visitor_;

  FizzServer<ActionMoveVisitor, SM> fizzServer_;
};

using AsyncFizzServer = AsyncFizzServerT<ServerStateMachine>;
} // namespace server
} // namespace fizz

#include <fizz/server/AsyncFizzServer-inl.h>