/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <cstdint>
#include <memory>

#include <fmt/format.h>
#include <gtest/gtest.h>

#include <folly/synchronization/Baton.h>

#include "mcrouter/CarbonRouterClient.h"
#include "mcrouter/CarbonRouterInstance.h"
#include "mcrouter/ProxyBase.h"
#include "mcrouter/lib/network/test/TestClientServerUtil.h"
// defaultTestOptions(); its own header does not build when included directly.
#include "mcrouter/options.h"
#include "mcrouter/routes/McrouterRouteHandle.h"
#include "mcrouter/stats.h"

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;
using namespace facebook::memcache::test;

namespace {

std::shared_ptr<CarbonRouterInstance<McrouterRouterInfo>> makeRouter(
    McrouterOptions opts) {
  return CarbonRouterInstance<McrouterRouterInfo>::create(std::move(opts));
}

std::shared_ptr<CarbonRouterInstance<McrouterRouterInfo>> makeNullRouter() {
  McrouterOptions opts = defaultTestOptions();
  opts.config = "{ \"route\": \"NullRoute\" }";
  return makeRouter(std::move(opts));
}

// Sends one get through a new router with SSL session caching on, and returns
// how many of its connections resumed a cached session.
uint64_t resumptionsOfNewRouter(uint16_t port, bool perRouter) {
  const auto ssl = validClientSsl();
  McrouterOptions opts = defaultTestOptions();
  opts.pem_cert_path = ssl.sslCertPath;
  opts.pem_key_path = ssl.sslKeyPath;
  opts.pem_ca_path = ssl.sslCaPath;
  opts.ssl_connection_cache = true;
  opts.ssl_session_cache_per_router = perRouter;
  opts.config = fmt::format(
      R"({{"pools": {{"A": {{"servers": ["[::1]:{}:caret:ssl"]}}}},)"
      R"( "route": "PoolRoute|A"}})",
      port);
  auto router = makeRouter(std::move(opts));
  EXPECT_NE(router, nullptr);
  if (!router) {
    return 0;
  }

  auto client = router->createClient(0 /* max_outstanding_requests */);
  folly::Baton<> replied;
  McGetRequest request("test");
  client->send(request, [&](const McGetRequest&, McGetReply&& reply) {
    EXPECT_EQ(*reply.result_ref(), carbon::Result::FOUND);
    replied.post();
  });
  replied.wait();
  return router->getProxyBase(0)->stats().getValue(
      num_ssl_resumption_successes_stat);
}

// The second of two routers connecting to one server resumes the first's
// session unless sessions are scoped per router.
uint64_t resumptionsOfSecondRouter(bool perRouter) {
  TestServer::Config config;
  config.useDefaultVersion = true;
  config.useTicketKeySeeds = true;
  auto server = TestServer::create(std::move(config));
  const auto port = server->getListenPort();

  EXPECT_EQ(resumptionsOfNewRouter(port, perRouter), 0);
  const auto resumptions = resumptionsOfNewRouter(port, perRouter);

  TestClient client("::1", port, 200, mc_caret_protocol, validClientSsl());
  client.sendGet("shutdown", carbon::Result::NOTFOUND);
  client.waitForReplies();
  server->join();
  return resumptions;
}

} // namespace

TEST(SslSessionCachePerRouterTest, RouterIdsAreNeverReused) {
  auto first = makeNullRouter();
  auto second = makeNullRouter();
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  EXPECT_NE(first->instanceId(), second->instanceId());

  const auto firstId = first->instanceId();
  first.reset();
  // A new router may be allocated where the first one was.
  auto third = makeNullRouter();
  ASSERT_NE(third, nullptr);
  EXPECT_NE(third->instanceId(), firstId);
  EXPECT_NE(third->instanceId(), second->instanceId());
}

TEST(SslSessionCachePerRouterTest, RoutersShareSessionsByDefault) {
  EXPECT_EQ(resumptionsOfSecondRouter(false), 1);
}

TEST(SslSessionCachePerRouterTest, RoutersResumeOnlyTheirOwnSessions) {
  EXPECT_EQ(resumptionsOfSecondRouter(true), 0);
}
