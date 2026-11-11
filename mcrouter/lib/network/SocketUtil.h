/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <string>

#include <folly/Expected.h>
#include <folly/Range.h>
#include <folly/Unit.h>
#include <folly/io/SocketOptionMap.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/AsyncTransport.h>

namespace folly {
class AsyncSocketException;
class EventBase;
} // namespace folly

namespace facebook {
namespace memcache {

struct ConnectionOptions;

#ifdef __linux__
// Linux IPv6 socket options that <linux/in6.h> omits once glibc's
// <netinet/in.h> is included.
inline constexpr int kIPv6FlowLabelManager = 32;
inline constexpr int kIPv6FlowInfoSend = 33;
#endif

enum class ConnectionState {
  Up, // Connection is open and we can write into it.
  Down, // Connection is not open (or close), we need to reconnect.
  Connecting, // Currently connecting.
  Error // Currently processing error.
};

/**
 * Creates a socket based on the given options. The socket will be attached to
 * the given Eventbase.
 */
folly::Expected<
    folly::AsyncTransportWrapper::UniquePtr,
    folly::AsyncSocketException>
createSocket(
    folly::EventBase& eventBase,
    const ConnectionOptions& connectionOptions);

/**
 * Like createSocket(), but instead creates a AsyncSocket-based socket that
 * plays well with Thrift clients and channels; all AsyncSocketExceptions are
 * automatically converted into TAsyncTransportExceptions.
 */
folly::Expected<
    folly::AsyncTransportWrapper::UniquePtr,
    folly::AsyncSocketException>
createAsyncSocket(
    folly::EventBase& eventBase,
    const ConnectionOptions& connectionOptions);
/**
 * Checks that the IPv6 flow label is in [1, kMaxIPv6FlowLabel] and registers
 * it on a scratch socket, so that host configuration problems surface before
 * any connection uses the label.
 */
folly::Expected<folly::Unit, std::string> probeIPv6FlowLabel(uint32_t label);

/**
 * Get the socket address based on the given options.
 */
folly::Expected<folly::SocketAddress, folly::AsyncSocketException>
getSocketAddress(const ConnectionOptions& connectionOptions);

/**
 * Create the socket options map based on the given ConnectionOptions and
 * SocketAddress.
 */
folly::SocketOptionMap createSocketOptions(
    const folly::SocketAddress& address,
    const ConnectionOptions& connectionOptions);

/**
 * Checks whether of not QoS was successfully applied to the given socket.
 * If QoS wasn't applied successfully (or if the getsockopt syscall fails),
 * this function will log failure.
 */
void checkWhetherQoSIsApplied(
    int socketFd,
    const folly::SocketAddress& address,
    const ConnectionOptions& connectionOptions,
    folly::StringPiece transportName);

} // namespace memcache
} // namespace facebook
