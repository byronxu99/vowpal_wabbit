#pragma once

#include "vw_slim_predict.h"

namespace vw_slim
{
class example_predict_builder
{
public:
  example_predict_builder(VW::example_predict* ex, const char* namespace_name);
  example_predict_builder(VW::example_predict* ex, VW::namespace_index namespace_idx);

  void push_feature_string(const char* feature_idx, VW::feature_value value);
  void push_feature(VW::feature_index feature_idx, VW::feature_value value);

private:
  VW::example_predict* _ex;
  VW::namespace_index _namespace_idx;
  uint64_t _namespace_hash;

  void add_namespace(VW::namespace_index feature_group);
};
}  // namespace vw_slim
