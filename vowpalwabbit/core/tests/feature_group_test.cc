// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.

#include "vw/core/feature_group.h"

#include "vw/common/uniform_hash.h"
#include "vw/core/scope_exit.h"
#include "vw/core/unique_sort.h"
#include "vw/core/vw.h"
#include "vw/test_common/test_common.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace ::testing;

TEST(FeatureGroup, UniqueFeatureGroupTest)
{
  VW::features fs;
  fs.add_feature_raw(1, 1.f);
  fs.add_feature_raw(2, 1.f);
  fs.add_feature_raw(1, 1.f);
  fs.add_feature_raw(1, 1.f);
  fs.add_feature_raw(25, 1.f);
  fs.add_feature_raw(3, 1.f);
  fs.add_feature_raw(3, 1.f);
  fs.add_feature_raw(3, 1.f);
  fs.add_feature_raw(5, 1.f);
  fs.add_feature_raw(7, 1.f);
  fs.add_feature_raw(13, 1.f);
  fs.add_feature_raw(11, 1.f);
  fs.add_feature_raw(12, 1.f);

  const auto parse_mask = (static_cast<uint64_t>(1) << 18) - 1;
  fs.sort(parse_mask);

  auto fs_copy1 = fs;
  auto fs_copy2 = fs;
  auto fs_copy3 = fs;
  auto fs_copy4 = fs;

  // Cap at 5
  VW::unique_features(fs, 5);
  EXPECT_THAT(fs.indices, ElementsAre(1, 2, 3, 5, 7));

  // Uncapped
  VW::unique_features(fs_copy1);
  EXPECT_THAT(fs_copy1.indices, ElementsAre(1, 2, 3, 5, 7, 11, 12, 13, 25));

  // Special case at max 1
  VW::unique_features(fs_copy2, 1);
  EXPECT_THAT(fs_copy2.indices, ElementsAre(1));

  // Special case for max 0
  VW::unique_features(fs_copy3, 0);
  EXPECT_TRUE(fs_copy3.empty());

  // Explicit negative input that isn't -1
  VW::unique_features(fs_copy4, -10);
  EXPECT_THAT(fs_copy4.indices, ElementsAre(1, 2, 3, 5, 7, 11, 12, 13, 25));

  // Special case for max 0
  VW::features empty_features;
  VW::unique_features(empty_features, 0);
  EXPECT_TRUE(empty_features.empty());

  VW::features fs_size_one;
  fs_size_one.add_feature_raw(1, 1.f);
  VW::unique_features(fs_size_one);
  EXPECT_THAT(fs_size_one.indices, ElementsAre(1));
}

TEST(FeatureGroup, SortFeatureGroupTest)
{
  VW::features fs;
  fs.add_feature_raw(1, 1.f);
  fs.add_feature_raw(25, 1.f);
  fs.add_feature_raw(3, 1.f);
  fs.add_feature_raw(5, 1.f);
  fs.add_feature_raw(7, 1.f);
  fs.add_feature_raw(13, 1.f);
  fs.add_feature_raw(11, 1.f);
  fs.add_feature_raw(12, 1.f);

  const auto parse_mask = (static_cast<uint64_t>(1) << 18) - 1;
  fs.sort(parse_mask);

  EXPECT_THAT(fs.indices, ElementsAre(1, 3, 5, 7, 11, 12, 13, 25));
}
