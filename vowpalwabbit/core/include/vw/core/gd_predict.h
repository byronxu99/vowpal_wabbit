// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.
#pragma once

#include "vw/core/debug_log.h"
#include "vw/core/example_predict.h"
#include "vw/core/feature_group.h"
#include "vw/core/interactions_predict.h"
#include "vw/core/v_array.h"

#undef VW_DEBUG_LOG
#define VW_DEBUG_LOG vw_dbg::GD_PREDICT

namespace VW
{
namespace details
{
template <class DataT>
inline void dummy_func(DataT&, const VW::audit_strings*)
{
}  // should never be called due to call_audit overload

inline void vec_add(VW::feature_value& p, VW::feature_value fx, VW::feature_value fw) { p += fw * fx; }

}  // namespace details

// iterate through one namespace (or its part), callback function FuncT(some_data_R, feature_value_x, feature_index)
template <class DataT, void (*FuncT)(DataT&, VW::feature_value feature_value, VW::feature_index feature_index),
    class WeightsT>
void foreach_feature(WeightsT& /*weights*/, const VW::features& fs, DataT& dat, VW::feature_index scale,
    VW::feature_index offset, VW::feature_value mult = 1.)
{
  for (const auto& f : fs)
  {
    FuncT(dat, mult * f.value(), VW::details::feature_to_weight_index(f.index(), scale, offset));
  }
}

// iterate through one namespace (or its part), callback function FuncT(some_data_R, feature_value_x, feature_weight)
template <class DataT,
    void (*FuncT)(DataT&, const VW::feature_value feature_value, VW::feature_value& weight_reference), class WeightsT>
inline void foreach_feature(WeightsT& weights, const VW::features& fs, DataT& dat, VW::feature_index scale,
    VW::feature_index offset, VW::feature_value mult = 1.)
{
  for (const auto& f : fs)
  {
    VW::weight& w = weights[VW::details::feature_to_weight_index(f.index(), scale, offset)];
    FuncT(dat, mult * f.value(), w);
  }
}

// iterate through one namespace (or its part), callback function FuncT(some_data_R, feature_value_x, feature_weight)
template <class DataT, void (*FuncT)(DataT&, VW::feature_value, VW::feature_value), class WeightsT>
inline void foreach_feature(const WeightsT& weights, const VW::features& fs, DataT& dat, VW::feature_index scale,
    VW::feature_index offset, VW::feature_value mult = 1.)
{
  for (const auto& f : fs)
  {
    FuncT(dat, mult * f.value(),
        weights.get(static_cast<size_t>(VW::details::feature_to_weight_index(f.index(), scale, offset))));
  }
}

template <class DataT, class WeightOrIndexT, void (*FuncT)(DataT&, VW::feature_value, WeightOrIndexT),
    class WeightsT>  // nullptr func can't be used as template param in old
                     // compilers

inline void generate_interactions(const std::vector<std::vector<VW::namespace_index>>& interactions, bool permutations,
    VW::example_predict& ec, DataT& dat, WeightsT& weights, size_t& num_interacted_features,
    VW::details::generate_interactions_object_cache& cache)  // default value removed to eliminate
                                                             // ambiguity in old complers
{
  VW::generate_interactions<DataT, WeightOrIndexT, FuncT, false, details::dummy_func<DataT>, WeightsT>(
      interactions, permutations, ec, dat, weights, num_interacted_features, cache);
}

// iterate through all namespaces and quadratic&cubic features, callback function FuncT(some_data_R, feature_value_x,
// WeightOrIndexT) where WeightOrIndexT is EITHER float& feature_weight OR uint64_t feature_index
template <class DataT, class WeightOrIndexT, void (*FuncT)(DataT&, VW::feature_value, WeightOrIndexT), class WeightsT>
inline void foreach_feature(WeightsT& weights, bool ignore_some_linear,
    std::array<bool, VW::NUM_NAMESPACES>& ignore_linear,
    const std::vector<std::vector<VW::namespace_index>>& interactions, bool permutations, VW::example_predict& ec,
    DataT& dat, size_t& num_interacted_features, VW::details::generate_interactions_object_cache& cache)
{
  VW::feature_index scale = ec.ft_index_scale;
  VW::feature_index offset = ec.ft_index_offset;

  if (ignore_some_linear)
  {
    for (VW::example_predict::iterator i = ec.begin(); i != ec.end(); ++i)
    {
      if (!ignore_linear[i.index()])
      {
        VW::features& f = *i;
        foreach_feature<DataT, FuncT, WeightsT>(weights, f, dat, scale, offset);
      }
    }
  }
  else
  {
    for (VW::features& f : ec) { foreach_feature<DataT, FuncT, WeightsT>(weights, f, dat, scale, offset); }
  }

  generate_interactions<DataT, WeightOrIndexT, FuncT, WeightsT>(
      interactions, permutations, ec, dat, weights, num_interacted_features, cache);
}

template <class DataT, class WeightOrIndexT, void (*FuncT)(DataT&, VW::feature_value, WeightOrIndexT), class WeightsT>
inline void foreach_feature(WeightsT& weights, bool ignore_some_linear,
    std::array<bool, VW::NUM_NAMESPACES>& ignore_linear,
    const std::vector<std::vector<VW::namespace_index>>& interactions, bool permutations, VW::example_predict& ec,
    DataT& dat, VW::details::generate_interactions_object_cache& cache)
{
  size_t num_interacted_features_ignored = 0;
  foreach_feature<DataT, WeightOrIndexT, FuncT, WeightsT>(weights, ignore_some_linear, ignore_linear, interactions,
      permutations, ec, dat, num_interacted_features_ignored, cache);
}

template <class WeightsT>
inline VW::feature_value inline_predict(WeightsT& weights, bool ignore_some_linear,
    std::array<bool, VW::NUM_NAMESPACES>& ignore_linear,
    const std::vector<std::vector<VW::namespace_index>>& interactions, bool permutations, VW::example_predict& ec,
    VW::details::generate_interactions_object_cache& cache, VW::feature_value initial = 0.f)
{
  foreach_feature<VW::feature_value, VW::feature_value, details::vec_add, WeightsT>(
      weights, ignore_some_linear, ignore_linear, interactions, permutations, ec, initial, cache);
  return initial;
}

template <class WeightsT>
inline VW::feature_value inline_predict(WeightsT& weights, bool ignore_some_linear,
    std::array<bool, VW::NUM_NAMESPACES>& ignore_linear,
    const std::vector<std::vector<VW::namespace_index>>& interactions, bool permutations, VW::example_predict& ec,
    size_t& num_interacted_features, VW::details::generate_interactions_object_cache& cache,
    VW::feature_value initial = 0.f)
{
  foreach_feature<VW::feature_value, VW::feature_value, details::vec_add, WeightsT>(weights, ignore_some_linear,
      ignore_linear, interactions, permutations, ec, initial, num_interacted_features, cache);
  return initial;
}
}  // namespace VW

