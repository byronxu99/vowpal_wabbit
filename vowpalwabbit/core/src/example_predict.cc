// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.

#include "vw/core/example_predict.h"

#include "vw/common/future_compat.h"
#include "vw/common/uniform_hash.h"
#include "vw/core/constant.h"
#include "vw/core/hash.h"

#include <sstream>

std::vector<namespace_index> VW::example_predict::namespaces() const
{
  std::vector<namespace_index> result;
  result.reserve(_feature_space.size());
  for (const auto& ns : _feature_space) { result.push_back(ns.first); }
  return result;
}

bool VW::example_predict::empty(namespace_index ns) const
{
  auto it = _feature_space.find(ns);
  return it == _feature_space.end() || it->second.empty();
}

bool VW::example_predict::contains(const std::string& ns) const
{
  auto ns_index = VW::namespace_string_to_index(ns, _hash_seed);
  return contains(ns_index);
}

void VW::example_predict::delete_namespace(const std::string& ns)
{
  auto ns_index = VW::namespace_string_to_index(ns, _hash_seed);
  delete_namespace(ns_index);
}

VW::features& VW::example_predict::operator[](const std::string& ns)
{
  auto ns_index = VW::namespace_string_to_index(ns, _hash_seed);

  // If the namespace already exists, return it
  auto ft_iter = _feature_space.find(ns_index);
  if (ft_iter != _feature_space.end()) { return ft_iter->second; }

  // This creates a new namespace
  // operator[](namespace_index) will call _initialize_namespace
  VW::features& ft = (*this)[ns_index];

  // Because the namespace didn't exist before, we need to set its string name
  ft.namespace_name = ns;

  return ft;
}

VW::features& VW::example_predict::_initialize_namespace(namespace_index ns)
{
  // operator[] will create a new namespace
  VW::features& ft = _feature_space[ns];

  // Set namespace hash
  if (ns == VW::details::DEFAULT_NAMESPACE)
  {
    // Default namespace always has a constant index equal to VW::details::DEFAULT_NAMESPACE
    // However, its hash is the hash of VW::details::DEFAULT_NAMESPACE_STR
    // which varies depending on the hash seed
    ft.namespace_hash = hash_namespace(VW::details::DEFAULT_NAMESPACE_STR);
  }
  else
  {
    // For all other namespaces, index is equal to hash
    ft.namespace_hash = ns;
  }

  // Set namespace audit string for special namespaces
  if (ns == VW::details::DEFAULT_NAMESPACE) { ft.namespace_name = VW::details::DEFAULT_NAMESPACE_STR; }
  if (ns == VW::details::WILDCARD_NAMESPACE) { ft.namespace_name = VW::details::WILDCARD_NAMESPACE_STR; }
  if (ns == VW::details::WAP_LDF_NAMESPACE) { ft.namespace_name = ""; }
  if (ns == VW::details::HISTORY_NAMESPACE) { ft.namespace_name = ""; }
  if (ns == VW::details::CONSTANT_NAMESPACE) { ft.namespace_name = ""; }
  if (ns == VW::details::NN_OUTPUT_NAMESPACE) { ft.namespace_name = ""; }
  if (ns == VW::details::AUTOLINK_NAMESPACE) { ft.namespace_name = ""; }
  if (ns == VW::details::NEIGHBOR_NAMESPACE) { ft.namespace_name = "neighbor"; }
  if (ns == VW::details::AFFIX_NAMESPACE) { ft.namespace_name = "affix"; }
  if (ns == VW::details::SPELLING_NAMESPACE) { ft.namespace_name = "spelling"; }
  if (ns == VW::details::CONDITIONING_NAMESPACE) { ft.namespace_name = "search_condition"; }
  if (ns == VW::details::DICTIONARY_NAMESPACE) { ft.namespace_name = "dictionary"; }
  if (ns == VW::details::NODE_ID_NAMESPACE) { ft.namespace_name = ""; }
  if (ns == VW::details::BASELINE_ENABLED_MESSAGE_NAMESPACE) { ft.namespace_name = ""; }
  if (ns == VW::details::CCB_SLOT_NAMESPACE) { ft.namespace_name = ""; }
  if (ns == VW::details::CCB_ID_NAMESPACE) { ft.namespace_name = "_ccb_slot_index"; }
  if (ns == VW::details::IGL_FEEDBACK_NAMESPACE) { ft.namespace_name = ""; }

  return ft;
}

uint64_t VW::example_predict::get_or_calculate_order_independent_feature_space_hash()
{
  if (!_is_set_feature_space_hash)
  {
    _feature_space_hash = 0;

    for (const VW::namespace_index ns : *this)
    {
      _feature_space_hash += std::hash<namespace_index>()(ns);
      for (const auto& f : _feature_space[ns])
      {
        _feature_space_hash += std::hash<feature_index>()(f.index());
        _feature_space_hash += std::hash<feature_value>()(f.value());
      }
    }

    _is_set_feature_space_hash = true;
  }

  return _feature_space_hash;
}

bool VW::example_predict::get_string_name(VW::namespace_index ns, std::string& name_out) const
{
  auto iter = _feature_space.find(ns);
  if (iter != _feature_space.end())
  {
    name_out = iter->second.namespace_name;
    return true;
  }
  return false;
}

VW::namespace_index VW::example_predict::namespace_string_to_index(VW::string_view s) const
{
  return VW::namespace_string_to_index(s, _hash_seed);
}

VW::namespace_index VW::example_predict::hash_namespace(VW::string_view s) const
{
  return VW::hash_namespace(s, _hash_seed);
}

bool VW::example_predict::invert_hash_namespace(VW::namespace_index hash, std::string& name_out) const
{
  // In case hash is not equal to index, we must iterate through
  // all namespaces instead of using _feature_space.find()
  for (auto iter = _feature_space.begin(); iter != _feature_space.end(); ++iter)
  {
    if (iter->first == hash)
    {
      name_out = iter->second.namespace_name;
      return true;
    }
  }
  return false;
}

VW::scope_exit_guard VW::example_predict::stash_features()
{
  auto features_copy = _feature_space;

#ifdef HAS_STD14
  return VW::scope_exit_guard(
      [this, features_copy = std::move(features_copy)]() mutable
      {
        _feature_space = std::move(features_copy);
        _is_set_feature_space_hash = false;
      });
#else
  return VW::scope_exit_guard(
      [this, features_copy]() mutable
      {
        _feature_space = std::move(features_copy);
        _is_set_feature_space_hash = false;
      });
#endif
}

VW::scope_exit_guard VW::example_predict::stash_interactions()
{
  VW::interaction_spec_type interactions_copy = *interactions;

#ifdef HAS_STD14
  return VW::scope_exit_guard([this, interactions_copy = std::move(interactions_copy)]() mutable
      { *interactions = std::move(interactions_copy); });
#else
  return VW::scope_exit_guard([this, interactions_copy]() mutable { *interactions = std::move(interactions_copy); });
#endif
}

VW::scope_exit_guard VW::example_predict::stash_scale_offset()
{
  auto scale_copy = ft_index_scale;
  auto offset_copy = ft_index_offset;

  return VW::scope_exit_guard(
      [this, scale_copy, offset_copy]() mutable
      {
        ft_index_scale = scale_copy;
        ft_index_offset = offset_copy;
      });
}
