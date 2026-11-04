/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <barrier>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <mcrouter/lib/FiberLocal.h>

using namespace facebook::mcrouter;

struct tag1 {};
struct tag3 {};
struct tag4 {};
struct tag5 {};
struct tag6 {};
struct ThreadExitTag {};

struct aligned128 {
  int16_t x = 1;
  int8_t y = 2;
} __attribute__((__packed__)) __attribute__((__aligned__(128)));

TEST(FiberLocalTest, basic) {
  using FiberLocal1 = FiberLocal<intptr_t, tag1>;
  // complex types are supported (as long as they are no-throw constructible)
  using FiberLocal3 = FiberLocal<folly::fbstring, tag3>;
  // alignment is obeyed
  static_assert(
      alignof(aligned128) == 128, "align128 should have alignment = 128");
  static_assert(
      alignof(intptr_t) == 8,
      "FiberLocal after align3 should have alignment = 8");
  using FiberLocal4 = FiberLocal<aligned128, tag4>;
  // another intptr_t, but with a unique tag
  using FiberLocal5 = FiberLocal<intptr_t, tag5>;
  // unique_ptr works
  using FiberLocal6 = FiberLocal<std::unique_ptr<std::string>, tag6>;

  constexpr size_t kNumThreads = 8;
  std::barrier barrier(kNumThreads);
  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (size_t i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&, i] {
      barrier.arrive_and_wait();
      FiberLocal1::ref() = i + 1;
      barrier.arrive_and_wait();
      EXPECT_EQ(FiberLocal1::ref(), i + 1);
      EXPECT_EQ(
          reinterpret_cast<uintptr_t>(&FiberLocal4::ref()) %
              alignof(aligned128),
          0);
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }

  FiberLocal1::ref() = 1;
  EXPECT_EQ(1, FiberLocal1::ref());
  FiberLocal3::ref() = "blah";
  EXPECT_EQ("blah", FiberLocal3::ref());
  EXPECT_EQ(1, FiberLocal4::ref().x);
  EXPECT_EQ(2, FiberLocal4::ref().y);
  FiberLocal4::ref().x = 3;
  FiberLocal4::ref().y = 4;
  EXPECT_EQ(3, FiberLocal4::ref().x);
  EXPECT_EQ(4, FiberLocal4::ref().y);
  FiberLocal5::ref() = 5;
  EXPECT_EQ(5, FiberLocal5::ref());
  EXPECT_FALSE(FiberLocal6::ref());
  FiberLocal6::ref() = std::unique_ptr<std::string>(new std::string("hello"));
  EXPECT_EQ("hello", *FiberLocal6::ref());
}

TEST(FiberLocalTest, accessFromThreadLocalDestructor) {
  using Local = FiberLocal<std::shared_ptr<int>, ThreadExitTag>;
  auto value = std::make_shared<int>(42);
  std::thread([&] {
    struct AccessOnExit {
      const std::shared_ptr<int>& value;
      ~AccessOnExit() {
        Local::ref() = value;
      }
    };
    // Construct this first so its destructor runs after ordinary thread locals
    // created by FiberLocal::ref().
    thread_local AccessOnExit access{value};
    (void)access;
    Local::ref() = value;
  }).join();
  EXPECT_EQ(value.use_count(), 1);
}
