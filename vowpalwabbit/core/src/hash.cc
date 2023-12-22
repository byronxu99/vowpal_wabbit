// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.

#include "vw/core/hash.h"

#include "vw/core/global_data.h"
#include "vw/core/parser.h"

namespace VW
{
VW::namespace_index hash_namespace(VW::workspace& all, const std::string& s)
{
  return VW::hash_namespace(s, all.runtime_config.hash_seed);
}

VW::namespace_index namespace_string_to_index(VW::workspace& all, const std::string& s)
{
  return VW::namespace_string_to_index(s, all.runtime_config.hash_seed);
}

VW::feature_index hash_feature(VW::workspace& all, const std::string& s, VW::namespace_index u)
{
  return all.parser_runtime.example_parser->hasher(s.data(), s.length(), u);
}

VW::feature_index hash_feature_cstr(VW::workspace& all, const char* fstr, VW::namespace_index u)
{
  return all.parser_runtime.example_parser->hasher(fstr, strlen(fstr), u);
}

VW::feature_index chain_hash(
    VW::workspace& all, const std::string& name, const std::string& value, VW::namespace_index u)
{
  return all.parser_runtime.example_parser->hasher(
      value.data(), value.length(), all.parser_runtime.example_parser->hasher(name.data(), name.length(), u));
}
}  // namespace VW
