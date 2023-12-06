// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.

#pragma once

#include "vw/common/future_compat.h"
#include "vw/core/generic_range.h"
#include "vw/core/v_array.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace VW
{

using feature_value = float;
using feature_index = uint64_t;
using namespace_index = unsigned char;

class features;

class audit_strings
{
public:
  std::string ns;
  std::string name;

  // This is only set if chain hashing is in use.
  std::string str_value;

  audit_strings() = default;
  audit_strings(std::string ns, std::string name) : ns(std::move(ns)), name(std::move(name)) {}
  audit_strings(std::string ns, std::string name, std::string str_value)
      : ns(std::move(ns)), name(std::move(name)), str_value(std::move(str_value))
  {
  }

  bool is_empty() const { return ns.empty() && name.empty() && str_value.empty(); }
};

inline std::string to_string(const audit_strings& ai)
{
  std::ostringstream ss;
  if (!ai.ns.empty() && ai.ns != " ") { ss << ai.ns << '^'; }
  ss << ai.name;
  if (!ai.str_value.empty()) { ss << '^' << ai.str_value; }
  return ss.str();
}

// sparse feature definition for the library interface
class feature
{
public:
  float x;
  uint64_t weight_index;

  feature() = default;
  feature(float _x, uint64_t _index) : x(_x), weight_index(_index) {}

  feature(const feature&) = default;
  feature& operator=(const feature&) = default;
  feature(feature&&) = default;
  feature& operator=(feature&&) = default;
};
static_assert(std::is_trivial<feature>::value, "To be used in v_array feature must be trivial");

namespace details
{
template <typename feature_value_type_t, typename feature_index_type_t, typename audit_type_t>
class audit_features_iterator final
{
public:
  using iterator_category = std::random_access_iterator_tag;
  using difference_type = std::ptrdiff_t;
  using value_type = audit_features_iterator<feature_value_type_t, feature_index_type_t, audit_type_t>;
  using pointer = value_type*;
  using reference = value_type&;
  using const_reference = const value_type&;

  audit_features_iterator() : _begin_values(nullptr), _begin_indices(nullptr), _begin_audit(nullptr) {}

  audit_features_iterator(
      feature_value_type_t* begin_values, feature_index_type_t* begin_indices, audit_type_t* begin_audit)
      : _begin_values(begin_values), _begin_indices(begin_indices), _begin_audit(begin_audit)
  {
  }

  audit_features_iterator(const audit_features_iterator&) = default;
  audit_features_iterator& operator=(const audit_features_iterator&) = default;
  audit_features_iterator(audit_features_iterator&&) noexcept = default;
  audit_features_iterator& operator=(audit_features_iterator&&) noexcept = default;

  inline feature_value_type_t& value() { return *_begin_values; }
  inline const feature_value_type_t& value() const { return *_begin_values; }

  inline feature_index_type_t& index() { return *_begin_indices; }
  inline const feature_index_type_t& index() const { return *_begin_indices; }

  inline audit_type_t* audit() { return _begin_audit; }
  inline const audit_type_t* audit() const { return _begin_audit; }

  inline reference operator*() { return *this; }
  inline const_reference operator*() const { return *this; }

  // Required for forward_iterator
  audit_features_iterator& operator++()
  {
    _begin_values++;
    _begin_indices++;
    if (_begin_audit != nullptr) { _begin_audit++; }
    return *this;
  }

  audit_features_iterator& operator+=(difference_type diff)
  {
    _begin_values += diff;
    _begin_indices += diff;
    if (_begin_audit != nullptr) { _begin_audit += diff; }
    return *this;
  }

  audit_features_iterator& operator-=(difference_type diff)
  {
    _begin_values -= diff;
    _begin_indices -= diff;
    if (_begin_audit != nullptr) { _begin_audit += diff; }
    return *this;
  }

  friend audit_features_iterator<feature_value_type_t, feature_index_type_t, audit_type_t> operator+(
      const audit_features_iterator& lhs, difference_type rhs)
  {
    return {lhs._begin_values + rhs, lhs._begin_indices + rhs,
        lhs._begin_audit != nullptr ? lhs._begin_audit + rhs : nullptr};
  }

  friend audit_features_iterator<feature_value_type_t, feature_index_type_t, audit_type_t> operator+(
      difference_type lhs, const audit_features_iterator& rhs)
  {
    return {rhs._begin_values + lhs, rhs._begin_indices + lhs,
        rhs._begin_audit != nullptr ? rhs._begin_audit + lhs : nullptr};
  }

  friend difference_type operator-(const audit_features_iterator& lhs, const audit_features_iterator& rhs)
  {
    return lhs._begin_values - rhs._begin_values;
  }

  friend audit_features_iterator<feature_value_type_t, feature_index_type_t, audit_type_t> operator-(
      const audit_features_iterator& lhs, difference_type rhs)
  {
    return {lhs._begin_values - rhs, lhs._begin_indices - rhs,
        lhs._begin_audit != nullptr ? lhs._begin_audit - rhs : nullptr};
  }

  // For all of the comparison operations only _begin_values is used, since the other values are incremented in sequence
  // (or ignored in the case of _begin_audit if it is nullptr)
  friend bool operator<(const audit_features_iterator& lhs, const audit_features_iterator& rhs)
  {
    return lhs._begin_values < rhs._begin_values;
  }

  friend bool operator>(const audit_features_iterator& lhs, const audit_features_iterator& rhs)
  {
    return lhs._begin_values > rhs._begin_values;
  }

  friend bool operator<=(const audit_features_iterator& lhs, const audit_features_iterator& rhs)
  {
    return !(lhs > rhs);
  }
  friend bool operator>=(const audit_features_iterator& lhs, const audit_features_iterator& rhs)
  {
    return !(lhs < rhs);
  }

  friend bool operator==(const audit_features_iterator& lhs, const audit_features_iterator& rhs)
  {
    return lhs._begin_values == rhs._begin_values;
  }

  friend bool operator!=(const audit_features_iterator& lhs, const audit_features_iterator& rhs)
  {
    return !(lhs == rhs);
  }

  friend void swap(audit_features_iterator& lhs, audit_features_iterator& rhs)
  {
    std::swap(lhs._begin_values, rhs._begin_values);
    std::swap(lhs._begin_indices, rhs._begin_indices);
    std::swap(lhs._begin_audit, rhs._begin_audit);
  }
  friend class ::VW::features;

private:
  feature_value_type_t* _begin_values;
  feature_index_type_t* _begin_indices;
  audit_type_t* _begin_audit;
};

template <typename feature_value_type_t, typename feature_index_type_t>
class features_iterator final
{
public:
  using iterator_category = std::random_access_iterator_tag;
  using difference_type = std::ptrdiff_t;
  using value_type = features_iterator<feature_value_type_t, feature_index_type_t>;
  using pointer = value_type*;
  using reference = value_type&;
  using const_reference = const value_type&;

  features_iterator() : _begin_values(nullptr), _begin_indices(nullptr) {}

  features_iterator(feature_value_type_t* begin_values, feature_index_type_t* begin_indices)
      : _begin_values(begin_values), _begin_indices(begin_indices)
  {
  }

  features_iterator(const features_iterator&) = default;
  features_iterator& operator=(const features_iterator&) = default;
  features_iterator(features_iterator&&) noexcept = default;
  features_iterator& operator=(features_iterator&&) noexcept = default;

  inline feature_value_type_t& value() { return *_begin_values; }
  inline const feature_value_type_t& value() const { return *_begin_values; }

  inline feature_index_type_t& index() { return *_begin_indices; }
  inline const feature_index_type_t& index() const { return *_begin_indices; }

  inline reference operator*() { return *this; }
  inline const_reference operator*() const { return *this; }

  features_iterator& operator++()
  {
    _begin_values++;
    _begin_indices++;
    return *this;
  }

  features_iterator& operator+=(difference_type diff)
  {
    _begin_values += diff;
    _begin_indices += diff;
    return *this;
  }

  features_iterator& operator-=(difference_type diff)
  {
    _begin_values -= diff;
    _begin_indices -= diff;
    return *this;
  }

  friend features_iterator<feature_value_type_t, feature_index_type_t> operator+(
      const features_iterator& lhs, difference_type rhs)
  {
    return {lhs._begin_values + rhs, lhs._begin_indices + rhs};
  }

  friend features_iterator<feature_value_type_t, feature_index_type_t> operator+(
      difference_type lhs, const features_iterator& rhs)
  {
    return {rhs._begin_values + lhs, rhs._begin_indices + lhs};
  }

  friend difference_type operator-(const features_iterator& lhs, const features_iterator& rhs)
  {
    return lhs._begin_values - rhs._begin_values;
  }

  friend features_iterator<feature_value_type_t, feature_index_type_t> operator-(
      const features_iterator& lhs, difference_type rhs)
  {
    return {lhs._begin_values - rhs, lhs._begin_indices - rhs};
  }

  // For all of the comparison operations only _begin_values is used, since _begin_indices is incremented along with it
  friend bool operator<(const features_iterator& lhs, const features_iterator& rhs)
  {
    return lhs._begin_values < rhs._begin_values;
  }

  friend bool operator>(const features_iterator& lhs, const features_iterator& rhs)
  {
    return lhs._begin_values > rhs._begin_values;
  }

  friend bool operator<=(const features_iterator& lhs, const features_iterator& rhs) { return !(lhs > rhs); }
  friend bool operator>=(const features_iterator& lhs, const features_iterator& rhs) { return !(lhs < rhs); }

  friend bool operator==(const features_iterator& lhs, const features_iterator& rhs)
  {
    return lhs._begin_values == rhs._begin_values;
  }

  friend bool operator!=(const features_iterator& lhs, const features_iterator& rhs) { return !(lhs == rhs); }

  friend void swap(features_iterator& lhs, features_iterator& rhs)
  {
    std::swap(lhs._begin_values, rhs._begin_values);
    std::swap(lhs._begin_indices, rhs._begin_indices);
  }
  friend class ::VW::features;

private:
  feature_value_type_t* _begin_values;
  feature_index_type_t* _begin_indices;
};

}  // namespace details

/// the core definition of a set of features.
class features
{
public:
  using iterator = details::features_iterator<feature_value, feature_index>;
  using const_iterator = details::features_iterator<const feature_value, const feature_index>;
  using audit_iterator = details::audit_features_iterator<feature_value, feature_index, VW::audit_strings>;
  using const_audit_iterator =
      details::audit_features_iterator<const feature_value, const feature_index, const VW::audit_strings>;

  VW::v_array<feature_value> values;           // Always needed.
  VW::v_array<feature_index> indices;          // Optional for sparse data.
  std::vector<VW::audit_strings> space_names;  // Optional for audit mode.

  float sum_feat_sq = 0.f;

  features() = default;
  ~features() = default;
  features(const features&) = default;
  features& operator=(const features&) = default;

  // custom move operators required since we need to leave the old value in
  // a null state to prevent freeing of shallow copied v_arrays
  features(features&& other) = default;
  features& operator=(features&& other) = default;

  inline size_t size() const { return values.size(); }

  inline bool empty() const { return values.empty(); }
  inline bool nonempty() const { return !empty(); }

  // default iterator for values & features
  inline iterator begin() { return {values.begin(), indices.begin()}; }
  inline const_iterator begin() const { return {values.begin(), indices.begin()}; }
  inline iterator end() { return {values.end(), indices.end()}; }
  inline const_iterator end() const { return {values.end(), indices.end()}; }

  inline const_iterator cbegin() const { return {values.cbegin(), indices.cbegin()}; }
  inline const_iterator cend() const { return {values.cend(), indices.cend()}; }

  inline VW::generic_range<audit_iterator> audit_range() { return {audit_begin(), audit_end()}; }
  inline VW::generic_range<const_audit_iterator> audit_range() const { return {audit_cbegin(), audit_cend()}; }

  inline audit_iterator audit_begin() { return {values.begin(), indices.begin(), space_names.data()}; }
  inline const_audit_iterator audit_begin() const { return {values.begin(), indices.begin(), space_names.data()}; }
  inline audit_iterator audit_end() { return {values.end(), indices.end(), space_names.data() + space_names.size()}; }
  inline const_audit_iterator audit_end() const
  {
    return {values.end(), indices.end(), space_names.data() + space_names.size()};
  }

  inline const_audit_iterator audit_cbegin() const { return {values.begin(), indices.begin(), space_names.data()}; }
  inline const_audit_iterator audit_cend() const
  {
    return {values.end(), indices.end(), space_names.data() + space_names.size()};
  }

  void clear();
  // These 3 overloads can be used if the sum_feat_sq of the removed section is known to avoid recalculating.
  void truncate_to(const audit_iterator& pos, float sum_feat_sq_of_removed_section);
  void truncate_to(const iterator& pos, float sum_feat_sq_of_removed_section);
  void truncate_to(size_t i, float sum_feat_sq_of_removed_section);
  void truncate_to(const audit_iterator& pos);
  void truncate_to(const iterator& pos);
  void truncate_to(size_t i);
  void concat(const features& other);
  void push_back(feature_value v, feature_index i);
  void push_back(feature_value v, feature_index i, uint64_t ns_hash);
  bool sort(uint64_t parse_mask);
};

/// Both fs1 and fs2 must be sorted.
/// Most often used with VW::flatten_features
float features_dot_product(const features& fs1, const features& fs2);
}  // namespace VW

using feature_value VW_DEPRECATED("Moved into VW namespace. Will be removed in VW 10.") = VW::feature_value;
using feature_index VW_DEPRECATED("Moved into VW namespace. Will be removed in VW 10.") = VW::feature_index;
using namespace_index VW_DEPRECATED("Moved into VW namespace. Will be removed in VW 10.") = VW::namespace_index;
using audit_strings VW_DEPRECATED("Moved into VW namespace. Will be removed in VW 10.") = VW::audit_strings;
using features VW_DEPRECATED("Moved into VW namespace. Will be removed in VW 10.") = VW::features;
using feature VW_DEPRECATED("Moved into VW namespace. Will be removed in VW 10.") = VW::feature;
