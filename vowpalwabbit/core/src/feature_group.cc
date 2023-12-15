// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.

#include "vw/core/feature_group.h"

#include "vw/core/hash.h"
#include "vw/core/v_array.h"

#include <algorithm>
#include <numeric>
#include <utility>
#include <vector>

void VW::features::clear()
{
  sum_feat_sq = 0.f;
  values.clear();
  indices.clear();
  audit_info.clear();
}

void VW::features::truncate_to(const audit_iterator& pos, float sum_feat_sq_of_removed_section)
{
  truncate_to(std::distance(audit_begin(), pos), sum_feat_sq_of_removed_section);
}

void VW::features::truncate_to(const iterator& pos, float sum_feat_sq_of_removed_section)
{
  truncate_to(std::distance(begin(), pos), sum_feat_sq_of_removed_section);
}

void VW::features::truncate_to(size_t i, float sum_feat_sq_of_removed_section)
{
  assert(i <= size());
  if (i == size()) { return; }
  sum_feat_sq -= sum_feat_sq_of_removed_section;

  values.resize(i);
  if (indices.end() != indices.begin()) { indices.resize(i); }

  if (audit_info.size() > i) { audit_info.erase(audit_info.begin() + i, audit_info.end()); }
}

void VW::features::truncate_to(const audit_iterator& pos) { truncate_to(std::distance(audit_begin(), pos)); }
void VW::features::truncate_to(const iterator& pos) { truncate_to(std::distance(begin(), pos)); }
void VW::features::truncate_to(size_t i)
{
  assert(i <= size());
  float sum_ft_squares_of_removed_chunk = 0.f;
  for (auto idx = i; idx < values.size(); ++idx) { sum_ft_squares_of_removed_chunk += values[idx] * values[i]; }
  truncate_to(i, sum_ft_squares_of_removed_chunk);
}

void VW::features::concat(const features& other)
{
  assert(values.size() == indices.size());
  assert(other.values.size() == other.indices.size());
  // Conditions to check:
  //  - !empty() && audit && other.audit -> push val, idx, audit
  //  - !empty() && audit && !other.audit -> fail
  //  - !empty() && !audit && other.audit -> fail
  //  - !empty() && !audit && !other.audit -> push val, idx
  //  - empty() && other.audit -> push val, idx, audit
  //  - empty() && !other.audit -> push val, idx

  // Cannot merge two feature groups if one has audit info and the other does not.
  assert(!(!empty() && (audit_info.empty() != other.audit_info.empty())));
  sum_feat_sq += other.sum_feat_sq;

  for (size_t i = 0; i < other.size(); ++i)
  {
    values.push_back(other.values[i]);
    indices.push_back(other.indices[i]);
  }

  if (!other.audit_info.empty())
  {
    audit_info.insert(audit_info.end(), other.audit_info.begin(), other.audit_info.end());
  }
}

void VW::features::add_feature_raw(feature_index i, feature_value v)
{
  values.push_back(v);
  indices.push_back(i);
  sum_feat_sq += v * v;
}

void VW::features::add_audit_string(std::string feature_name)
{
  audit_info.emplace_back(namespace_name, namespace_hash, std::move(feature_name));
}

void VW::features::add_audit_string(std::string feature_name, std::string str_value)
{
  audit_info.emplace_back(namespace_name, namespace_hash, std::move(feature_name), std::move(str_value));
}

void VW::features::add_feature(feature_index i, feature_value v, bool audit)
{
  VW::feature_index index = VW::hash_feature(i, namespace_hash);
  add_feature_raw(index, v * namespace_value);
  if (audit) { add_audit_string(std::to_string(i)); }
}

void VW::features::add_feature(feature_index i, VW::string_view str_value, bool audit)
{
  VW::feature_index index = VW::chain_hash_feature(i, str_value, namespace_hash);
  add_feature_raw(index, namespace_value);
  if (audit) { add_audit_string(std::to_string(i), std::string(str_value)); }
}

void VW::features::add_feature(VW::string_view feature_name, feature_value v, bool audit)
{
  VW::feature_index index = VW::hash_feature(feature_name, namespace_hash);
  add_feature_raw(index, v * namespace_value);
  if (audit) { add_audit_string(std::string(feature_name)); }
}

void VW::features::add_feature(VW::string_view feature_name, VW::string_view str_value, bool audit)
{
  VW::feature_index index = VW::chain_hash_feature(feature_name, str_value, namespace_hash);
  add_feature_raw(index, namespace_value);
  if (audit) { add_audit_string(std::string(feature_name), std::string(str_value)); }
}

