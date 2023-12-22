// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.

#include "vw/core/reductions/mf.h"

#include "vw/config/options.h"
#include "vw/core/learner.h"
#include "vw/core/numeric_casts.h"
#include "vw/core/reductions/gd.h"
#include "vw/core/scope_exit.h"
#include "vw/core/setup_base.h"

using namespace VW::LEARNER;
using namespace VW::config;

namespace
{
class mf
{
public:
  size_t rank = 0;

  // array to cache w*x, (l^k * x_l) and (r^k * x_r)
  // [ w*(1,x_l,x_r) , l^1*x_l, r^1*x_r, l^2*x_l, r^2*x_2, ... ]
  VW::v_array<float> sub_predictions;

  // array for temp storage of features
  VW::features temp_features;

  VW::workspace* all = nullptr;  // for pairs? and finalize
};

template <bool cache_sub_predictions>
void predict(mf& data, learner& base, VW::example& ec)
{
  float prediction = 0;
  if (cache_sub_predictions) { data.sub_predictions.resize(2 * data.rank + 1); }

  // predict from linear terms
  base.predict(ec);

  // store linear prediction
  if (cache_sub_predictions) { data.sub_predictions[0] = ec.partial_prediction; }
  prediction += ec.partial_prediction;

  // stash data and clear example
  auto restore_features_guard = ec.stash_features();
  auto saved_features = ec.feature_space();
  auto restore_interactions_guard = ec.stash_interactions();
  auto saved_interactions = *ec.interactions;
  ec.delete_all_namespaces();
  ec.interactions->clear();

  // add interaction terms to prediction
  for (auto& i : saved_interactions)
  {
    auto left_ns = i[0];
    auto right_ns = i[1];

    if (ec[left_ns].size() > 0 && ec[right_ns].size() > 0)
    {
      for (size_t k = 1; k <= data.rank; k++)
      {
        // set example to left namespace only
        ec.delete_all_namespaces();
        ec[left_ns] = saved_features[left_ns];

        // compute l^k * x_l using base learner
        base.predict(ec, k);
        float x_dot_l = ec.partial_prediction;
        if (cache_sub_predictions) { data.sub_predictions[2 * k - 1] = x_dot_l; }

        // set example to right namespace only
        ec.delete_all_namespaces();
        ec[right_ns] = saved_features[right_ns];

        // compute r^k * x_r using base learner
        base.predict(ec, k + data.rank);
        float x_dot_r = ec.partial_prediction;
        if (cache_sub_predictions) { data.sub_predictions[2 * k] = x_dot_r; }

        // accumulate prediction
        prediction += (x_dot_l * x_dot_r);
      }
    }
  }

  // finalize prediction
  ec.partial_prediction = prediction;
  ec.pred.scalar = VW::details::finalize_prediction(*data.all->sd, data.all->logger, ec.partial_prediction);
}

void learn(mf& data, learner& base, VW::example& ec)
{
  // predict with current weights
  predict<true>(data, base, ec);
  float predicted = ec.pred.scalar;

  // update linear weights
  base.update(ec);
  ec.pred.scalar = ec.updated_prediction;

  // stash data and clear example
  auto restore_features_guard = ec.stash_features();
  auto saved_features = ec.feature_space();
  auto restore_interactions_guard = ec.stash_interactions();
  auto saved_interactions = *ec.interactions;
  ec.delete_all_namespaces();
  ec.interactions->clear();

  // update interaction terms
  // looping over all pairs of non-empty namespaces
  for (auto& i : saved_interactions)
  {
    int left_ns = i[0];
    int right_ns = i[1];

    if (ec[left_ns].size() > 0 && ec[right_ns].size() > 0)
    {
      // set example to left namespace only
      ec.delete_all_namespaces();
      ec[left_ns] = saved_features[left_ns];

      // store feature values in left namespace
      data.temp_features = ec[left_ns];

      for (size_t k = 1; k <= data.rank; k++)
      {
        auto& fs = ec[left_ns];
        // multiply features in left namespace by r^k * x_r
        for (size_t j = 0; j < fs.size(); ++j) { fs.values[j] *= data.sub_predictions[2 * k]; }

        // update l^k using base learner
        base.update(ec, k);

        // restore left namespace features (undoing multiply)
        fs = data.temp_features;

        // compute new l_k * x_l scaling factors
        // base.predict(ec, k);
        // data.sub_predictions[2*k-1] = ec.partial_prediction;
        // ec.pred.scalar = ec.updated_prediction;
      }

      // set example to right namespace only
      ec.delete_all_namespaces();
      ec[right_ns] = saved_features[right_ns];

      // store feature values for right namespace
      data.temp_features = ec[right_ns];

      for (size_t k = 1; k <= data.rank; k++)
      {
        auto& fs = ec[right_ns];
        // multiply features in right namespace by l^k * x_l
        for (size_t j = 0; j < fs.size(); ++j) { fs.values[j] *= data.sub_predictions[2 * k - 1]; }

        // update r^k using base learner
        base.update(ec, k + data.rank);
        ec.pred.scalar = ec.updated_prediction;

        // restore right namespace features
        fs = data.temp_features;
      }
    }
  }

  // restore original prediction
  ec.pred.scalar = predicted;
}
}  // namespace

std::shared_ptr<VW::LEARNER::learner> VW::reductions::mf_setup(VW::setup_base_i& stack_builder)
{
  options_i& options = *stack_builder.get_options();
  VW::workspace& all = *stack_builder.get_all_pointer();
  auto data = VW::make_unique<mf>();
  uint64_t rank;
  option_group_definition new_options("[Reduction] Matrix Factorization Reduction");
  new_options.add(make_option("new_mf", rank).keep().necessary().help("Rank for reduction-based matrix factorization"));

  if (!options.add_parse_and_check_necessary(new_options)) { return nullptr; }

  data->rank = VW::cast_to_smaller_type<size_t>(rank);
  data->all = &all;
  // store global pairs in local data structure and clear global pairs
  // for eventual calls to base learner
  auto non_pair_count =
      std::count_if(all.feature_tweaks_config.interactions.begin(), all.feature_tweaks_config.interactions.end(),
          [](const std::vector<VW::namespace_index>& interaction) { return interaction.size() != 2; });
  if (non_pair_count > 0) { THROW("can only use pairs with new_mf"); }

  all.initial_weights_config.random_positive_weights = true;

  size_t feature_width = 2 * data->rank + 1;

  auto l = make_reduction_learner(std::move(data), require_singleline(stack_builder.setup_base_learner(feature_width)),
      learn, predict<false>, stack_builder.get_setupfn_name(mf_setup))
               .set_feature_width(feature_width)
               .set_output_prediction_type(VW::prediction_type_t::SCALAR)
               .build();

  return l;
}
