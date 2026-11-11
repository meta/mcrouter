/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>

#include <fmt/core.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <folly/ScopeGuard.h>
#include <folly/String.h>
#include <folly/fibers/Baton.h>
#include <folly/portability/Sockets.h>
#include <folly/portability/Unistd.h>
#include <folly/test/SocketAddressTestHelper.h>

#include "mcrouter/CarbonRouterInstance.h"
#include "mcrouter/config.h"
#include "mcrouter/lib/network/ConnectionOptions.h"
#include "mcrouter/lib/network/SocketUtil.h"
#include "mcrouter/lib/network/gen/MemcacheRouterInfo.h"
#include "mcrouter/lib/network/test/TestClientServerUtil.h"

#ifdef __linux__
#include <poll.h>

#include <linux/in6.h>
#endif

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;

TEST(IPv6FlowLabelOptionTest, LabelAbove20BitsIsRejected) {
  auto opts = defaultTestOptions();
  opts.config = R"({ "route": "NullRoute" })";
  opts.ipv6_flow_label = kMaxIPv6FlowLabel + 1;
  EXPECT_THAT(
      [&] {
        CarbonRouterInstance<MemcacheRouterInfo>::create(std::move(opts));
      },
      testing::ThrowsMessage<std::runtime_error>(
          testing::HasSubstr("ipv6_flow_label")));
}

#ifdef __linux__
namespace {

// Returns a socket listening on [::1], or -1 on failure.
int listenOnLoopback(uint16_t& port) {
  const int fd = socket(AF_INET6, SOCK_STREAM, 0);
  if (fd < 0) {
    return -1;
  }
  sockaddr_in6 address{};
  address.sin6_family = AF_INET6;
  address.sin6_addr = in6addr_loopback;
  socklen_t length = sizeof(address);
  if (bind(fd, reinterpret_cast<sockaddr*>(&address), length) != 0 ||
      listen(fd, 16) != 0 ||
      getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
    close(fd);
    return -1;
  }
  port = ntohs(address.sin6_port);
  return fd;
}

// Accepts one connection and returns the flow label of its packets, or 0.
uint32_t acceptedFlowLabel(int listener) {
  pollfd pending{listener, POLLIN, 0};
  if (poll(&pending, 1, 10000) != 1) {
    return 0;
  }
  const int fd = accept(listener, nullptr, nullptr);
  if (fd < 0) {
    return 0;
  }
  SCOPE_EXIT {
    close(fd);
  };
  in6_flowlabel_req request{};
  request.flr_action = IPV6_FL_A_GET;
  request.flr_flags = IPV6_FL_F_REMOTE;
  socklen_t length = sizeof(request);
  if (getsockopt(fd, IPPROTO_IPV6, kIPv6FlowLabelManager, &request, &length) !=
      0) {
    return 0;
  }
  return ntohl(request.flr_label);
}

} // namespace

TEST(IPv6FlowLabelOptionTest, ValidLabelsAreAccepted) {
  if (!folly::SocketAddressTestHelper::isIPv6Enabled()) {
    GTEST_SKIP() << "IPv6 is not available";
  }
  // Labels >= 0x80000 also need net.ipv6.flowlabel_state_ranges=0.
  for (uint32_t label : {1u, 0x7ffffu}) {
    auto opts = defaultTestOptions();
    opts.config = R"({ "route": "NullRoute" })";
    opts.ipv6_flow_label = label;
    EXPECT_NE(
        nullptr,
        CarbonRouterInstance<MemcacheRouterInfo>::create(std::move(opts)))
        << label;
  }
}

TEST(IPv6FlowLabelTest, RouterLabelsTcpConnections) {
  if (!folly::SocketAddressTestHelper::isIPv6Enabled()) {
    GTEST_SKIP() << "IPv6 is not available";
  }
  constexpr uint32_t kLabel = 0x12345;
  struct Case {
    const char* transport;
    bool handshakeOffload;
  };
  const auto ssl = facebook::memcache::test::validClientSsl();
  for (const auto& [transport, handshakeOffload] :
       {Case{"ascii:plain", false},
        Case{"ascii:ssl", false},
        Case{"ascii:ssl", true},
        Case{"caret:fizz", false},
        Case{"thrift:plain", false},
        Case{"thrift:ssl", false},
        Case{"thrift:fizz", false},
        Case{"thrift:tls_to_plain", false}}) {
    const auto name = fmt::format(
        "{}{}", transport, handshakeOffload ? " with handshake offload" : "");
    uint16_t port = 0;
    const int listener = listenOnLoopback(port);
    ASSERT_GE(listener, 0) << "cannot listen on [::1]: "
                           << folly::errnoStr(errno);
    SCOPE_EXIT {
      close(listener);
    };
    const McGetRequest request("key");
    folly::fibers::Baton replied;
    auto opts = defaultTestOptions();
    opts.ipv6_flow_label = kLabel;
    opts.pem_cert_path = ssl.sslCertPath;
    opts.pem_key_path = ssl.sslKeyPath;
    opts.pem_ca_path = ssl.sslCaPath;
    opts.ssl_handshake_offload = handshakeOffload;
    opts.config = fmt::format(
        R"({{"route": "PoolRoute|A",
            "pools": {{"A": {{"servers": ["[::1]:{}:{}"]}}}}}})",
        port,
        transport);
    auto router = CarbonRouterInstance<MemcacheRouterInfo>::init(
        fmt::format("IPv6FlowLabelTest-{}", name), opts);
    ASSERT_NE(nullptr, router) << name;
    SCOPE_EXIT {
      router->shutdown();
    };
    auto client = router->createClient(
        0 /* max_outstanding_requests */,
        false /* max_outstanding_requests_error */);
    client->send(request, [&replied](const McGetRequest&, McGetReply&&) {
      replied.post();
    });
    EXPECT_EQ(kLabel, acceptedFlowLabel(listener)) << name;
    EXPECT_TRUE(replied.try_wait_for(std::chrono::seconds(10))) << name;
  }
}
#endif
