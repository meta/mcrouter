/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <chrono>
#include <memory>
#include <optional>

#include <folly/io/SocketOptionMap.h>
#include <folly/io/async/AsyncSocket.h>

#include "mcrouter/lib/CompressionCodecManager.h"
#include "mcrouter/lib/mc/protocol.h"
#include "mcrouter/lib/network/AccessPoint.h"
#include "mcrouter/lib/network/SecurityOptions.h"

namespace facebook {
namespace memcache {

inline constexpr uint32_t kMaxIPv6FlowLabel = 0xfffff;

/**
 * A struct for storing all connection related options.
 */
struct ConnectionOptions {
  using SocketOptions = folly::SocketOptionMap;

  ConnectionOptions(
      folly::StringPiece host,
      uint16_t port,
      mc_protocol_t protocol,
      SecurityMech mech = SecurityMech::NONE,
      const std::optional<std::string>& serviceIdOverride = std::nullopt)
      : accessPoint(
            std::make_shared<AccessPoint>(
                host,
                port,
                protocol,
                mech,
                false,
                false,
                0,
                std::nullopt,
                serviceIdOverride)) {}

  explicit ConnectionOptions(std::string_view serviceName)
      : accessPoint(std::make_shared<AccessPoint>(serviceName)) {}

  explicit ConnectionOptions(std::shared_ptr<const AccessPoint> ap)
      : accessPoint(std::move(ap)) {}

  /**
   * Access point of the destination.
   */
  std::shared_ptr<const AccessPoint> accessPoint;

  /**
   * IPv6 flow label in [1, kMaxIPv6FlowLabel], registered before each TCP
   * connect; 0 disables labeling. With a nonzero label, connections to IPv4,
   * IPv4-mapped or Unix-socket destinations fail. Service-name destinations
   * connect through ServiceRouter and are not labeled.
   */
  uint32_t ipv6FlowLabel{0};

  /**
   * Number of TCP KeepAlive probes to send before considering connection dead.
   *
   * Note: Option will be enabled iff it is supported by the OS and this
   *       value > 0.
   */
  int tcpKeepAliveCount{0};

  /**
   * Interval between last data packet sent and the first TCP KeepAlive probe.
   */
  int tcpKeepAliveIdle{0};

  /**
   * Interval between two consequent TCP KeepAlive probes.
   */
  int tcpKeepAliveInterval{0};

  /**
   * The number of times to retry establishing a connection in case of a
   * connect timeout. We will just return the result back to the client after
   * either the connection is esblished, or we exhausted all retries.
   */
  unsigned int numConnectTimeoutRetries{0};

  /**
   * Connect timeout in ms.
   */
  std::chrono::milliseconds connectTimeout{0};

  /**
   * Write timeout in ms.
   */
  std::chrono::milliseconds writeTimeout{0};

  /**
   * Informs whether QoS is enabled.
   */
  bool enableQoS{false};

  /*
   * QoS class to use in packages.
   */
  unsigned int qosClass{0};

  /*
   * QoS path to use in packages.
   */
  unsigned int qosPath{0};

  /**
   * Path of the debug fifo.
   * If empty, debug fifo is disabled.
   */
  std::string debugFifoPath;

  /**
   * Name of the router that owns this connection.
   * NOTE: Must be be a literal (constexpr), and shouldn't be used
   * outside of mcrouter.
   */
  folly::StringPiece routerInfoName;

  /**
   * Security options for this connection
   */
  SecurityOptions securityOpts;

  /**
   * Use JemallocNodumpAllocator
   */
  bool useJemallocNodumpAllocator{false};

  /**
   * Map of codecs to use for compression.
   * If nullptr, compression will be disabled.
   */
  const CompressionCodecMap* compressionCodecMap{nullptr};

  /**
   * True to enable thrift compression.
   */
  bool thriftCompression{false};

  /**
   * Payloads >= thriftCompressionTreshold will be compressed
   * iff thriftCompression is enabled.
   */
  size_t thriftCompressionThreshold{0};

  /**
   * AF_XDP/XSK client: max request datagram payload in bytes before the
   * request is rejected with LOCAL_ERROR. 0 means no cap (clamp to the jumbo
   * buffer size). Plumbed from the xdp_tx_truncate_bytes mcrouter option.
   */
  size_t xdpTxTruncateBytes{0};

  /**
   * AF_XDP/XSK client: max reply datagram payload in bytes accepted from the
   * server. A larger reply is rejected with REMOTE_ERROR rather than
   * truncated. 0 means no cap beyond the jumbo receive buffer. Plumbed from
   * the xdp_rx_truncate_bytes mcrouter option.
   */
  size_t xdpRxTruncateBytes{0};

#ifndef MCROUTER_OSS_BUILD
  /**
   * MetaRoCE client: initial retransmission timeout in milliseconds. Must
   * exceed the RTT to the destination. Plumbed from the metaroce_rto_ms
   * mcrouter option; 0 leaves the engine default.
   */
  size_t metaroceRtoMs{0};

  /**
   * MetaRoCE client: log per-connection transport stats once per this many
   * requests. Plumbed from the metaroce_stats_log_every mcrouter option; 0
   * leaves the transport default.
   */
  size_t metaroceStatsLogEvery{0};
#endif
};
} // namespace memcache
} // namespace facebook
