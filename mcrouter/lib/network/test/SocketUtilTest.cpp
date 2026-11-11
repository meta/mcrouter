/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "mcrouter/lib/network/SocketUtil.h"

#include <cerrno>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/test/ConnCallback.h>
#include <folly/net/test/MockNetOpsDispatcher.h>
#include <folly/test/SocketAddressTestHelper.h>

#include "mcrouter/lib/network/ConnectionOptions.h"

#ifdef __linux__
#include <linux/in6.h>
#endif

namespace facebook::memcache {
namespace {

using namespace testing;

#ifdef __linux__
TEST(SocketUtilTest, ExplicitLabelCreatesManagerOptions) {
  ConnectionOptions options("2001:db8::1", 12345, mc_thrift_protocol);
  options.ipv6FlowLabel = 12345;
  const auto address = getSocketAddress(options);
  ASSERT_TRUE(address.hasValue());
  const auto socketOptions = createSocketOptions(*address, options);

  in6_flowlabel_req expected{};
  ASSERT_EQ(1, inet_pton(AF_INET6, "::1", &expected.flr_dst));
  expected.flr_label = htonl(12345);
  expected.flr_action = IPV6_FL_A_GET;
  expected.flr_flags = IPV6_FL_F_CREATE;
  expected.flr_share = IPV6_FL_S_ANY;
  EXPECT_EQ(2, socketOptions.size());
  EXPECT_EQ(
      std::string(reinterpret_cast<const char*>(&expected), sizeof(expected)),
      socketOptions.at({IPPROTO_IPV6, kIPv6FlowLabelManager}).asString());
  EXPECT_EQ(1, socketOptions.at({IPPROTO_IPV6, kIPv6FlowInfoSend}).asInt());
}

TEST(SocketUtilTest, RegistrationFailurePreventsConnect) {
  if (!folly::SocketAddressTestHelper::isIPv6Enabled()) {
    GTEST_SKIP() << "IPv6 is not available";
  }
  folly::EventBase evb;
  ConnectionOptions options("::1", 12345, mc_thrift_protocol);
  options.ipv6FlowLabel = 12345;
  const auto address = getSocketAddress(options);
  ASSERT_TRUE(address.hasValue());
  auto socketOptions = createSocketOptions(*address, options);
  // A missing request fails without allocating a shared kernel label.
  socketOptions[{IPPROTO_IPV6, kIPv6FlowLabelManager}] = std::string{};
  auto transport = createAsyncSocket(evb, options);
  ASSERT_TRUE(transport.hasValue());
  auto* socket = (*transport)->getUnderlyingTransport<folly::AsyncSocket>();
  ASSERT_NE(nullptr, socket);
  auto dispatcher =
      std::make_shared<NiceMock<folly::netops::test::MockDispatcher>>();
  dispatcher->forwardToDefaultImpl();
  socket->setOverrideNetOpsDispatcher(dispatcher);
  EXPECT_CALL(*dispatcher, connect(_, _, _)).Times(0);
  folly::test::ConnCallback callback;
  socket->connect(&callback, *address, 0, socketOptions);
  EXPECT_EQ(folly::test::STATE_FAILED, callback.state);
  EXPECT_EQ(EINVAL, callback.exception.getErrno());
  EXPECT_EQ(folly::NetworkSocket(), socket->getNetworkSocket());
}

TEST(SocketUtilTest, ProbeRegistersValidLabels) {
  if (!folly::SocketAddressTestHelper::isIPv6Enabled()) {
    GTEST_SKIP() << "IPv6 is not available";
  }
  // Labels >= 0x80000 also need net.ipv6.flowlabel_state_ranges=0.
  for (auto label : {1u, 0x7ffffu}) {
    EXPECT_TRUE(probeIPv6FlowLabel(label).hasValue()) << label;
  }
}

TEST(SocketUtilTest, ProbeRejectsLabelsOutOfRange) {
  for (auto label : {0u, kMaxIPv6FlowLabel + 1}) {
    const auto probe = probeIPv6FlowLabel(label);
    ASSERT_TRUE(probe.hasError()) << label;
    EXPECT_THAT(probe.error(), HasSubstr("[1, 1048575]"));
  }
}

TEST(SocketUtilTest, LabelAbove20BitsIsRejected) {
  folly::EventBase evb;
  ConnectionOptions options("::1", 12345, mc_thrift_protocol);
  options.ipv6FlowLabel = kMaxIPv6FlowLabel + 1;
  EXPECT_TRUE(createSocket(evb, options).hasError());
  EXPECT_TRUE(createAsyncSocket(evb, options).hasError());
}

TEST(SocketUtilTest, NonNativeIPv6ConnectionsFailBeforeConnecting) {
  for (const auto* endpoint :
       {"127.0.0.1:12345", "[::ffff:127.0.0.1]:12345", "unix:/tmp/socket"}) {
    folly::EventBase evb;
    auto ap = AccessPoint::create(endpoint, mc_thrift_protocol);
    ASSERT_NE(nullptr, ap);
    ConnectionOptions options(ap);
    options.ipv6FlowLabel = 1;
    const auto address = getSocketAddress(options);
    ASSERT_TRUE(address.hasValue());
    const auto socketOptions = createSocketOptions(*address, options);
    EXPECT_EQ(0, socketOptions.count({IPPROTO_IPV6, kIPv6FlowLabelManager}));
    EXPECT_EQ(0, socketOptions.count({IPPROTO_IPV6, kIPv6FlowInfoSend}));
    auto transport = createSocket(evb, options);
    ASSERT_TRUE(transport.hasValue());
    auto* socket = (*transport)->getUnderlyingTransport<folly::AsyncSocket>();
    ASSERT_NE(nullptr, socket);
    folly::test::ConnCallback callback;
    socket->connect(&callback, *address, 0, socketOptions);
    EXPECT_EQ(folly::test::STATE_FAILED, callback.state) << endpoint;
    EXPECT_EQ(
        folly::AsyncSocketException::BAD_ARGS, callback.exception.getType())
        << endpoint;
  }
}
#else
TEST(SocketUtilTest, ExplicitLabelRequiresLinux) {
  folly::EventBase evb;
  ConnectionOptions options("::1", 12345, mc_thrift_protocol);
  options.ipv6FlowLabel = 1;
  EXPECT_TRUE(createSocket(evb, options).hasError());
  EXPECT_TRUE(probeIPv6FlowLabel(1).hasError());
}
#endif

TEST(SocketUtilTest, UnsetLabelKeepsExistingAddressSupport) {
  folly::EventBase evb;
  for (const auto* endpoint :
       {"127.0.0.1:12345", "[::1]:12345", "unix:/tmp/socket"}) {
    auto ap = AccessPoint::create(endpoint, mc_thrift_protocol);
    ASSERT_NE(nullptr, ap);
    ConnectionOptions options(ap);
    const auto address = getSocketAddress(options);
    ASSERT_TRUE(address.hasValue());
    EXPECT_TRUE(createSocketOptions(*address, options).empty());
    EXPECT_TRUE(createSocket(evb, options).hasValue());
  }
}

} // namespace
} // namespace facebook::memcache
