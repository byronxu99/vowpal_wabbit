// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.

#include "vw/core/kskip_ngram_transformer.h"

#include "vw/io/logger.h"

#include <memory>

void add_grams(size_t ngram, size_t skip_gram, VW::features& fs, size_t initial_length, std::vector<size_t>& gram_mask,
    size_t skips)
{
  if (ngram == 0 && gram_mask.back() < initial_length)
  {
    // In this branch, gram_mask has been filled completely

    // Get last index of features that will not exceed the final increment in gram_mask
    // Iterate through all features to last index
    size_t last = initial_length - gram_mask.back();
    for (size_t i = 0; i < last; i++)
    {
      // Generate the new index by hashing the indices in gram_mask
      uint64_t new_index = fs.indices[i];
      for (size_t n = 1; n < gram_mask.size(); n++)
      {
        new_index = new_index * VW::details::QUADRATIC_CONSTANT + fs.indices[i + gram_mask[n]];
      }

      // Add new feature with index we just computed and feature value 1
      fs.add_feature_raw(new_index, 1.f);

      // Add the new feature name to the list of feature names
      if (!fs.audit_info.empty())
      {
        std::string feature_name(fs.audit_info[i].feature_name);
        for (size_t n = 1; n < gram_mask.size(); n++)
        {
          feature_name += std::string("^");
          feature_name += std::string(fs.audit_info[i + gram_mask[n]].feature_name);
        }
        fs.add_audit_string(feature_name);
      }
    }
  }
  if (ngram > 0)
  {
    // Add current value of skips to the gram_mask and generate (n-1)-grams
    gram_mask.push_back(gram_mask.back() + 1 + skips);
    add_grams(ngram - 1, skip_gram, fs, initial_length, gram_mask, 0);
    gram_mask.pop_back();
  }
  if (skip_gram > 0 && ngram > 0)
  {
    // Increment the value of skips and call function again
    add_grams(ngram, skip_gram - 1, fs, initial_length, gram_mask, skips + 1);
  }
}

void compile_gram(const std::vector<std::string>& grams, std::unordered_map<VW::namespace_index, uint32_t>& dest,
    uint32_t& default_dest,
    const std::string& descriptor, bool /*quiet*/, VW::io::logger& logger)
{
  for (const auto& gram : grams)
  {
    if (isdigit(gram[0]) != 0)
    {
      int n = atoi(gram.c_str());
      logger.err_info("Generating {0}-{1} for all namespaces.", n, descriptor);
      default_dest = n;
    }
    else if (gram.size() == 1) { logger.out_error("The namespace index must be specified before the n"); }
    else
    {
      int n = atoi(gram.c_str() + 1);
      dest[static_cast<uint32_t>(static_cast<unsigned char>(*gram.c_str()))] = n;
      logger.err_info("Generating {0}-{1} for {2} namespaces.", n, descriptor, gram[0]);
    }
  }
}

void VW::kskip_ngram_transformer::generate_grams(example* ex)
{
  for (namespace_index index : *ex)
  {
    size_t length = (*ex)[index].size();
    uint32_t ngram_def = ngram_default;
    if (ngram_definition.find(index) != ngram_definition.end()) { ngram_def = ngram_definition[index]; }
    for (size_t n = 1; n < ngram_def; n++)
    {
      gram_mask.clear();
      gram_mask.push_back(0);
      uint32_t skip_def = skip_default;
      if (skip_definition.find(index) != skip_definition.end()) { skip_def = skip_definition[index]; }
      add_grams(n, skip_def, (*ex)[index], length, gram_mask, 0);
    }
  }
}

VW::kskip_ngram_transformer VW::kskip_ngram_transformer::build(
    const std::vector<std::string>& grams, const std::vector<std::string>& skips, bool quiet, VW::io::logger& logger)
{
  kskip_ngram_transformer transformer(grams, skips);

  compile_gram(grams, transformer.ngram_definition, transformer.ngram_default, "grams", quiet, logger);
  compile_gram(skips, transformer.skip_definition, transformer.skip_default, "skips", quiet, logger);
  return transformer;
}

VW::kskip_ngram_transformer::kskip_ngram_transformer(std::vector<std::string> grams, std::vector<std::string> skips)
    : initial_ngram_definitions(std::move(grams)), initial_skip_definitions(std::move(skips))
{ }
