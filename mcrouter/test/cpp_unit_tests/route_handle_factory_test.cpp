/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <thread>

#include "mcrouter/PoolFactory.h"
#include "mcrouter/Proxy.h"
#include "mcrouter/lib/config/RouteHandleFactory.h"
#include "mcrouter/lib/network/gen/MemcacheRouterInfo.h"
#include "mcrouter/options.h"
#include "mcrouter/routes/McRouteHandleProvider.h"
#include "mcrouter/routes/test/RouteHandleTestUtil.h"

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;

TEST(RouteHandleFactoryTest, sanity) {
  TestFiberManager<MemcacheRouterInfo> fm;

  auto router = getTestRouter();
  auto proxy = router->getProxy(0);
  PoolFactory pf(
      folly::dynamic::object(),
      router->configApi(),
      folly::json::metadata_map{});
  McRouteHandleProvider<MemcacheRouterInfo> provider(*proxy, pf);
  RouteHandleFactory<MemcacheRouteHandleIf> factory(provider, proxy->getId());

  auto rh = factory.create("AllAsyncRoute|ErrorRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McGetRequest("a"));
    EXPECT_EQ(carbon::Result::NOTFOUND, *reply.result());
  });

  rh = factory.create("AllFastestRoute|ErrorRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McGetRequest("a"));
    EXPECT_TRUE(isErrorResult(*reply.result()));
  });

  rh = factory.create("AllInitialRoute|ErrorRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McGetRequest("a"));
    EXPECT_TRUE(isErrorResult(*reply.result()));
  });

  rh = factory.create("AllMajorityRoute|ErrorRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McGetRequest("a"));
    EXPECT_TRUE(isErrorResult(*reply.result()));
  });

  rh = factory.create("AllSyncRoute|ErrorRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McGetRequest("a"));
    EXPECT_TRUE(isErrorResult(*reply.result()));
  });

  rh = factory.create("FailoverRoute|NullRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McGetRequest("a"));
    EXPECT_EQ(carbon::Result::NOTFOUND, *reply.result());
  });

  rh = factory.create("HashRoute|ErrorRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McGetRequest("a"));
    EXPECT_TRUE(isErrorResult(*reply.result()));
  });

  rh = factory.create("HostIdRoute|ErrorRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McGetRequest("a"));
    EXPECT_TRUE(isErrorResult(*reply.result()));
  });

  rh = factory.create("LatestRoute|NullRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McGetRequest("a"));
    EXPECT_EQ(carbon::Result::NOTFOUND, *reply.result());
  });

  rh = factory.create("LoggingRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    mockFiberContext();
    auto reply = rh->route(McGetRequest("a"));
    EXPECT_EQ(carbon::Result::NOTFOUND, *reply.result());
  });

  rh = factory.create("MissFailoverRoute|NullRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McGetRequest("a"));
    EXPECT_EQ(carbon::Result::NOTFOUND, *reply.result());
  });

  rh = factory.create("RandomRoute|ErrorRoute");
  EXPECT_TRUE(rh != nullptr);
  fm.run([&rh]() {
    auto reply = rh->route(McGetRequest("a"));
    EXPECT_TRUE(isErrorResult(*reply.result()));
  });
}

TEST(RouteHandleFactoryTest, sharedStateUsesCheckedConstructionSlots) {
  SharedRouteStateTable table;
  SimpleRouteHandleProvider<MemcacheRouteHandleIf> provider;
  RouteHandleFactory<MemcacheRouteHandleIf> first(provider, 7, &table);
  RouteHandleFactory<MemcacheRouteHandleIf> second(provider, 0, &table);
  const folly::dynamic json = folly::dynamic::object("type", "SomeRoute");
  auto make = [] { return std::make_shared<int>(42); };
  auto firstPlacement = first.sharedRouteState<int>(json, make);
  auto secondPlacement = first.sharedRouteState<int>(json, make);
  EXPECT_NE(firstPlacement, secondPlacement);
  EXPECT_EQ(firstPlacement, second.sharedRouteState<int>(json, make));
  EXPECT_EQ(secondPlacement, second.sharedRouteState<int>(json, make));
  EXPECT_EQ(table.mismatchCount(), 0);
}

