// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.

#pragma once
#include "vw/common/future_compat.h"
#include "vw/common/hash.h"
#include "vw/common/string_view.h"
#include "vw/core/constant.h"
#include "vw/core/global_data.h"
#include "vw/core/hashstring.h"
#include "vw/core/vw_fwd.h"

#include <cstddef>  // defines size_t
#include <cstdint>
#include <string>

namespace VW
{
// Namespace hashing functions
// Hash algorithm is VW::uniform_hash() in vw/common/hash.h
// Seed is typically provided as a command line argument
inline VW::namespace_index hash_namespace(VW::string_view ns, uint64_t seed = 0) { return VW::uniform_hash(ns, seed); }

inline VW::namespace_index hash_namespace(const char* ns, uint64_t seed = 0)
{
  return VW::uniform_hash(ns, std::strlen(ns), seed);
}

inline VW::namespace_index hash_namespace(VW::workspace& all, const std::string& s)
{
  // return all.parser_runtime.example_parser->hasher(s.data(), s.length(), all.runtime_config.hash_seed);
  return hash_namespace(s, all.runtime_config.hash_seed);
}

inline VW::namespace_index hash_namespace_static(const std::string& s, const std::string& hash)
{
  // return get_hasher(hash)(s.data(), s.length(), 0);
  return hash_namespace(s, 0);
}

// Feature hashing functions
// Hash algorithm is either hashall() or hashstring() in vw/core/hashstring.h
// hashall() is equivalent to VW::uniform_hash()
// hashstring() will detect if input is integer and will simply add the value to the namespace hash
// Seed is the namespace hash
inline VW::feature_index hash_feature(VW::workspace& all, const std::string& s, VW::namespace_index u)
{
  // return all.parser_runtime.example_parser->hasher(s.data(), s.length(), u) & all.runtime_state.parse_mask;
  return all.parser_runtime.example_parser->hasher(s.data(), s.length(), u);
}
inline VW::feature_index hash_feature_static(
    const std::string& s, VW::namespace_index u, const std::string& h, uint32_t num_bits)
{
  // size_t parse_mark = (1 << num_bits) - 1;
  // return get_hasher(h)(s.data(), s.length(), u) & parse_mark;
  return get_hasher(h)(s.data(), s.length(), u);
}

inline VW::feature_index hash_feature_cstr(VW::workspace& all, const char* fstr, VW::namespace_index u)
{
  // return all.parser_runtime.example_parser->hasher(fstr, strlen(fstr), u) & all.runtime_state.parse_mask;
  return all.parser_runtime.example_parser->hasher(fstr, strlen(fstr), u);
}

// Hash feature assuming that the feature is a string
inline VW::feature_index hash_feature(VW::string_view ft_name, VW::namespace_index ns_hash)
{
  return VW::uniform_hash(ft_name, ns_hash);
}

// Hash feature assuming that the feature is an integer
inline VW::feature_index hash_feature(VW::feature_index ft_index, VW::namespace_index ns_hash)
{
  return ft_index + ns_hash;
}

// Chain hashing functions
// Chain hashing is used when feature value is a string
// Chain hash is hash(feature_value, hash(feature_name, namespace_hash))
inline VW::feature_index chain_hash(
    VW::workspace& all, const std::string& name, const std::string& value, VW::namespace_index u)
{
  return all.parser_runtime.example_parser->hasher(
      value.data(), value.length(), all.parser_runtime.example_parser->hasher(name.data(), name.length(), u));
  //&all.runtime_state.parse_mask;
}

inline VW::feature_index chain_hash_static(
    const std::string& name, const std::string& value, VW::namespace_index u, hash_func_t hash_func)
{
  // return hash_func(value.data(), value.length(), hash_func(name.data(), name.length(), u)) & parse_mask;
  return hash_func(value.data(), value.length(), hash_func(name.data(), name.length(), u));
}

// Chain hash assuming that the feature name and value are strings
inline VW::feature_index chain_hash_feature(
    VW::string_view ft_name, VW::string_view ft_value, VW::namespace_index ns_hash)
{
  return VW::uniform_hash(ft_value, VW::hash_feature(ft_name, ns_hash));
}

// Chain hash assuming that the feature name is an integer and the feature value is a string
inline VW::feature_index chain_hash_feature(
    VW::feature_index ft_index, VW::string_view ft_value, VW::namespace_index ns_hash)
{
  return VW::uniform_hash(ft_value, VW::hash_feature(ft_index, ns_hash));
}

}  // namespace VW
