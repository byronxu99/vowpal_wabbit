// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.
#include "vw/core/example.h"

#include "vw/cache_parser/parse_example_cache.h"
#include "vw/core/cb_continuous_label.h"
#include "vw/core/interactions.h"
#include "vw/core/model_utils.h"
#include "vw/core/reductions/gd.h"
#include "vw/core/scope_exit.h"
#include "vw/core/simple_label_parser.h"
#include "vw/core/text_utils.h"
#include "vw/core/vw.h"

#include <algorithm>
#include <climits>
#include <cstdint>

void VW::swap_prediction(VW::polyprediction& a, VW::polyprediction& b, VW::prediction_type_t prediction_type)
{
  switch (prediction_type)
  {
    case VW::prediction_type_t::SCALAR:
      std::swap(b.scalar, a.scalar);
      break;
    case VW::prediction_type_t::SCALARS:
      std::swap(b.scalars, a.scalars);
      break;
    case VW::prediction_type_t::ACTION_SCORES:
      std::swap(b.a_s, a.a_s);
      break;
    case VW::prediction_type_t::PDF:
      std::swap(b.pdf, a.pdf);
      break;
    case VW::prediction_type_t::ACTION_PROBS:
      std::swap(b.a_s, a.a_s);
      break;
    case VW::prediction_type_t::MULTICLASS:
      std::swap(b.multiclass, a.multiclass);
      break;
    case VW::prediction_type_t::MULTILABELS:
      std::swap(b.multilabels, a.multilabels);
      break;
    case VW::prediction_type_t::PROB:
      std::swap(b.prob, a.prob);
      break;
    case VW::prediction_type_t::MULTICLASS_PROBS:
      std::swap(b.scalars, a.scalars);
      break;
    case VW::prediction_type_t::DECISION_PROBS:
      std::swap(b.decision_scores, a.decision_scores);
      break;
    case VW::prediction_type_t::ACTION_PDF_VALUE:
      std::swap(b.pdf_value, a.pdf_value);
      break;
    case VW::prediction_type_t::ACTIVE_MULTICLASS:
      std::swap(b.active_multiclass, a.active_multiclass);
      break;
    case VW::prediction_type_t::NOPRED:
      // Noop
      break;
  }
}

float calculate_total_sum_features_squared(bool permutations, VW::example& ec)
{
  float sum_features_squared = 0.f;
  for (auto ns : ec) { sum_features_squared += ec[ns].sum_feat_sq; }

  float calculated_sum_features_squared =
      VW::eval_sum_ft_squared_of_generated_ft(permutations, *ec.interactions, ec.feature_space());
  sum_features_squared += calculated_sum_features_squared;
  return sum_features_squared;
}

VW::example::~example()
{
  if (passthrough)
  {
    delete passthrough;
    passthrough = nullptr;
  }
}

float VW::example::get_total_sum_feat_sq()
{
  if (!_total_sum_feat_sq_calculated)
  {
    total_sum_feat_sq = calculate_total_sum_features_squared(_use_permutations, *this);
    _total_sum_feat_sq_calculated = true;
  }
  return total_sum_feat_sq;
}

float collision_cleanup(VW::features& fs)
{
  // Input must be sorted.
  assert(std::is_sorted(fs.indices.begin(), fs.indices.end()));

  // This loops over the sequence of feature values and their indexes
  // when an index is repeated this combines them by adding their values.
  // This assumes that fs is sorted (which is the case in `flatten_sort_example`).

  float sum_sq = 0.f;
  if (!fs.empty())
  {
    VW::features::iterator p1 = fs.begin();
    uint64_t last_index = p1.index();

    for (VW::features::iterator p2 = (fs.begin() + 1); p2 != fs.end(); ++p2)
    {
      if (last_index == p2.index()) { p1.value() += p2.value(); }
      else
      {
        sum_sq += p1.value() * p1.value();
        ++p1;
        p1.value() = p2.value();
        p1.index() = p2.index();
        last_index = p2.index();
      }
    }

    sum_sq += p1.value() * p1.value();
    ++p1;

    fs.truncate_to(p1, 0);
    fs.sum_feat_sq = sum_sq;
  }

  return sum_sq;
}

class full_features_and_source
{
public:
  VW::features fs;
};

void vec_ffs_store(full_features_and_source& p, float fx, uint64_t fi) { p.fs.push_back(fx, fi); }

namespace VW
{
void flatten_features(VW::workspace& all, example& ec, features& fs)
{
  fs.clear();
  full_features_and_source ffs;
  ffs.fs = std::move(fs);

  // We want to call vec_ffs_store() with feature indices, not weight indices
  // Set feature index scale to 1 and offset to 0
  auto restore_guard = ec.stash_scale_offset();
  ec.ft_index_scale = 1;
  ec.ft_index_offset = 0;
  VW::foreach_feature<full_features_and_source, uint64_t, vec_ffs_store>(all, ec, ffs);

  ffs.fs.sort(all.runtime_state.parse_mask);
  ffs.fs.sum_feat_sq = collision_cleanup(ffs.fs);
  fs = std::move(ffs.fs);
}

void return_multiple_example(VW::workspace& all, VW::multi_ex& examples)
{
  for (auto ec : examples) { details::clean_example(all, *ec); }
  examples.clear();
}
namespace details
{
void clean_example(VW::workspace& all, example& ec)
{
  VW::empty_example(all, ec);
  all.parser_runtime.example_parser->example_pool.return_object(&ec);
}
void truncate_example_namespace(VW::example& ec, VW::namespace_index ns, const features& fs)
{
  // print_update is called after this del_example_namespace,
  // so we need to keep the ec.num_features correct,
  // so shared features are included in the reported number of "current features"
  // ec.num_features -= numf;
  auto& del_target = ec[ns];
  assert(del_target.size() >= fs.size());
  if (del_target.size() == fs.size())
  {
    // remove the entire feature group
    ec.delete_namespace(ns);
  }
  else
  {
    // remove only the features in fs from the feature group
    del_target.truncate_to(del_target.size() - fs.size(), fs.sum_feat_sq);
  }
  ec.reset_total_sum_feat_sq();
  ec.num_features -= fs.size();
}

void append_example_namespace(VW::example& ec, VW::namespace_index ns, const features& fs)
{
  // operator[] creates a new empty features object if it doesn't exist
  features& add_fs = ec[ns];
  add_fs.concat(fs);
  ec.reset_total_sum_feat_sq();
  ec.num_features += fs.size();
}

void append_example_namespaces_from_example(VW::example& target, const VW::example& source)
{
  for (VW::namespace_index ns : source)
  {
    if (ns == VW::details::CONSTANT_NAMESPACE) { continue; }
    append_example_namespace(target, ns, source[ns]);
  }
}

void truncate_example_namespaces_from_example(VW::example& target, const VW::example& source)
{
  if (source.empty())
  {  // making sure we can deal with empty shared example
    return;
  }
  for (VW::namespace_index ns : source)
  {
    if (ns == VW::details::CONSTANT_NAMESPACE) { continue; }
    truncate_example_namespace(target, ns, source[ns]);
  }
}
}  // namespace details
}  // namespace VW

namespace VW
{
std::string to_string(const v_array<float>& scalars, int decimal_precision)
{
  std::stringstream ss;
  std::string delim;
  for (float f : scalars)
  {
    ss << delim << VW::fmt_float(f, decimal_precision);
    delim = ",";
  }
  return ss.str();
}
}  // namespace VW
