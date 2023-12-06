// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.

#pragma once

#include "vw/common/fnv_hash.h"
#include "vw/common/future_compat.h"
#include "vw/common/vw_exception.h"
#include "vw/core/constant.h"
#include "vw/core/example_predict.h"
#include "vw/core/feature_group.h"
#include "vw/core/interaction_generation_state.h"
#include "vw/core/object_pool.h"

#include <cstdint>
#include <stack>
#include <string>
#include <utility>
#include <vector>

namespace VW
{
namespace details
{
const static VW::audit_strings EMPTY_AUDIT_STRINGS;

/*
 * By default include interactions of feature with itself.
 * This approach produces slightly more interactions but it's safer
 * for some cases, as discussed in issues/698
 * Previous behaviour was: include interactions of feature with itself only if its value != value^2.
 *
 */

// 3 template functions to pass FuncT() proper argument (weight index in regressor, or its coefficient)
template <class DataT, void (*FuncT)(DataT&, const VW::feature_value, VW::feature_value&), class WeightsT>
inline void call_func_t(DataT& dat, WeightsT& weights, const VW::feature_value ft_value, const VW::feature_index wt_idx)
{
  FuncT(dat, ft_value, weights[wt_idx]);
}

template <class DataT, void (*FuncT)(DataT&, const VW::feature_value, VW::feature_value), class WeightsT>
inline void call_func_t(
    DataT& dat, const WeightsT& weights, const VW::feature_value ft_value, const VW::feature_index wt_idx)
{
  FuncT(dat, ft_value, weights.get(static_cast<size_t>(wt_idx)));
}

template <class DataT, void (*FuncT)(DataT&, VW::feature_value, VW::feature_index), class WeightsT>
inline void call_func_t(
    DataT& dat, WeightsT& /*weights*/, const VW::feature_value ft_value, const VW::feature_index wt_idx)
{
  FuncT(dat, ft_value, wt_idx);
}

inline bool term_is_empty(VW::namespace_index term, const std::array<VW::features, VW::NUM_NAMESPACES>& feature_groups)
{
  return feature_groups[term].empty();
}

inline bool has_empty_interaction_quadratic(const std::array<VW::features, VW::NUM_NAMESPACES>& feature_groups,
    const std::vector<VW::namespace_index>& namespace_indexes)
{
  assert(namespace_indexes.size() == 2);
  return term_is_empty(namespace_indexes[0], feature_groups) || term_is_empty(namespace_indexes[1], feature_groups);
}

inline bool has_empty_interaction_cubic(const std::array<VW::features, VW::NUM_NAMESPACES>& feature_groups,
    const std::vector<VW::namespace_index>& namespace_indexes)
{
  assert(namespace_indexes.size() == 3);
  return term_is_empty(namespace_indexes[0], feature_groups) || term_is_empty(namespace_indexes[1], feature_groups) ||
      term_is_empty(namespace_indexes[2], feature_groups);
  ;
}

inline bool has_empty_interaction(const std::array<VW::features, VW::NUM_NAMESPACES>& feature_groups,
    const std::vector<VW::namespace_index>& namespace_indexes)
{
  return std::any_of(namespace_indexes.begin(), namespace_indexes.end(),
      [&](VW::namespace_index idx) { return term_is_empty(idx, feature_groups); });
}

// The inline function below may be adjusted to change the way
// synthetic (interaction) features' values are calculated, e.g.,
// fabs(value1-value2) or even value1>value2?1.0:-1.0
// Beware - its result must be non-zero.
constexpr inline VW::feature_value interaction_value(VW::feature_value value1, VW::feature_value value2)
{
  return value1 * value2;
}

// This function maps feature index (from example object) to weight index (in regressor)
constexpr inline VW::feature_index feature_to_weight_index(
    VW::feature_index ft_idx, VW::feature_index ft_scale, VW::feature_index ft_offset)
{
  return ft_idx * ft_scale + ft_offset;
}

// uncomment line below to disable usage of inner 'for' loops for pair and triple interactions
// end switch to usage of non-recursive feature generation algorithm for interactions of any length

// #define GEN_INTER_LOOP

std::tuple<VW::details::features_range_t, VW::details::features_range_t> inline generate_quadratic_char_combination(
    const std::array<VW::features, VW::NUM_NAMESPACES>& feature_groups, VW::namespace_index ns_idx1,
    VW::namespace_index ns_idx2)
{
  return {std::make_tuple(std::make_pair(feature_groups[ns_idx1].audit_begin(), feature_groups[ns_idx1].audit_end()),
      std::make_pair(feature_groups[ns_idx2].audit_begin(), feature_groups[ns_idx2].audit_end()))};
}

std::tuple<VW::details::features_range_t, VW::details::features_range_t,
    VW::details::features_range_t> inline generate_cubic_char_combination(const std::array<VW::features,
                                                                              VW::NUM_NAMESPACES>& feature_groups,
    VW::namespace_index ns_idx1, VW::namespace_index ns_idx2, VW::namespace_index ns_idx3)
{
  return {std::make_tuple(std::make_pair(feature_groups[ns_idx1].audit_begin(), feature_groups[ns_idx1].audit_end()),
      std::make_pair(feature_groups[ns_idx2].audit_begin(), feature_groups[ns_idx2].audit_end()),
      std::make_pair(feature_groups[ns_idx3].audit_begin(), feature_groups[ns_idx3].audit_end()))};
}

std::vector<VW::details::features_range_t> inline generate_generic_char_combination(
    const std::array<VW::features, VW::NUM_NAMESPACES>& feature_groups, const std::vector<VW::namespace_index>& terms)
{
  std::vector<VW::details::features_range_t> inter;
  inter.reserve(terms.size());
  for (const auto& term : terms)
  {
    inter.emplace_back(feature_groups[term].audit_begin(), feature_groups[term].audit_end());
  }
  return inter;
}

template <class DataT, class WeightOrIndexT, void (*FuncT)(DataT&, VW::feature_value, WeightOrIndexT), bool audit,
    void (*audit_func)(DataT&, const VW::audit_strings*), class WeightsT>
void inner_kernel(DataT& dat, VW::features::const_audit_iterator& begin, VW::features::const_audit_iterator& end,
    const VW::feature_index scale, const VW::feature_index offset, WeightsT& weights, VW::feature_value ft_value,
    VW::fnv_hasher partial_hash, const uint32_t hash_bits)
{
  if (audit)
  {
    for (; begin != end; ++begin)
    {
      audit_func(dat, begin.audit() == nullptr ? &EMPTY_AUDIT_STRINGS : begin.audit());
      VW::feature_index interaction_hash = partial_hash.hash(begin.index()).get_truncated_hash(hash_bits);
      call_func_t<DataT, FuncT>(dat, weights, interaction_value(ft_value, begin.value()),
          feature_to_weight_index(interaction_hash, scale, offset));
      audit_func(dat, nullptr);
    }
  }
  else
  {
    for (; begin != end; ++begin)
    {
      VW::feature_index interaction_hash = partial_hash.hash(begin.index()).get_truncated_hash(hash_bits);
      call_func_t<DataT, FuncT>(dat, weights, interaction_value(ft_value, begin.value()),
          feature_to_weight_index(interaction_hash, scale, offset));
    }
  }
}

template <bool Audit, typename KernelFuncT, typename AuditFuncT>
size_t process_quadratic_interaction(
    const std::tuple<VW::details::features_range_t, VW::details::features_range_t>& range, bool permutations,
    const KernelFuncT& kernel_func, const AuditFuncT& audit_func)
{
  size_t num_features = 0;
  auto first_begin = std::get<0>(range).first;
  const auto& first_end = std::get<0>(range).second;
  const auto& second_begin = std::get<1>(range).first;
  auto& second_end = std::get<1>(range).second;

  const bool same_namespace = (!permutations && (first_begin == second_begin));
  size_t i = 0;
  for (; first_begin != first_end; ++first_begin)
  {
    auto interaction_hasher = VW::fnv_hasher().hash(first_begin.index());
    if (Audit) { audit_func(first_begin.audit() != nullptr ? first_begin.audit() : &EMPTY_AUDIT_STRINGS); }
    // next index differs for permutations and simple combinations
    auto begin = second_begin;
    if (same_namespace) { begin += i; }
    num_features += std::distance(begin, second_end);
    kernel_func(begin, second_end, first_begin.value(), interaction_hasher);
    if (Audit) { audit_func(nullptr); }
    i++;
  }
  return num_features;
}

template <bool Audit, typename KernelFuncT, typename AuditFuncT>
size_t process_cubic_interaction(
    const std::tuple<VW::details::features_range_t, VW::details::features_range_t, VW::details::features_range_t>&
        range,
    bool permutations, const KernelFuncT& kernel_func, const AuditFuncT& audit_func)
{
  size_t num_features = 0;
  auto first_begin = std::get<0>(range).first;
  const auto& first_end = std::get<0>(range).second;
  const auto& second_begin = std::get<1>(range).first;
  auto second_end = std::get<1>(range).second;
  const auto& third_begin = std::get<2>(range).first;
  auto& third_end = std::get<2>(range).second;

  // don't compare 1 and 3 as interaction is sorted
  const bool same_namespace1 = (!permutations && (first_begin == second_begin));
  const bool same_namespace2 = (!permutations && (second_begin == third_begin));

  size_t i = 0;
  for (; first_begin != first_end; ++first_begin)
  {
    if (Audit) { audit_func(first_begin.audit() != nullptr ? first_begin.audit() : &EMPTY_AUDIT_STRINGS); }

    const auto interaction_hasher_1 = VW::fnv_hasher().hash(first_begin.index());
    const VW::feature_value interaction_value_1 = first_begin.value();
    size_t j = 0;
    if (same_namespace1)  // next index differs for permutations and simple combinations
    {
      j = i;
    }

    for (auto inner_second_begin = second_begin + j; inner_second_begin != second_end; ++inner_second_begin)
    {
      // f3 x k*(f2 x k*f1)
      if (Audit)
      {
        audit_func(inner_second_begin.audit() != nullptr ? inner_second_begin.audit() : &EMPTY_AUDIT_STRINGS);
      }
      auto interaction_hasher_2 = interaction_hasher_1.hash(inner_second_begin.index());
      VW::feature_value interaction_value_2 = interaction_value(interaction_value_1, inner_second_begin.value());

      auto begin = third_begin;
      // next index differs for permutations and simple combinations
      if (same_namespace2) { begin += j; }
      num_features += std::distance(begin, third_end);
      kernel_func(begin, third_end, interaction_value_2, interaction_hasher_2);
      if (Audit) { audit_func(nullptr); }
      j++;
    }  // end for (snd)
    if (Audit) { audit_func(nullptr); }
    i++;
  }
  return num_features;
}

template <bool Audit, typename KernelFuncT, typename AuditFuncT>
size_t process_generic_interaction(const std::vector<VW::details::features_range_t>& range, bool permutations,
    const KernelFuncT& kernel_func, const AuditFuncT& audit_func,
    std::vector<VW::details::feature_gen_data>& state_data)
{
  size_t num_features = 0;
  state_data.clear();
  state_data.reserve(range.size());
  // preparing state data
  for (const auto& r : range) { state_data.emplace_back(r.first, r.second); }

  if (!permutations)  // adjust state_data for simple combinations
  {                   // if permutations mode is disabled then namespaces in ns are already sorted and thus grouped
    // (in fact, currently they are sorted even for enabled permutations mode)
    // let's go throw the list and calculate number of features to skip in namespaces which
    // repeated more than once to generate only simple combinations of features

    for (auto fgd = state_data.data() + (state_data.size() - 1); fgd > state_data.data(); --fgd)
    {
      const auto prev = fgd - 1;
      fgd->self_interaction =
          (fgd->current_it == prev->current_it);  // state_data.begin().self_interaction is always false
    }
  }  // end of state_data adjustment

  const auto gen_data_head = state_data.data();                            // always equal to first ns
  const auto gen_data_last = state_data.data() + (state_data.size() - 1);  // always equal to last ns

  VW::details::feature_gen_data* cur_data = gen_data_head;

  // generic feature generation cycle for interactions of any length
  bool do_it = true;
  while (do_it)
  {
    if (cur_data < gen_data_last)  // can go further through the list of namespaces in interaction
    {
      VW::details::feature_gen_data* next_data = cur_data + 1;

      if (next_data->self_interaction)
      {  // if next namespace is same, we should start with loop_idx + 1 to avoid feature interaction with itself
        // unless feature has value x and x != x*x. E.g. x != 0 and x != 1. Features with x == 0 are already
        // filtered out in parse_args.cc::maybeFeature().
        const auto current_offset = cur_data->current_it - cur_data->begin_it;
        next_data->current_it = next_data->begin_it;
        next_data->current_it += current_offset;
      }
      else { next_data->current_it = next_data->begin_it; }

      if (Audit) { audit_func((*cur_data->current_it).audit()); }

      if (cur_data == gen_data_head)  // first namespace
      {
        next_data->hasher = VW::fnv_hasher().hash((*cur_data->current_it).index());
        next_data->x = (*cur_data->current_it).value();  // data->x == 1.
      }
      else  // namespace after the first
      {
        next_data->hasher = cur_data->hasher.hash((*cur_data->current_it).index());
        next_data->x = interaction_value((*cur_data->current_it).value(), cur_data->x);
      }
      ++cur_data;
    }
    else
    {
      // last namespace - iterate its features and go back
      // start value is not a constant in this case
      size_t start_i = 0;
      if (!permutations) { start_i = gen_data_last->current_it - gen_data_last->begin_it; }

      VW::feature_value interaction_value = gen_data_last->x;
      auto interaction_hasher = gen_data_last->hasher;

      auto begin = cur_data->begin_it + start_i;
      num_features += (cur_data->end_it - begin);
      kernel_func(begin, cur_data->end_it, interaction_value, interaction_hasher);
      // trying to go back increasing loop_idx of each namespace by the way
      bool go_further;
      do {
        --cur_data;
        ++cur_data->current_it;
        go_further = cur_data->current_it == cur_data->end_it;
        if (Audit) { audit_func(nullptr); }
      } while (go_further && cur_data != gen_data_head);

      do_it = !(cur_data == gen_data_head && go_further);
      // if do_it==false - we've reached 0 namespace but its 'cur_data.loop_idx > cur_data.loop_end' -> exit the
      // while loop
    }  // if last namespace
  }    // while do_it

  return num_features;
}
}  // namespace details

// this templated function generates new features for given example and set of interactions
// and passes each of them to given function FuncT()
// it must be in header file to avoid compilation problems
template <class DataT, class WeightOrIndexT, void (*FuncT)(DataT&, VW::feature_value, WeightOrIndexT), bool audit,
    void (*audit_func)(DataT&, const VW::audit_strings*),
    class WeightsT>  // nullptr func can't be used as template param in old compilers
inline void generate_interactions(const std::vector<std::vector<VW::namespace_index>>& interactions, bool permutations,
    VW::example_predict& ec, DataT& dat, WeightsT& weights, size_t& num_features,
    VW::details::generate_interactions_object_cache&
        cache)  // default value removed to eliminate ambiguity in old complers
{
  num_features = 0;
  // often used values
  const auto inner_kernel_func = [&](VW::features::const_audit_iterator begin, VW::features::const_audit_iterator end,
                                     VW::feature_value value, VW::fnv_hasher hasher)
  {
    details::inner_kernel<DataT, WeightOrIndexT, FuncT, audit, audit_func>(
        dat, begin, end, ec.ft_index_scale, ec.ft_index_offset, weights, value, hasher, weights.feature_hash_bits());
  };

  const auto depth_audit_func = [&](const VW::audit_strings* audit_str) { audit_func(dat, audit_str); };

  // current list of namespaces to interact.
  for (const auto& ns : interactions)
  {
#ifndef GEN_INTER_LOOP

    // unless GEN_INTER_LOOP is defined we use nested 'for' loops for interactions length 2 (pairs) and 3 (triples)
    // and generic non-recursive algorithm for all other cases.
    // nested 'for' loops approach is faster, but can't be used for interaction of any length.
    const size_t len = ns.size();
    if (len == 2)  // special case of pairs
    {
      // Skip over any interaction with an empty namespace.
      if (details::has_empty_interaction_quadratic(ec.feature_space, ns)) { continue; }
      num_features += details::process_quadratic_interaction<audit>(
          details::generate_quadratic_char_combination(ec.feature_space, ns[0], ns[1]), permutations, inner_kernel_func,
          depth_audit_func);
    }
    else if (len == 3)  // special case for triples
    {
      // Skip over any interaction with an empty namespace.
      if (details::has_empty_interaction_cubic(ec.feature_space, ns)) { continue; }
      num_features += details::process_cubic_interaction<audit>(
          details::generate_cubic_char_combination(ec.feature_space, ns[0], ns[1], ns[2]), permutations,
          inner_kernel_func, depth_audit_func);
    }
    else  // generic case: quatriples, etc.
#endif
    {
      // Skip over any interaction with an empty namespace.
      if (details::has_empty_interaction(ec.feature_space, ns)) { continue; }
      num_features +=
          details::process_generic_interaction<audit>(details::generate_generic_char_combination(ec.feature_space, ns),
              permutations, inner_kernel_func, depth_audit_func, cache.state_data);
    }
  }
}  // foreach interaction in all.feature_tweaks_config.interactions

}  // namespace VW

namespace INTERACTIONS  // NOLINT
{

template <class DataT, class WeightOrIndexT, void (*FuncT)(DataT&, VW::feature_value, WeightOrIndexT), bool audit,
    void (*audit_func)(DataT&, const VW::audit_strings*),
    class WeightsT>  // nullptr func can't be used as template param in old compilers
VW_DEPRECATED("Moved into VW namespace") inline void generate_interactions(
    const std::vector<std::vector<VW::namespace_index>>& interactions, bool permutations, VW::example_predict& ec,
    DataT& dat, WeightsT& weights, size_t& num_features,
    VW::details::generate_interactions_object_cache&
        cache)  // default value removed to eliminate ambiguity in old complers
{
  VW::generate_interactions<DataT, WeightOrIndexT, FuncT, audit, audit_func, WeightsT>(
      interactions, permutations, ec, dat, weights, num_features, cache);
}
}  // namespace INTERACTIONS