/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <stdint.h>
#include <cassert>

#include <folly/ScopeGuard.h>

namespace facebook {
namespace memcache {

class ListenSocket {
 public:
  /**
   * @throws std::runtime_error  if failed to create a listen socket
   */
  explicit ListenSocket(bool zeroCopyEnabled = false);
  ~ListenSocket();

  /**
   * Listen on `port` instead of on an OS-assigned ephemeral one.
   *
   * SO_REUSEADDR is set, so a port that a previous process left with
   * connections in TIME_WAIT can be rebound immediately. That does not permit
   * a second live listener, so a port another process is listening on still
   * fails.
   *
   * @throws std::runtime_error  if the port cannot be bound
   */
  static ListenSocket createOnPort(uint16_t port, bool zeroCopyEnabled = false);

  /**
   * Listen on ::1 only, so nothing off the host can connect. There is no IPv4
   * fallback: a client that connects to ::1 would miss a socket on 127.0.0.1,
   * so a host without IPv6 fails here rather than later. A `port` of 0 takes an
   * OS-assigned ephemeral one; any other port is bound with SO_REUSEADDR, as
   * createOnPort() binds it.
   *
   * @throws std::runtime_error  if the port cannot be bound
   */
  static ListenSocket createOnLoopback(uint16_t port);

  uint16_t getPort() const {
    return port_;
  }

  /*
   * Get the socket fd. Note that the socket will be closed in
   * destructor ~ListenSocket. If the caller of this function intends to close
   * this fd, then use the other function releaseSocketFd()
   */
  int getSocketFd() const {
    assert(socketFd_ != -1);
    return socketFd_;
  }

  /*
   * Get the socket fd. Note that the caller of this function is responsible
   * to close this fd
   */
  int releaseSocketFd() {
    assert(socketFd_ != -1);
    SCOPE_EXIT {
      socketFd_ = -1;
    };
    return socketFd_;
  }

  /**
   * Set close on exec flag on or off, according to `value'.
   */
  void setCloseOnExec(bool value);

  // movable, but not copyable
  ListenSocket(ListenSocket&& other) noexcept;
  ListenSocket& operator=(ListenSocket&& other) noexcept;
  ListenSocket(const ListenSocket&) = delete;
  ListenSocket& operator=(const ListenSocket&) = delete;

 private:
  ListenSocket(int socketFd, uint16_t port)
      : socketFd_(socketFd), port_(port) {}

  int socketFd_{-1};
  uint16_t port_{0};
};

/**
 * @return true  if port is open, false otherwise
 */
bool isPortOpen(uint16_t port);
} // namespace memcache
} // namespace facebook
