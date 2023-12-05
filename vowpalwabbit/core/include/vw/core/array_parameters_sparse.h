// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.

#pragma once

#include "vw/common/vw_exception.h"
#include "vw/common/vw_throw.h"
#include "vw/core/constant.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <unordered_map>

namespace VW
{

class sparse_parameters;
namespace details
{

using weight_map = std::unordered_map<uint64_t, std::shared_ptr<VW::weight>>;

template <typename T>
class sparse_iterator
{
public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = T;
  using difference_type = ptrdiff_t;
  using pointer = T*;
  using reference = T&;

  sparse_iterator(weight_map::iterator iter) : _iter(iter) {}

  sparse_iterator& operator=(const sparse_iterator& other) = default;
  sparse_iterator(const sparse_iterator& other) = default;
  sparse_iterator& operator=(sparse_iterator&& other) noexcept = default;
  sparse_iterator(sparse_iterator&& other) noexcept = default;

  uint64_t index() { return _iter->first; }

  T& operator*() { return *(_iter->second); }

  sparse_iterator& operator++()
  {
    _iter++;
    return *this;
  }

  bool operator==(const sparse_iterator& rhs) const { return _iter == rhs._iter; }
  bool operator!=(const sparse_iterator& rhs) const { return _iter != rhs._iter; }

private:
  weight_map::iterator _iter;
};
}  // namespace details

class sparse_parameters
{
public:
  using iterator = details::sparse_iterator<VW::weight>;
  using const_iterator = details::sparse_iterator<const VW::weight>;

  // The sparse_parameters object consists of a std::unordered_map from
  // feature hash to a contiguous array of (feature_width * stride) floats.
  sparse_parameters(uint32_t hash_bits, uint32_t feature_width_bits, uint32_t stride_shift = 0);
  sparse_parameters();

  sparse_parameters(const sparse_parameters& other) = delete;
  sparse_parameters& operator=(const sparse_parameters& other) = delete;
  sparse_parameters& operator=(sparse_parameters&&) noexcept = delete;
  sparse_parameters(sparse_parameters&&) noexcept = delete;

  bool not_null() { return (_weight_mask > 0); }
  VW::weight* first() { THROW_OR_RETURN("Allreduce currently not supported in sparse", nullptr); }

  // iterator with stride
  iterator begin() { return iterator(_map.begin()); }
  iterator end() { return iterator(_map.end()); }

  // const iterator
  const_iterator cbegin() const { return const_iterator(_map.begin()); }
  const_iterator cend() const { return const_iterator(_map.end()); }

  // operator[] will find weight in _map and return and insert a default value if not found. Does alter _map.
  inline VW::weight& operator[](size_t i) { return *(get_or_default_and_get(i)); }
  inline const VW::weight& operator[](size_t i) const { return *(get_or_default_and_get(i)); }

  // get() will find weight in _map and return a default value if not found. Does not alter _map.
  inline VW::weight& get(size_t i) { return *(get_impl(i)); };
  inline const VW::weight& get(size_t i) const { return *(get_impl(i)); };

  // index() is a 3-dimensional index
  inline VW::weight& index(size_t hash_index, size_t width_index, size_t stride_index = 0)
  {
    return operator[](
        (hash_index << (_feature_width_bits + _stride_shift)) + (width_index << _stride_shift) + stride_index);
  }
  inline const VW::weight& index(size_t hash_index, size_t width_index, size_t stride_index = 0) const
  {
    return operator[](
        (hash_index << (_feature_width_bits + _stride_shift)) + (width_index << _stride_shift) + stride_index);
  }

  // strided_index() is a 2-dimensional index
  // first dimension includes hash index and feature width index
  // second dimension is the stride index
  inline VW::weight& strided_index(size_t hash_width_index, size_t stride_index = 0)
  {
    return operator[]((hash_width_index << _stride_shift) + stride_index);
  }
  inline const VW::weight& strided_index(size_t hash_width_index, size_t stride_index = 0) const
  {
    return operator[]((hash_width_index << _stride_shift) + stride_index);
  }

  void shallow_copy(const sparse_parameters& input);

  template <typename Lambda>
  void set_default(Lambda&& default_func)
  {
    _default_func = default_func;
  }

  void set_zero(size_t offset);

  uint64_t hash_mask() const { return _hash_mask; }
  uint64_t weight_mask() const { return _weight_mask; }

  uint64_t stride() const { return static_cast<uint64_t>(1) << _stride_shift; }
  uint32_t stride_shift() const { return _stride_shift; }

  uint32_t hash_bits() const { return _hash_bits; }
  uint32_t feature_width_bits() const { return _feature_width_bits; }

  void stride_shift(uint32_t stride_shift) { _stride_shift = stride_shift; }

#ifndef _WIN32
  void share(size_t /* length */);
#endif

private:
  // This must be mutable because the const operator[] must be able to intialize default weights to return.
  mutable details::weight_map _map;

  // Function used to initialize weights
  // Arguments are pointer to weight and 1-dimensional array index
  std::function<void(VW::weight*, uint64_t)> _default_func;

  uint32_t _hash_bits;
  uint32_t _feature_width_bits;
  uint32_t _stride_shift;

  // (1 << hash_bits) - 1
  uint64_t _hash_mask;

  // (1 << (hash_bits + feature_width_bits + stride_shift)) - 1
  uint64_t _weight_mask;

  // It is marked const so it can be used from both const and non const operator[]
  // The map itself is mutable to facilitate this
  VW::weight* get_or_default_and_get(size_t i) const;
  VW::weight* get_impl(size_t i) const;

  std::shared_ptr<VW::weight> allocate_block(size_t hash_index) const;
};
}  // namespace VW
using sparse_parameters VW_DEPRECATED("sparse_parameters moved into VW namespace") = VW::sparse_parameters;
