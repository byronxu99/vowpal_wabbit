#pragma once

#include "vw/common/hash.h"
#include "vw/core/feature_group.h"
#include "vw/core/global_data.h"
#include "vw/core/parse_primitives.h"
#include "vw/core/vw.h"

#include <cstdint>
#include <string>
#include <vector>

namespace VW
{
namespace parsers
{
namespace json
{
namespace details
{
template <bool audit>
class namespace_builder
{
public:
  example* ex;
  std::string name;
  bool hash_all;
  features* ftrs;

  namespace_builder(example* _ex, const char* _name, bool _hash_all) : ex(_ex), name(_name), hash_all(_hash_all)
  {
    // operator[] will create a new namespace if it doesn't exist
    ftrs = &(*ex)[name];
  }

  ~namespace_builder()
  {
    // delete namespace if it is empty
    if (ftrs->size() == 0) { ex->delete_namespace(name); }
  }

  // Add a feature with integer index and float value
  void add_feature(feature_index i, feature_value v)
  {
    // filter out 0-values
    if (v == 0) { return; }

    ftrs->add_feature(i, v, audit);
  }

  // Add a feature with integer index, float value, and custom audit string
  void add_feature(feature_index i, feature_value v, std::string feature_name)
  {
    // filter out 0-values
    if (v == 0) { return; }

    ftrs->add_feature_raw(i, v);
    if (audit) { ftrs->add_audit_string(std::move(feature_name)); }
  }

  // Add a feature with string name and float value
  // Feature name may be parsed as int depending on hash_all
  void add_feature(const char* name, feature_value value = 1.f)
  {
    if (name == nullptr || name[0] == '\0') { return; }

    if (hash_all)
    {
      // always treat feature name as a string
      ftrs->add_feature(name, value, audit);
    }
    else
    {
      // check if the string is an integer
      if (VW::details::is_string_integer(name))
      {
        VW::feature_index index = std::strtoll(name, nullptr, 10);
        ftrs->add_feature(index, value, audit);
      }
      else { ftrs->add_feature(name, value, audit); }
    }
  }

  // Add a feature with string name and string value
  // Feature name may be parsed as int depending on hash_all
  // Feature value is assumed to be a string and will not be parsed as float
  void add_feature(const char* name, const char* value)
  {
    if (name == nullptr || name[0] == '\0') { return; }

    if (hash_all)
    {
      // always treat feature name as a string
      ftrs->add_feature(name, value, audit);
    }
    else
    {
      // check if the string is an integer
      if (VW::details::is_string_integer(name))
      {
        VW::feature_index index = std::strtoll(name, nullptr, 10);
        ftrs->add_feature(index, value, audit);
      }
      else { ftrs->add_feature(name, value, audit); }
    }
  }
};

}  // namespace details
}  // namespace json
}  // namespace parsers
}  // namespace VW
