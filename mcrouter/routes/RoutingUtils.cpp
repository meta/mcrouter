/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "mcrouter/routes/RoutingUtils.h"

#include <algorithm>

#include <folly/json/dynamic.h>
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/options.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

static constexpr uint32_t kMaxTotalFanout = 32 * 1024;

uint32_t getAdditionalFanout(
    const folly::dynamic& json,
    const McrouterOptions& opts) {
  auto jAdditionalFanout = json.get_ptr("additional_fanout");
  if (!jAdditionalFanout) {
    return 0;
  }

  checkLogic(jAdditionalFanout->isInt(), "additional_fanout is not an integer");
  const int64_t additionalFanout = jAdditionalFanout->getInt();
  const uint32_t maxFanout = getMaxAdditionalFanout(opts);
  checkLogic(
      additionalFanout >= 0 && additionalFanout <= maxFanout,
      "additional_fanout={} must be in [0, {}]",
      additionalFanout,
      maxFanout);
  return static_cast<uint32_t>(additionalFanout);
}

uint32_t getMaxAdditionalFanout(const McrouterOptions& opts) {
  const uint64_t numProxies =
      opts.thread_affinity ? 1 : std::max<uint64_t>(opts.num_proxies, 1);
  return static_cast<uint32_t>(
      std::max<uint64_t>(kMaxTotalFanout / numProxies, 1) - 1);
}

std::optional<folly::StringPiece> getTwJobFromServices(
    const folly::dynamic* jservices,
    folly::StringPiece serverKey) {
  if (!jservices || !jservices->isObject()) {
    return std::nullopt;
  }
  const auto* jservice = jservices->get_ptr(serverKey);
  if (!jservice || !jservice->isObject()) {
    return std::nullopt;
  }
  const auto* jprops = jservice->get_ptr("props");
  if (!jprops || !jprops->isObject()) {
    return std::nullopt;
  }
  const auto* jTwJob = jprops->get_ptr("tw_job");
  if (jTwJob && jTwJob->isString()) {
    auto twJob = jTwJob->stringPiece();
    if (!twJob.empty()) {
      return twJob;
    }
  }
  return std::nullopt;
}

bool servicesContainTwJob(const folly::dynamic* jservices) {
  if (!jservices || !jservices->isObject()) {
    return false;
  }
  for (const auto& item : jservices->items()) {
    const auto& jservice = item.second;
    if (!jservice.isObject()) {
      continue;
    }
    const auto* jprops = jservice.get_ptr("props");
    if (!jprops || !jprops->isObject()) {
      continue;
    }
    const auto* jTwJob = jprops->get_ptr("tw_job");
    if (jTwJob && jTwJob->isString() && !jTwJob->stringPiece().empty()) {
      return true;
    }
  }
  return false;
}

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
