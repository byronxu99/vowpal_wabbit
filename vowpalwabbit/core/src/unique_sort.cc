// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.
#include "vw/core/unique_sort.h"

#include "vw/core/example.h"
#include "vw/core/feature_group.h"

#include <algorithm>

void VW::unique_features(features& fs, int max)
{
  if (fs.indices.empty()) { return; }
  if (max == 0)
  {
    fs.clear();
    return;
  }
  if (max == 1)
  {
    fs.truncate_to(1);
    return;
  }

  auto last_index = std::size_t{0};
  for (auto i = std::size_t{1}; i != fs.size(); ++i)
  {
    if (fs.indices[i] != fs.indices[last_index])
    {
      if (i != ++last_index)
      {
        fs.values[last_index] = fs.values[i];
        fs.indices[last_index] = fs.indices[i];
        if (!fs.audit_info.empty()) { fs.audit_info[last_index] = std::move(fs.audit_info[i]); }
      }

      const auto unique_items_found = last_index + 1;
      // Rely on a negative integer to wrap around and be larger if a user passed in a negative value.
      if (unique_items_found >= static_cast<size_t>(max)) { break; }
    }
  }
  ++last_index;
  fs.truncate_to(last_index);
}

void VW::unique_sort_features(uint64_t parse_mask, VW::example& ae)
{
  for (auto ns : ae)
  {
    if (ae[ns].sort(parse_mask)) { unique_features(ae[ns]); }
  }

  ae.sorted = true;
}