namespace GD
{

// iterate through one namespace (or its part), callback function FuncT(some_data_R, feature_value_x, feature_index)
template <class DataT, void (*FuncT)(DataT&, VW::feature_value feature_value, VW::feature_index feature_index),
    class WeightsT>
VW_DEPRECATED("Moved to VW namespace")
void foreach_feature(WeightsT& weights, const VW::features& fs, DataT& dat, VW::feature_index scale,
    VW::feature_index offset, VW::feature_value mult = 1.)
{
  VW::foreach_feature<DataT, FuncT, WeightsT>(weights, fs, dat, scale, offset, mult);
}

// iterate through one namespace (or its part), callback function FuncT(some_data_R, feature_value_x, feature_weight)
template <class DataT,
    void (*FuncT)(DataT&, const VW::feature_value feature_value, VW::feature_value& weight_reference), class WeightsT>
VW_DEPRECATED("Moved to VW namespace")
inline void foreach_feature(WeightsT& weights, const VW::features& fs, DataT& dat, VW::feature_index scale,
    VW::feature_index offset, VW::feature_value mult = 1.)
{
  VW::foreach_feature<DataT, FuncT, WeightsT>(weights, fs, dat, scale, offset, mult);
}

// iterate through one namespace (or its part), callback function FuncT(some_data_R, feature_value_x, feature_weight)
template <class DataT, void (*FuncT)(DataT&, VW::feature_value, VW::feature_value), class WeightsT>
VW_DEPRECATED("Moved to VW namespace")
inline void foreach_feature(const WeightsT& weights, const VW::features& fs, DataT& dat, VW::feature_index scale,
    VW::feature_index offset, VW::feature_value mult = 1.)
{
  VW::foreach_feature<DataT, FuncT, WeightsT>(weights, fs, dat, scale, offset, mult);
}

template <class DataT, class WeightOrIndexT, void (*FuncT)(DataT&, VW::feature_value, WeightOrIndexT),
    class WeightsT>  // nullptr func can't be used as template param in old
                     // compilers
VW_DEPRECATED("Moved to VW namespace") inline void generate_interactions(
    const std::vector<std::vector<VW::namespace_index>>& interactions, bool permutations, VW::example_predict& ec,
    DataT& dat, WeightsT& weights, size_t& num_interacted_features,
    VW::details::generate_interactions_object_cache& cache)  // default value removed to eliminate
                                                             // ambiguity in old complers
{
  VW::generate_interactions<DataT, WeightOrIndexT, FuncT, WeightsT>(
      interactions, permutations, ec, dat, weights, num_interacted_features, cache);
}

// iterate through all namespaces and quadratic&cubic features, callback function FuncT(some_data_R, feature_value_x,
// WeightOrIndexT) where WeightOrIndexT is EITHER float& feature_weight OR uint64_t feature_index
template <class DataT, class WeightOrIndexT, void (*FuncT)(DataT&, VW::feature_value, WeightOrIndexT), class WeightsT>
VW_DEPRECATED("Moved to VW namespace")
inline void foreach_feature(WeightsT& weights, bool ignore_some_linear,
    std::array<bool, VW::NUM_NAMESPACES>& ignore_linear,
    const std::vector<std::vector<VW::namespace_index>>& interactions, bool permutations, VW::example_predict& ec,
    DataT& dat, size_t& num_interacted_features, VW::details::generate_interactions_object_cache& cache)
{
  VW::foreach_feature<DataT, WeightOrIndexT, FuncT, WeightsT>(
      weights, ignore_some_linear, ignore_linear, interactions, permutations, ec, dat, num_interacted_features, cache);
}

template <class DataT, class WeightOrIndexT, void (*FuncT)(DataT&, VW::feature_value, WeightOrIndexT), class WeightsT>
VW_DEPRECATED("Moved to VW namespace")
inline void foreach_feature(WeightsT& weights, bool ignore_some_linear,
    std::array<bool, VW::NUM_NAMESPACES>& ignore_linear,
    const std::vector<std::vector<VW::namespace_index>>& interactions, bool permutations, VW::example_predict& ec,
    DataT& dat, VW::details::generate_interactions_object_cache& cache)
{
  VW::foreach_feature<DataT, WeightOrIndexT, FuncT, WeightsT>(
      weights, ignore_some_linear, ignore_linear, interactions, permutations, ec, dat, cache);
}

template <class WeightsT>
VW_DEPRECATED("Moved to VW namespace")
inline VW::feature_value
    inline_predict(WeightsT& weights, bool ignore_some_linear, std::array<bool, VW::NUM_NAMESPACES>& ignore_linear,
        const std::vector<std::vector<VW::namespace_index>>& interactions, bool permutations, VW::example_predict& ec,
        VW::details::generate_interactions_object_cache& cache, VW::feature_value initial = 0.f)
{
  return VW::inline_predict(weights, ignore_some_linear, ignore_linear, interactions, permutations, ec, cache, initial);
}

template <class WeightsT>
VW_DEPRECATED("Moved to VW namespace")
inline VW::feature_value
    inline_predict(WeightsT& weights, bool ignore_some_linear, std::array<bool, VW::NUM_NAMESPACES>& ignore_linear,
        const std::vector<std::vector<VW::namespace_index>>& interactions, bool permutations, VW::example_predict& ec,
        size_t& num_interacted_features, VW::details::generate_interactions_object_cache& cache,
        VW::feature_value initial = 0.f)
{
  return VW::inline_predict(weights, ignore_some_linear, ignore_linear, interactions, permutations, ec,
      num_interacted_features, cache, initial);
}
}  // namespace GD