TEST(RouteHandleFactoryTest, sharedStateWithoutTableDoesNotShare) {
  SimpleRouteHandleProvider<MemcacheRouteHandleIf> provider;
  RouteHandleFactory<MemcacheRouteHandleIf> factory(provider, 0, nullptr);
  const folly::dynamic json = folly::dynamic::object("type", "SomeRoute");
  auto make = [] { return std::make_shared<int>(42); };
  auto first = factory.sharedRouteState<int>(json, make);
  EXPECT_NE(first, factory.sharedRouteState<int>(json, make));
}

TEST(RouteHandleFactoryTest, sharedStateRejectsFingerprintMismatch) {
  SharedRouteStateTable table;
  SimpleRouteHandleProvider<MemcacheRouteHandleIf> provider;
  RouteHandleFactory<MemcacheRouteHandleIf> first(provider, 0, &table);
  RouteHandleFactory<MemcacheRouteHandleIf> second(provider, 1, &table);
  const folly::dynamic json = folly::dynamic::object("type", "FirstRoute");
  const folly::dynamic other = folly::dynamic::object("type", "SecondRoute");
  auto make = [] { return std::make_shared<int>(42); };
  auto state = first.sharedRouteState<int>(json, make);
  EXPECT_DEBUG_DEATH(
      {
        EXPECT_NE(state, second.sharedRouteState<int>(other, make));
        EXPECT_EQ(table.mismatchCount(), 1);
      },
      "Shared route state mismatch at slot 0");
  RouteHandleFactory<MemcacheRouteHandleIf> third(provider, 2, &table);
  EXPECT_EQ(state, third.sharedRouteState<int>(json, make));
}

TEST(RouteHandleFactoryTest, sharedStateRejectsTypeMismatch) {
  SharedRouteStateTable table;
  SimpleRouteHandleProvider<MemcacheRouteHandleIf> provider;
  RouteHandleFactory<MemcacheRouteHandleIf> first(provider, 0, &table);
  RouteHandleFactory<MemcacheRouteHandleIf> second(provider, 1, &table);
  const folly::dynamic json = folly::dynamic::object("type", "SomeRoute");
  auto state = first.sharedRouteState<int>(
      json, [] { return std::make_shared<int>(42); });
  EXPECT_DEBUG_DEATH(
      {
        auto fresh = second.sharedRouteState<std::string>(
            json, [] { return std::make_shared<std::string>("fresh"); });
        EXPECT_EQ(*fresh, "fresh");
        EXPECT_EQ(table.mismatchCount(), 1);
      },
      "Shared route state mismatch at slot 0");
  EXPECT_EQ(*state, 42);
}

