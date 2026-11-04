/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "mcrouter/Server.h"

#include <glog/logging.h>

#include <folly/String.h>

#ifdef __linux__
#include <systemd/sd-daemon.h>
#endif

#include "mcrouter/lib/network/AsyncMcServer.h"

#include <thrift/lib/cpp/server/TServerEventHandler.h>
#include <thrift/lib/cpp2/server/ThriftServer.h>

namespace facebook {
namespace memcache {
namespace mcrouter {

namespace detail {

void notifySystemdReady() {
#ifdef __linux__
  const auto result = sd_notify(0, "READY=1");
  if (result < 0) {
    LOG(ERROR) << "Failed to notify systemd of readiness: "
               << folly::errnoStr(-result);
  }
#endif
}

namespace {

class SystemdReadyHandler : public apache::thrift::server::TServerEventHandler {
 public:
  void preServe(const folly::SocketAddress*) override {
    LOG(INFO) << "Thrift Server and AsyncMcServer running.";
    notifySystemdReady();
  }
};

} // namespace

void installSystemdReadyHandler(apache::thrift::ThriftServer& server) {
  server.addServerEventHandler(std::make_shared<SystemdReadyHandler>());
}

void startServerShutdown(
    std::shared_ptr<apache::thrift::ThriftServer> thriftServer,
    std::shared_ptr<AsyncMcServer> asyncMcServer,
    std::shared_ptr<std::atomic<bool>> shutdownStarted) {
  if (!shutdownStarted->exchange(true)) {
    LOG(INFO) << "Started server shutdown";
    if (asyncMcServer) {
      LOG(INFO) << "Started shutdown of AsyncMcServer";
      asyncMcServer->shutdown();
      asyncMcServer->join();
      LOG(INFO) << "Completed shutdown of AsyncMcServer";
    }
    if (thriftServer) {
      LOG(INFO) << "Calling stop on ThriftServer";
      thriftServer->stop();
      LOG(INFO) << "Called stop on ThriftServer";
    }
  }
}

} // namespace detail

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