// https://stackoverflow.com/questions/17074324/how-can-i-sort-two-vectors-in-the-same-way-with-criteria-that-uses-only-one-of
template <typename IndexVec, typename ValVec, typename Compare>
std::vector<std::size_t> sort_permutation(const IndexVec& index_vec, const ValVec& value_vec, const Compare& compare)
{
  assert(index_vec.size() == value_vec.size());
  std::vector<std::size_t> dest_index_vec(index_vec.size());
  std::iota(dest_index_vec.begin(), dest_index_vec.end(), 0);
  std::sort(dest_index_vec.begin(), dest_index_vec.end(),
      [&](std::size_t i, std::size_t j) { return compare(index_vec[i], index_vec[j], value_vec[i], value_vec[j]); });
  return dest_index_vec;
}

template <typename VecT, typename... Rest>
size_t size_of_first_vec(const VecT& vec, Rest&... /*rest*/)
{
  return vec.size();
}

template <typename VecT>
void do_swap_for_all(size_t pos1, size_t pos2, VecT& vec)
{
  std::swap(vec[pos1], vec[pos2]);
}

template <typename VecT, typename... Rest>
void do_swap_for_all(size_t pos1, size_t pos2, VecT& vec, Rest&... rest)
{
  std::swap(vec[pos1], vec[pos2]);
  do_swap_for_all(pos1, pos2, rest...);
}

template <typename... VecTs>
void apply_permutation_in_place(const std::vector<std::size_t>& dest_index_vec, VecTs&... vecs)
{
  const auto size = size_of_first_vec(vecs...);
  assert(dest_index_vec.size() == size);
  std::vector<bool> done(size);
  for (std::size_t i = 0; i < size; ++i)
  {
    if (done[i]) { continue; }
    done[i] = true;
    std::size_t prev_j = i;
    std::size_t j = dest_index_vec[i];
    while (i != j)
    {
      do_swap_for_all(prev_j, j, vecs...);
      done[j] = true;
      prev_j = j;
      j = dest_index_vec[j];
    }
  }
}

void push_many(std::vector<std::pair<bool, uint64_t>>& vec, size_t num, bool is_valid, uint64_t hash)
{
  for (auto i = std::size_t{0}; i < num; ++i) { vec.emplace_back(is_valid, hash); }
}

bool VW::features::sort(uint64_t parse_mask)
{
  if (indices.empty()) { return false; }
  // Compared indices are masked even though the saved values are not necessarilly masked.
  const auto comparator = [parse_mask](feature_index index_first, feature_index index_second, feature_value value_first,
                              feature_value value_second)
  {
    const auto masked_index_first = index_first & parse_mask;
    const auto masked_index_second = index_second & parse_mask;
    return (masked_index_first < masked_index_second) ||
        ((masked_index_first == masked_index_second) && (value_first < value_second));
  };
  const auto dest_index_vec = sort_permutation(indices, values, comparator);
  if (!audit_info.empty()) { apply_permutation_in_place(dest_index_vec, values, indices, audit_info); }
  else { apply_permutation_in_place(dest_index_vec, values, indices); }
  return true;
}

float VW::features_dot_product(const features& fs1, const features& fs2)
{
  assert(std::is_sorted(fs1.indices.begin(), fs1.indices.end()));
  assert(std::is_sorted(fs2.indices.begin(), fs2.indices.end()));

  float dotprod = 0;
  if (fs2.indices.empty()) { return 0.f; }

  for (size_t idx1 = 0, idx2 = 0; idx1 < fs1.size() && idx2 < fs2.size(); idx1++)
  {
    uint64_t ec1pos = fs1.indices[idx1];
    uint64_t ec2pos = fs2.indices[idx2];
    if (ec1pos < ec2pos) { continue; }

    while (ec1pos > ec2pos && ++idx2 < fs2.size()) { ec2pos = fs2.indices[idx2]; }

    if (ec1pos == ec2pos)
    {
      dotprod += fs1.values[idx1] * fs2.values[idx2];
      ++idx2;
    }
  }
  return dotprod;
}

VW::scope_exit_guard VW::features::stash_features()
{
  auto values_copy = values;
  auto indices_copy = indices;
  auto audit_info_copy = audit_info;
  return VW::scope_exit_guard([this, values_copy = std::move(values_copy), indices_copy = std::move(indices_copy),
                                 audit_info_copy = std::move(audit_info_copy), sum_feat_sq_copy = sum_feat_sq]() mutable {
    values = std::move(values_copy);
    indices = std::move(indices_copy);
    audit_info = std::move(audit_info_copy);
    sum_feat_sq = sum_feat_sq_copy;
  });
}