TEST(RouteHandleFactoryTest, sharedStateConcurrentFactoriesShareEachSlot) {
  SharedRouteStateTable table;
  std::atomic<size_t> creations{0};
  std::array<std::array<std::shared_ptr<int>, 12>, 8> states;
  std::vector<std::thread> threads;
  threads.reserve(states.size());
  for (size_t proxy = 0; proxy < states.size(); ++proxy) {
    threads.emplace_back([&, proxy] {
      SimpleRouteHandleProvider<MemcacheRouteHandleIf> provider;
      RouteHandleFactory<MemcacheRouteHandleIf> factory(
          provider, proxy, &table);
      for (size_t slot = 0; slot < states[proxy].size(); ++slot) {
        folly::dynamic json = folly::dynamic::object("slot", slot);
        states[proxy][slot] = factory.sharedRouteState<int>(json, [&] {
          ++creations;
          return std::make_shared<int>(slot);
        });
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  EXPECT_EQ(creations, states[0].size());
  EXPECT_EQ(table.mismatchCount(), 0);
  for (const auto& proxyStates : states) {
    for (size_t slot = 0; slot < proxyStates.size(); ++slot) {
      EXPECT_EQ(proxyStates[slot], states[0][slot]);
      EXPECT_EQ(*proxyStates[slot], slot);
    }
  }
}

TEST(RouteHandleFactoryTest, sharedStateFailedConstructionDoesNotPublishSlot) {
  SharedRouteStateTable table;
  size_t attempts = 0;
  auto make = [&]() -> std::shared_ptr<void> {
    if (++attempts == 1) {
      throw std::runtime_error("construction failed");
    }
    return std::make_shared<int>(42);
  };
  EXPECT_THROW(table.getOrCreate(0, typeid(int), 1, make), std::runtime_error);
  auto state = table.getOrCreate(0, typeid(int), 1, make);
  EXPECT_EQ(state, table.getOrCreate(0, typeid(int), 1, [] {
    return std::make_shared<int>(7);
  }));
  EXPECT_EQ(attempts, 2);
}

TEST(RouteHandleFactoryTest, sharedStateCallbackCanCreateOtherSlots) {
  SharedRouteStateTable table;
  auto state = table.getOrCreate(0, typeid(int), 0, [&] {
    for (size_t slot = 1; slot <= 64; ++slot) {
      auto nested = table.getOrCreate(slot, typeid(size_t), slot, [slot] {
        return std::make_shared<size_t>(slot);
      });
      EXPECT_EQ(*std::static_pointer_cast<size_t>(nested), slot);
    }
    return std::make_shared<int>(42);
  });
  EXPECT_EQ(*std::static_pointer_cast<int>(state), 42);
  EXPECT_EQ(state, table.getOrCreate(0, typeid(int), 0, [] {
    return std::make_shared<int>(7);
  }));
  EXPECT_EQ(table.mismatchCount(), 0);
}

TEST(RouteHandleFactoryTest, sharedStateMismatchCallbackCanCreateAnotherSlot) {
  SharedRouteStateTable table;
  auto state = table.getOrCreate(
      0, typeid(int), 1, [] { return std::make_shared<int>(42); });
  EXPECT_DEBUG_DEATH(
      {
        auto fresh = table.getOrCreate(0, typeid(std::string), 1, [&] {
          EXPECT_EQ(table.mismatchCount(), 1);
          auto nested = table.getOrCreate(
              1, typeid(int), 2, [] { return std::make_shared<int>(7); });
          EXPECT_EQ(*std::static_pointer_cast<int>(nested), 7);
          return std::make_shared<std::string>("fresh");
        });
        EXPECT_EQ(*std::static_pointer_cast<std::string>(fresh), "fresh");
      },
      "Shared route state mismatch at slot 0");
  EXPECT_EQ(state, table.getOrCreate(0, typeid(int), 1, [] {
    return std::make_shared<int>(7);
  }));
}

TEST(RouteHandleFactoryTest, sharedStateFailedConstructionDoesNotBindMetadata) {
  SharedRouteStateTable table;
  size_t attempts = 0;
  auto make = [&]() -> std::shared_ptr<void> {
    if (++attempts == 1) {
      throw std::runtime_error("construction failed");
    }
    return std::make_shared<std::string>("fresh");
  };
  EXPECT_THROW(table.getOrCreate(0, typeid(int), 1, make), std::runtime_error);
  auto state = table.getOrCreate(0, typeid(std::string), 2, make);
  EXPECT_EQ(attempts, 2);
  EXPECT_EQ(*std::static_pointer_cast<std::string>(state), "fresh");
  EXPECT_EQ(state, table.getOrCreate(0, typeid(std::string), 2, [] {
    return std::make_shared<std::string>("other");
  }));
  EXPECT_EQ(table.mismatchCount(), 0);
}

TEST(RouteHandleFactoryTest, sharedStatePreservesBasePointerAdjustment) {
  struct PrimaryBase {
    virtual ~PrimaryBase() = default;
    int primary{1};
  };
  struct SecondaryBase {
    int secondary{2};
  };
  struct Derived : PrimaryBase, SecondaryBase {};
  SharedRouteStateTable table;
  SimpleRouteHandleProvider<MemcacheRouteHandleIf> provider;
  RouteHandleFactory<MemcacheRouteHandleIf> first(provider, 0, &table);
  RouteHandleFactory<MemcacheRouteHandleIf> second(provider, 1, &table);
  const folly::dynamic json = folly::dynamic::object("type", "SomeRoute");
  auto derived = std::make_shared<Derived>();
  std::shared_ptr<SecondaryBase> expected = derived;
  ASSERT_NE(
      static_cast<void*>(expected.get()), static_cast<void*>(derived.get()));
  auto state =
      first.sharedRouteState<SecondaryBase>(json, [&] { return derived; });
  EXPECT_EQ(state.get(), expected.get());
  EXPECT_EQ(state, second.sharedRouteState<SecondaryBase>(json, [&] {
    return derived;
  }));
  EXPECT_EQ(state->secondary, 2);
}
