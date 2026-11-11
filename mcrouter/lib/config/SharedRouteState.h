/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <memory>
#include <typeindex>
#include <utility>

#include <folly/Function.h>
#include <folly/Synchronized.h>
#include <folly/container/F14Map.h>
#include <folly/synchronization/CallOnce.h>
#include <glog/logging.h>

namespace facebook::memcache {

class SharedRouteStateTable {
 public:
  std::shared_ptr<void> getOrCreate(
      size_t slot,
      std::type_index type,
      size_t fingerprint,
      folly::FunctionRef<std::shared_ptr<void>()> make) {
    Slot* entry;
    {
      auto locked = state_.wlock();
      entry = &locked->slots.try_emplace(slot).first->second;
    }
    folly::call_once(entry->flag, [&] {
      auto state = make();
      entry->type = type;
      entry->fingerprint = fingerprint;
      entry->state = std::move(state);
    });
    if (entry->type == type && entry->fingerprint == fingerprint) {
      return entry->state;
    }
    const auto mismatches = ++state_.wlock()->mismatches;
    LOG(DFATAL) << "Shared route state mismatch at slot " << slot
                << "; mismatches in this config build: " << mismatches;
    return make();
  }

  size_t mismatchCount() const {
    return state_.rlock()->mismatches;
  }

 private:
  struct Slot {
    folly::once_flag flag;
    std::type_index type{typeid(void)};
    size_t fingerprint{0};
    std::shared_ptr<void> state;
  };

  struct State {
    // Slots must stay stable while callbacks insert other slots.
    folly::F14NodeMap<size_t, Slot> slots;
    size_t mismatches{0};
  };

  folly::Synchronized<State> state_;
};

} // namespace facebook::memcache
