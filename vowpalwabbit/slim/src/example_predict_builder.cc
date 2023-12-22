#include "vw/slim/example_predict_builder.h"

#include "vw/common/uniform_hash.h"
#include "vw/core/hashstring.h"

namespace vw_slim
{
example_predict_builder::example_predict_builder(VW::example_predict* ex, const char* namespace_name) : _ex(ex)
{
  add_namespace(namespace_name[0]);
  _namespace_hash = VW::details::hashstring(namespace_name, strlen(namespace_name), 0);
}

example_predict_builder::example_predict_builder(VW::example_predict* ex, VW::namespace_index namespace_idx)
    : _ex(ex), _namespace_hash(namespace_idx)
{
  add_namespace(namespace_idx);
}

void example_predict_builder::add_namespace(VW::namespace_index feature_group) { _namespace_idx = feature_group; }

void example_predict_builder::push_feature_string(const char* feature_name, VW::feature_value value)
{
  VW::feature_index feature_hash = VW::details::hashstring(feature_name, strlen(feature_name), _namespace_hash);
  (*_ex)[_namespace_idx].add_feature_raw(feature_hash, value);
}

void example_predict_builder::push_feature(VW::feature_index feature_idx, VW::feature_value value)
{
  (*_ex)[_namespace_idx].add_feature_raw(_namespace_hash + feature_idx, value);
}
}  // namespace vw_slim
