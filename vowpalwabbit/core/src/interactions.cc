// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.

#include "vw/core/interactions.h"

#include "vw/core/vw_math.h"

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <iterator>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

using namespace VW::config;

namespace
{
template <typename FuncT>
void for_each_value(
    const VW::feature_groups_type& feature_spaces, VW::namespace_index term, const FuncT& func)
{
  for (auto value : feature_spaces[term].values) { func(value); }
}

float calc_sum_ft_squared_for_term(
    const VW::feature_groups_type& feature_spaces, VW::namespace_index term)
{
  auto iter = feature_spaces.find(term);
  if (iter == feature_spaces.end()) { return 0.f; }
  return iter->second.sum_feat_sq;
}

template <typename InteractionTermT>
float calculate_count_and_sum_ft_sq_for_permutations(const VW::feature_groups_type& feature_spaces,
    const std::vector<std::vector<InteractionTermT>>& interactions)
{
  float sum_feat_sq_in_inter_outer = 0.;
  for (const auto& interaction : interactions)
  {
    float sum_feat_sq_in_inter = 1.;
    for (auto term : interaction) { sum_feat_sq_in_inter *= calc_sum_ft_squared_for_term(feature_spaces, term); }
    sum_feat_sq_in_inter_outer += sum_feat_sq_in_inter;
  }

  return sum_feat_sq_in_inter_outer;
}

template <typename InteractionTermT>
float calculate_count_and_sum_ft_sq_for_combinations(const VW::feature_groups_type& feature_spaces,
    const std::vector<std::vector<InteractionTermT>>& interactions)
{
  std::vector<float> results;

  float sum_feat_sq_in_inter_outer = 0.;
  for (const auto& inter : interactions)
  {
    float sum_feat_sq_in_inter = 1.;

    for (auto interaction_term_it = inter.begin(); interaction_term_it != inter.end(); ++interaction_term_it)
    {
      if ((interaction_term_it == inter.end() - 1) ||
          (*interaction_term_it != *(interaction_term_it + 1)))  // neighbour namespaces are different
      {
        sum_feat_sq_in_inter *= calc_sum_ft_squared_for_term(feature_spaces, *interaction_term_it);
        if (sum_feat_sq_in_inter == 0.f) { break; }
      }
      else
      {
        // already compared ns == ns+1
        size_t order_of_inter = 2;
        for (auto ns_end = interaction_term_it + 2; ns_end < inter.end(); ++ns_end)
        {
          if (*interaction_term_it == *ns_end) { ++order_of_inter; }
          else
          {
            // Since the list is sorted, as soon as they don't match we can exit.
            break;
          }
        }
        results.resize(order_of_inter);
        std::fill(results.begin(), results.end(), 0.f);

        for_each_value(feature_spaces, *interaction_term_it,
            [&](float value)
            {
              const float x = value * value;
              results[0] += x;
              for (size_t j = 1; j < order_of_inter; ++j) { results[j] += results[j - 1] * x; }
            });

        sum_feat_sq_in_inter *= results[order_of_inter - 1];
        if (sum_feat_sq_in_inter == 0.f) { break; }

        interaction_term_it += order_of_inter - 1;  // jump over whole block
      }
    }

    sum_feat_sq_in_inter_outer += sum_feat_sq_in_inter;
  }

  return sum_feat_sq_in_inter_outer;
}
}  // namespace

// returns number of new features that will be generated for example and sum of their squared values
float VW::eval_sum_ft_squared_of_generated_ft(bool permutations,
    const VW::interaction_spec_type& interactions,
    const VW::feature_groups_type& feature_spaces)
{
  float sum_ft_sq = 0.f;

  if (permutations) { sum_ft_sq += calculate_count_and_sum_ft_sq_for_permutations(feature_spaces, interactions); }
  else { sum_ft_sq += calculate_count_and_sum_ft_sq_for_combinations(feature_spaces, interactions); }
  return sum_ft_sq;
}

bool VW::details::sort_interactions_comparator(
    const std::vector<VW::namespace_index>& a, const std::vector<VW::namespace_index>& b)
{
  if (a.size() != b.size()) { return a.size() < b.size(); }
  return a < b;
}

VW::interaction_spec_type VW::details::expand_quadratics_wildcard_interactions(
    bool leave_duplicate_interactions, const std::unordered_set<VW::namespace_index>& new_example_indices)
{
  // C++ doesn't support unordered_set of vectors
  std::set<std::vector<VW::namespace_index>> interactions;

  for (auto it = new_example_indices.begin(); it != new_example_indices.end(); ++it)
  {
    auto idx1 = *it;
    interactions.insert({idx1, idx1});

    for (auto jt = it; jt != new_example_indices.end(); ++jt)
    {
      auto idx2 = *jt;
      interactions.insert({idx1, idx2});
      interactions.insert({idx2, idx2});
      if (leave_duplicate_interactions) { interactions.insert({idx2, idx1}); }
    }
  }
  return VW::interaction_spec_type(interactions.begin(), interactions.end());
}
