// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.
#pragma once

#include "vw/common/future_compat.h"
#include "vw/core/constant.h"
#include "vw/core/feature_group.h"
#include "vw/core/hash.h"
#include "vw/core/reduction_features.h"
#include "vw/core/scope_exit.h"
#include "vw/core/v_array.h"
#include "vw/core/vw_fwd.h"  // for VW::namespace_index

#include <string>
#include <unordered_map>
#include <vector>

namespace VW
{
// Namespace index is the hash of the namespace.
// Features object contains the features in that namespace.
using feature_groups_type = std::unordered_map<namespace_index, features>;

// Interactions are specified by a vector of vectors of namespace indices
// Each inner vector denotes one interaction
// Each interaction is a list of namespace indices
using interaction_spec_type = std::vector<std::vector<namespace_index>>;

namespace details
{
// Iterator object for example_predict
// This is a wrapper around feature_groups_type::iterator
// *iter returns namespace index
// iter.index() also returns namespace index
// iter.features() returns features object
template <typename NamespaceT, typename FeaturesT, typename IteratorT>
class example_predict_iterator final
{
public:
  example_predict_iterator(IteratorT iter) : _iter(iter) {}

  // operator* returns namespace index
  inline NamespaceT operator*() { return _iter->first; }

  // operator++ increments the iterator
  inline example_predict_iterator<NamespaceT, FeaturesT, IteratorT>& operator++()
  {
    _iter++;
    return *this;
  }

  inline NamespaceT index() { return _iter->first; }
  inline FeaturesT& features() { return _iter->second; }

  inline bool operator==(const example_predict_iterator<NamespaceT, FeaturesT, IteratorT>& rhs) const
  {
    return _iter == rhs._iter;
  }
  inline bool operator!=(const example_predict_iterator<NamespaceT, FeaturesT, IteratorT>& rhs) const
  {
    return _iter != rhs._iter;
  }

private:
  IteratorT _iter;
};
}  // namespace details

class example_predict
{
public:
  using iterator = details::example_predict_iterator<namespace_index, features, VW::feature_groups_type::iterator>;
  using const_iterator =
      details::example_predict_iterator<const namespace_index, const features, VW::feature_groups_type::const_iterator>;

  example_predict() = default;
  ~example_predict() = default;
  example_predict(const example_predict&) = delete;
  example_predict& operator=(const example_predict&) = delete;
  example_predict(example_predict&& other) = default;
  example_predict& operator=(example_predict&& other) = default;

  // this hashing function does not take into account the order of the features
  // an example with the exact same namespaces / features-values but in a different order will have the same hash
  uint64_t get_or_calculate_order_independent_feature_space_hash();
  inline void clear_feature_space_hash() { _is_set_feature_space_hash = false; }

  /// If namespaces are modified this iterator is invalidated.
  inline iterator begin() { return iterator(_feature_space.begin()); }
  inline const_iterator begin() const { return const_iterator(_feature_space.cbegin()); }

  /// If namespaces are modified this iterator is invalidated.
  inline iterator end() { return iterator(_feature_space.end()); }
  inline const_iterator end() const { return const_iterator(_feature_space.cend()); }

  // Number of namespaces in the example.
  inline size_t size() const { return _feature_space.size(); }

  // Get all namespaces in the example.
  std::vector<namespace_index> namespaces() const;

  // Check if the example contains a namespace.
  inline bool contains(namespace_index ns) const { return _feature_space.find(ns) != _feature_space.end(); }
  bool contains(const std::string& ns) const;

  // Remove a namespace from the example.
  inline void delete_namespace(namespace_index ns) { _feature_space.erase(ns); }
  void delete_namespace(const std::string& ns);

  // Remove all namespace from the example.
  inline void delete_all_namespaces() { _feature_space.clear(); }

  // Is the example empty?
  inline bool empty() const { return _feature_space.empty(); }

  // Is the namespace empty?
  bool empty(namespace_index ns) const;

  // Get the features object for a namespace.
  // This will create a new namespace if it doesn't exist.
  features& operator[](const std::string& ns);
  inline features& operator[](namespace_index ns)
  {
    auto iter = _feature_space.find(ns);
    if (iter == _feature_space.end()) { return _initialize_namespace(ns); }
    else { return iter->second; }
  }

  // Get the features for a namespace, const version.
  // This will throw an exception if the namespace doesn't exist.
  const features& operator[](const std::string& ns) const;
  inline const features& operator[](namespace_index ns) const { return _feature_space.at(ns); }

  // Get all features
  // inline feature_groups_type& feature_space() { return _feature_space; }
  inline const feature_groups_type& feature_space() const { return _feature_space; }

  // Get the string name of a namespace
  // Returns boolean indicating if the namespace was found
  bool get_string_name(VW::namespace_index ns, std::string& name_out) const;

  // Compute the hash of a namespace string using the hash seed in this example object
  inline VW::namespace_index hash_namespace(VW::string_view s) const { return VW::hash_namespace(s, _hash_seed); }

  // Convert a namespace hash back to a string
  // Not computationally efficient, may traverse all namespaces in the example
  // Returns boolean indicating if the namespace was found
  bool invert_hash_namespace(VW::namespace_index hash, std::string& name_out) const;

  // Stash a copy of all feature groups, interaction specifications, or scale and offset
  // When the returned scope exit guard is destroyed, the stashed data is restored
  // This is used to undo any changes to the stashed parts of the example
  VW::scope_exit_guard stash_features();
  VW::scope_exit_guard stash_interactions();
  VW::scope_exit_guard stash_scale_offset();

  // Weight index = feature_index * ft_index_scale + ft_index_offset
  // Computed by the function feature_to_weight_index in interactions_predict.h
  uint64_t ft_index_scale = 1;   // Scaling factor for feature indices
  uint64_t ft_index_offset = 0;  // Offset for feature indices

  // Interactions specified for the example
  VW::interaction_spec_type* interactions = nullptr;

  // Optional
  reduction_features ex_reduction_features;

  // Used for debugging reductions.  Keeps track of current reduction level.
  uint32_t debug_current_reduction_depth = 0;

protected:
  // Hash seed for generating namespace hashes.
  // This must be set from VW::workspace when the example is created
  // The relevant functions are declared as friend to access _hash_seed
  uint64_t _hash_seed = 0;

private:
  // Feature groups in the example.
  VW::feature_groups_type _feature_space;

  // A unique hash of the feature space and namespaces of the example.
  uint64_t _feature_space_hash = 0;
  bool _is_set_feature_space_hash = false;

  // Initialize a new namespace in the example object
  features& _initialize_namespace(namespace_index ns);
};
}  // namespace VW

using namespace_index VW_DEPRECATED("namespace_index moved into VW namespace") = VW::namespace_index;
using example_predict VW_DEPRECATED("example_predict moved into VW namespace") = VW::example_predict;
