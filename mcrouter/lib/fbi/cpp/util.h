/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <sys/time.h>

#include <chrono>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <fmt/format.h>

#include <folly/Conv.h>
#include <folly/Expected.h>
#include <folly/Likely.h>
#include <folly/Range.h>
#include <folly/Unit.h>
#include <folly/json/json.h>

using timeval_t = struct timeval;

namespace folly {
struct dynamic;
} // namespace folly

namespace facebook {
namespace memcache {

namespace detail {

/**
 * Formats a folly::dynamic the way folly::format did: a scalar prints its
 * value, with any format spec applied to it, and null prints "(null)".
 * folly::format rejected arrays and objects unless the format string named a
 * key; they print as JSON here.
 */
struct FormattableDynamic {
  const folly::dynamic& value;
};

template <typename T>
constexpr bool isCString = std::is_same_v<std::decay_t<T>, char*> ||
    std::is_same_v<std::decay_t<T>, const char*>;

template <typename T>
using FormatArgT = std::conditional_t<
    std::is_same_v<std::decay_t<T>, folly::dynamic>,
    FormattableDynamic,
    std::conditional_t<isCString<T>, std::string_view, T>>;

// folly::dynamic and C strings are passed to fmt as what folly::format printed
// for them: the scalar a dynamic holds, and "(null)" for a null C string.
template <typename T>
decltype(auto) toFormatArg(T&& arg) {
  if constexpr (std::is_same_v<std::decay_t<T>, folly::dynamic>) {
    return FormattableDynamic{arg};
  } else if constexpr (isCString<T>) {
    return arg ? std::string_view(arg) : std::string_view("(null)");
  } else {
    return std::forward<T>(arg);
  }
}

/**
 * Formats `fmt` and `args` with fmt::format(), passing each argument through
 * toFormatArg().
 */
template <typename... Args>
std::string formatMessage(
    fmt::format_string<FormatArgT<Args>...> fmt,
    Args&&... args) {
  return fmt::format(fmt, toFormatArg(std::forward<Args>(args))...);
}

} // namespace detail

/**
 * If `condition` is false, throws std::logic_error.
 * `fmt` and `args` are passed to fmt::format().
 */
template <typename... Args>
void checkLogic(
    bool condition,
    fmt::format_string<detail::FormatArgT<Args>...> fmt,
    Args&&... args) {
  if (!condition) [[unlikely]] {
    throw std::logic_error(
        detail::formatMessage<Args...>(fmt, std::forward<Args>(args)...));
  }
}

/**
 * Throws std::logic_error with formatted string.
 * `fmt` and `args` are passed to fmt::format().
 */
template <typename... Args>
[[noreturn]] void throwLogic(
    fmt::format_string<detail::FormatArgT<Args>...> fmt,
    Args&&... args) {
  throw std::logic_error(
      detail::formatMessage<Args...>(fmt, std::forward<Args>(args)...));
}

/**
 * If `condition` is false, throws std::runtime_error.
 * `fmt` and `args` are passed to fmt::format().
 */
template <typename... Args>
void checkRuntime(
    bool condition,
    fmt::format_string<detail::FormatArgT<Args>...> fmt,
    Args&&... args) {
  if (!condition) [[unlikely]] {
    throw std::runtime_error(
        detail::formatMessage<Args...>(fmt, std::forward<Args>(args)...));
  }
}

/**
 * Throws std::runtime_error with formatted string.
 * `fmt` and `args` are passed to fmt::format().
 */
template <typename... Args>
[[noreturn]] void throwRuntime(
    fmt::format_string<detail::FormatArgT<Args>...> fmt,
    Args&&... args) {
  throw std::runtime_error(
      detail::formatMessage<Args...>(fmt, std::forward<Args>(args)...));
}

/** folly::to style conversion routines */

template <typename T, typename F>
T to(const F& x);

/** milliseconds to timeval_t */
template <>
inline timeval_t to<timeval_t>(const unsigned int& ms) {
  timeval_t r;
  r.tv_sec = ms / 1000;
  r.tv_usec = ms % 1000 * 1000;
  return r;
}

/** milliseconds to timeval_t */
template <>
inline timeval_t to<timeval_t>(const std::chrono::milliseconds& ms) {
  timeval_t r;
  r.tv_sec = ms.count() / 1000;
  r.tv_usec = ms.count() % 1000 * 1000;
  return r;
}

/** timeval_t to milliseconds */
template <>
inline std::chrono::milliseconds to<std::chrono::milliseconds>(
    const timeval_t& t) {
  using namespace std::chrono;
  return duration_cast<milliseconds>(
      seconds(t.tv_sec) + microseconds(t.tv_usec));
}

/**
 * True iff a and b point to the same region in memory
 */
inline bool sameMemoryRegion(folly::StringPiece a, folly::StringPiece b) {
  return (a.empty() && b.empty()) ||
      (a.size() == b.size() && a.begin() == b.begin());
}

/**
 * Returns value from map or default, if there is no key in map.
 */
template <class Map>
inline typename Map::mapped_type tryGet(
    const Map& map,
    const typename Map::key_type& key,
    const typename Map::mapped_type def = typename Map::mapped_type()) {
  auto it = map.find(key);
  return it == map.end() ? def : it->second;
}

/**
 * Returns string with length in [minLen, maxLen] and random characters
 * from range.
 */
std::string randomString(
    size_t minLen = 1,
    size_t maxLen = 20,
    folly::StringPiece range =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");

/**
 * Returns hash value for a given key.
 * To use for probabilistic sampling, e.g. for stats.
 */
uint32_t getMemcacheKeyHashValue(folly::StringPiece key);

/**
 * Checks if the given hash is within a range.
 * The range is from 0 to (MAX(uint32_t)/sample_rate)
 * Used for probabilistic decisions, like stats sampling.
 */
bool determineIfSampleKeyForViolet(
    uint32_t routingKeyHash,
    uint32_t sample_period);

/**
 * @return MD5 hash of a string
 */
std::string Md5Hash(folly::StringPiece input);

/**
 * @param Cmp  comparator used to compare characters
 *
 * @return  true if two strings are equal
 */
template <class Cmp>
bool equalStr(folly::StringPiece A, folly::StringPiece B, Cmp&& cmp) {
  if (A.size() != B.size()) {
    return false;
  }
  return std::equal(A.begin(), A.end(), B.begin(), std::forward<Cmp>(cmp));
}

/**
 * Writes 'contents' to the file, overwriting any existing contents
 * (will create if file doesn't exist)
 *
 * @return true on success, false otherwise
 */
bool writeStringToFile(folly::StringPiece contents, const std::string& path);

/**
 * Append 'contents' to the file (will create if file doesn't exist)
 *
 * @return true on success, false otherwise
 */
bool appendStringToFile(folly::StringPiece contents, const std::string& path);

/**
 * Write the given 'contents' to 'absFilename' atomically. This first writes
 * the contents to a temp file to in the absFilename's parent directory
 * and then calls 'rename()', which is atomic.
 *
 * @return true on success, false otherwise
 */
bool atomicallyWriteFileToDisk(
    folly::StringPiece contents,
    const std::string& absFilename);

/**
 * Analogue of UNIX touch: changes file access and modification time, if file
 * doesn't exist creates it.
 *
 * @return true on success, false otherwise
 */
bool touchFile(const std::string& path);

/**
 * Make uint64 random number out of uint32. Especially useful for mt19937.
 */
template <class RNG>
typename std::enable_if<RNG::word_size == 32, uint64_t>::type randomInt64(
    RNG& rng) {
  return ((uint64_t)rng() << 32) | (uint64_t)rng();
}

/**
 * Specialization for random generator with uint64 result type.
 */
template <class RNG>
typename std::enable_if<RNG::word_size == 64, uint64_t>::type randomInt64(
    RNG& rng) {
  return rng();
}

/**
 * @return name of current thread
 */
std::string getThreadName();

/**
 * Parse json string
 *
 * @param s - pointer to json formatted string
 * @param metadatamap - if non-null, will be populated with parse metadata
 * @param allow_trailing_comma - enabled to allow trailing comma by default
 *
 * @return dynamic struct containing the parsed json
 */
folly::dynamic parseJsonString(
    folly::StringPiece s,
    folly::json::metadata_map* metadataMap = nullptr,
    bool allow_trailing_comma = true);

/**
 * @return returns a prefix of `s` with '...' appended if s is longer than
 *         `maxLength`.
 */
std::string shorten(folly::StringPiece s, size_t maxLength);

/**
 * @return `s` where all occurences of `from` are replaced with `to`
 */
std::string
replaceAll(std::string s, const std::string& from, const std::string& to);

/**
 * Same as folly::toPrettyJson but also sorts keys in dictionaries
 */
std::string toPrettySortedJson(const folly::dynamic& json);

/**
 * Makes sure a directory exists and is writable (e.g. create if not found, etc)
 */
bool ensureDirExistsAndWritable(const std::string& path);

/**
 * Makes sure a directory exists and is writable.
 * Return more detailed information about errors to aid debugging.
 */
folly::Expected<folly::Unit, std::runtime_error>
ensureDirExistsAndWritableOrReturnError(const std::string& path);

/**
 * Makes sure that a file or directory has the desired permissions.
 */
bool ensureHasPermission(const std::string& path, mode_t mode);

/**
 * Makes sure that a file or directory has the desired permissions.
 * Return more detailed information about errors to aid debugging.
 */
folly::Expected<folly::Unit, std::runtime_error>
ensureHasPermissionOrReturnError(const std::string& path, mode_t mode);

/**
 * Determines if any two intervals are overlapping.
 */
bool intervalOverlap(std::vector<std::vector<size_t>>& intervals);
} // namespace memcache
} // namespace facebook

template <>
struct fmt::formatter<facebook::memcache::detail::FormattableDynamic> {
  constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) {
    auto it = ctx.begin();
    while (it != ctx.end() && *it != '}') {
      ++it;
    }
    spec_ = string_view(ctx.begin(), static_cast<size_t>(it - ctx.begin()));
    return it;
  }

  template <typename FormatContext>
  auto format(
      const facebook::memcache::detail::FormattableDynamic& arg,
      FormatContext& ctx) const -> decltype(ctx.out()) {
    const folly::dynamic& d = arg.value;
    switch (d.type()) {
      case folly::dynamic::NULLT:
        return write(ctx, string_view("(null)"));
      case folly::dynamic::BOOL:
        return write(ctx, d.getBool());
      case folly::dynamic::INT64:
        return write(ctx, d.getInt());
      case folly::dynamic::DOUBLE:
        return write(ctx, d.getDouble());
      case folly::dynamic::STRING:
        return write(ctx, string_view(d.getString()));
      case folly::dynamic::ARRAY:
      case folly::dynamic::OBJECT:
        break;
    }
    return write(ctx, folly::toJson(d));
  }

 private:
  template <typename FormatContext, typename T>
  auto write(FormatContext& ctx, const T& value) const -> decltype(ctx.out()) {
    if (spec_.size() == 0) {
      return fmt::format_to(ctx.out(), "{}", value);
    }
    std::string spec = "{:";
    spec.append(spec_.data(), spec_.size());
    spec += '}';
    return fmt::format_to(ctx.out(), fmt::runtime(spec), value);
  }

  string_view spec_;
};
