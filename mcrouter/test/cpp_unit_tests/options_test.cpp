/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "mcrouter/options.h"

using facebook::memcache::McrouterOptions;
using std::string;
using std::unordered_map;
using std::vector;

TEST(OptionsSetFromDictTest, sanity) {
  McrouterOptions opts;
  unordered_map<string, string> dict;

  auto e = opts.updateFromDict(dict);
  EXPECT_TRUE(e.empty());

  /* default */
  EXPECT_TRUE(opts.num_proxies == 1);

  dict["num_proxies"] = "4";
  e = opts.updateFromDict(dict);
  EXPECT_TRUE(e.empty());
  EXPECT_TRUE(opts.num_proxies == 4);

  dict.clear();
  dict["num_proxies"] = "a";
  e = opts.updateFromDict(dict);
  EXPECT_EQ(e.size(), 1);
  EXPECT_EQ(e[0].requestedName, "num_proxies");
  EXPECT_EQ(e[0].requestedValue, "a");
  /* unchanged */
  EXPECT_TRUE(opts.num_proxies == 4);

  dict.clear();
  dict["blah"] = "a";
  e = opts.updateFromDict(dict);
  /* unknown options don't cause errors */
  EXPECT_EQ(e.size(), 0);
  /* unchanged */
  EXPECT_TRUE(opts.num_proxies == 4);

  dict.clear();
  dict["enable_tw_crash_config_backup_path"] = "1";
  e = opts.updateFromDict(dict);
  EXPECT_TRUE(e.empty());
  EXPECT_TRUE(opts.enable_tw_crash_config_backup_path);
}

TEST(OptionsSetFromDictTest, StringMapValueMayContainColons) {
  McrouterOptions opts;
  const unordered_map<string, string> dict{
      {"config_params", "proc-name:ray::RayTrainWorker,environment:prod"}};

  const auto errors = opts.updateFromDict(dict);

  EXPECT_TRUE(errors.empty());
  EXPECT_EQ(opts.config_params.at("proc-name"), "ray::RayTrainWorker");
  EXPECT_EQ(opts.config_params.at("environment"), "prod");
}

TEST(OptionsSetFromDictTest, EmptyStringMapParsesToEmptyMap) {
  EXPECT_TRUE(facebook::memcache::options::parseStringMapOption("").empty());

  McrouterOptions opts;
  EXPECT_TRUE(opts.updateFromDict({{"config_params", ""}}).empty());
  EXPECT_TRUE(opts.config_params.empty());
}

TEST(OptionsSetFromDictTest, EmptyStringMapClearsExistingValue) {
  McrouterOptions opts;
  opts.config_params = {{"existing", "value"}};

  EXPECT_TRUE(opts.updateFromDict({{"config_params", ""}}).empty());
  EXPECT_TRUE(opts.config_params.empty());
}

TEST(OptionsSetFromDictTest, DefaultOptionsRoundTripWithEmptyStringMap) {
  McrouterOptions opts;
  const auto dict = opts.toDict();
  ASSERT_EQ(dict.at("config_params"), "");

  ASSERT_TRUE(opts.updateFromDict(dict).empty());
  EXPECT_TRUE(opts.config_params.empty());
  EXPECT_TRUE(opts.compare(dict).empty());
}

TEST(OptionsSetFromDictTest, StringMapRejectsMalformedPairs) {
  for (const auto& value : {
           "missing-delimiter",
           ":missing-name",
           ",a:1",
           "a:1,",
           "a:1,,b:2",
           ",",
       }) {
    McrouterOptions opts;
    opts.config_params = {{"existing", "value"}};
    const unordered_map<string, string> dict{{"config_params", value}};

    const auto errors = opts.updateFromDict(dict);

    ASSERT_EQ(errors.size(), 1);
    EXPECT_EQ(errors[0].requestedName, "config_params");
    EXPECT_EQ(errors[0].requestedValue, value);
    EXPECT_EQ(
        opts.config_params,
        (unordered_map<string, string>{{"existing", "value"}}));
  }
}

TEST(OptionsCompareTest, EmptyStringMapMismatchIsNotAConversionError) {
  McrouterOptions opts;
  opts.config_params = {{"existing", "value"}};
  auto dict = opts.toDict();
  dict["config_params"] = "";

  const auto errors = opts.compare(dict);
  ASSERT_EQ(errors.size(), 1);
  EXPECT_EQ(errors[0].optionName, "config_params");
  EXPECT_EQ(errors[0].errorMsg.find("couldn't convert value"), string::npos);
}

TEST(OptionsCompareTest, ToggleTrueAndOneCompareEqual) {
  McrouterOptions opts;
  opts.config_params = {{"existing", "value"}};
  opts.enable_tw_crash_config_backup_path = true;
  auto dict = opts.toDict();
  ASSERT_EQ(dict.at("enable_tw_crash_config_backup_path"), "1");
  dict["enable_tw_crash_config_backup_path"] = "true";

  EXPECT_TRUE(opts.compare(dict).empty());
}

TEST(OptionsCompareTest, StringTrueAndOneRemainDifferent) {
  McrouterOptions opts;
  opts.config_params = {{"existing", "value"}};
  opts.service_name = "1";
  auto dict = opts.toDict();
  dict["service_name"] = "true";

  const auto errors = opts.compare(dict);
  ASSERT_EQ(errors.size(), 1);
  EXPECT_EQ(errors[0].optionName, "service_name");
}

TEST(OptionsCompareTest, StringMapTrueAndOneRemainDifferent) {
  McrouterOptions opts;
  opts.config_params = {{"enabled", "1"}};
  auto dict = opts.toDict();
  dict["config_params"] = "enabled:true";

  const auto errors = opts.compare(dict);
  ASSERT_EQ(errors.size(), 1);
  EXPECT_EQ(errors[0].optionName, "config_params");
}

TEST(OptionsCompareTest, NumericSpellingsRemainDifferent) {
  McrouterOptions opts;
  opts.config_params = {{"existing", "value"}};
  opts.num_proxies = 1;
  auto dict = opts.toDict();
  dict["num_proxies"] = "01";

  const auto errors = opts.compare(dict);
  ASSERT_EQ(errors.size(), 1);
  EXPECT_EQ(errors[0].optionName, "num_proxies");
}

TEST(OptionsCompareTest, InvalidToggleReportsConversionError) {
  McrouterOptions opts;
  opts.config_params = {{"existing", "value"}};
  auto dict = opts.toDict();
  dict["enable_tw_crash_config_backup_path"] = "2";

  const auto errors = opts.compare(dict);
  ASSERT_EQ(errors.size(), 1);
  EXPECT_EQ(errors[0].optionName, "enable_tw_crash_config_backup_path");
  EXPECT_NE(errors[0].errorMsg.find("couldn't convert value"), string::npos);
}
