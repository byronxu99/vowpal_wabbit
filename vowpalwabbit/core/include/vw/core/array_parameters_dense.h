// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.

#pragma once

#include "vw/core/constant.h"

#include <cassert>
#include <cstdint>
#include <iterator>
#include <memory>

namespace VW
{

namespace details
{

template <typename T>
class dense_iterator
{
public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = T;
  using difference_type = std::ptrdiff_t;
  using pointer = T*;
  using reference = T&;

  dense_iterator(T* current, T* begin, uint32_t stride_shift)
      : _current(current), _begin(begin), _stride(static_cast<uint64_t>(1) << stride_shift), _stride_shift(stride_shift)
  {
  }

  T& operator*() { return *_current; }

  size_t index() { return _current - _begin; }

  size_t index_without_stride() { return (index() >> _stride_shift); }

  dense_iterator& operator++()
  {
    _current += _stride;
    return *this;
  }

  dense_iterator& operator+(size_t n)
  {
    _current += _stride * n;
    return *this;
  }

  dense_iterator& operator+=(size_t n)
  {
    _current += _stride * n;
    return *this;
  }

  dense_iterator& next_non_zero(const dense_iterator& end)
  {
    while (_current + _stride < end._current)
    {
      _current += _stride;
      if (*_current != 0.0f) { return *this; }
    }
    _current = end._current;
    return *this;
  }

  // ignores the stride
  pointer operator[](size_t n)
  {
    assert(n < _stride);
    return _current + n;
  }

  bool operator==(const dense_iterator& rhs) const { return _current == rhs._current; }
  bool operator!=(const dense_iterator& rhs) const { return _current != rhs._current; }
  bool operator<(const dense_iterator& rhs) const { return _current < rhs._current; }
  bool operator<=(const dense_iterator& rhs) const { return _current <= rhs._current; }

private:
  T* _current;
  T* _begin;
  uint64_t _stride;
  uint32_t _stride_shift;
};
}  // namespace details

class dense_parameters
{
public:
  using iterator = details::dense_iterator<VW::weight>;
  using const_iterator = details::dense_iterator<const VW::weight>;

  dense_parameters(uint32_t hash_bits, uint32_t feature_width_bits, uint32_t stride_shift = 0);
  dense_parameters();

  dense_parameters(const dense_parameters& other) = delete;
  dense_parameters& operator=(const dense_parameters& other) = delete;
  dense_parameters& operator=(dense_parameters&&) noexcept;
  dense_parameters(dense_parameters&&) noexcept;

  bool not_null();
  VW::weight* first() { return _begin.get(); }  // TODO: Temporary fix for allreduce.

  VW::weight* data() { return _begin.get(); }

  const VW::weight* data() const { return _begin.get(); }

  // iterator with stride
  iterator begin() { return iterator(_begin.get(), _begin.get(), stride_shift()); }
  iterator end() { return iterator(_begin.get() + _weight_mask + 1, _begin.get(), stride_shift()); }

  // const iterator
  const_iterator cbegin() const { return const_iterator(_begin.get(), _begin.get(), stride_shift()); }
  const_iterator cend() const { return const_iterator(_begin.get() + _weight_mask + 1, _begin.get(), stride_shift()); }

  // operator[] is a 1-dimensional index into a flattened array
  inline const VW::weight& operator[](size_t i) const { return _begin.get()[i & _weight_mask]; }
  inline VW::weight& operator[](size_t i) { return _begin.get()[i & _weight_mask]; }

  // get() is only needed for sparse_weights, same as operator[] for dense_weights
  inline const VW::weight& get(size_t i) const { return operator[](i); }
  inline VW::weight& get(size_t i) { return operator[](i); }

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

  VW_ATTR(nodiscard) static dense_parameters shallow_copy(const dense_parameters& input);
  VW_ATTR(nodiscard) static dense_parameters deep_copy(const dense_parameters& input);

  template <typename Lambda>
  void set_default(Lambda&& default_func)
  {
    if (not_null())
    {
      auto iter = begin();
      for (size_t i = 0; iter != end(); ++iter, i += stride())
      {
        // Types are required to be VW::weight* and uint64_t.
        default_func(&(*iter), iter.index());
      }
    }
  }

  void set_zero(size_t offset);

  uint64_t hash_mask() const { return _hash_mask; }
  uint64_t weight_mask() const { return _weight_mask; }

  uint64_t raw_length() const { return _weight_mask + 1; }

  uint64_t stride() const { return static_cast<uint64_t>(1) << _stride_shift; }
  uint32_t stride_shift() const { return _stride_shift; }

  uint32_t hash_bits() const { return _hash_bits; }
  uint32_t feature_width_bits() const { return _feature_width_bits; }

  void stride_shift(uint32_t stride_shift) { _stride_shift = stride_shift; }

#ifndef _WIN32
#  ifndef DISABLE_SHARED_WEIGHTS
  void share(size_t length);
#  endif
#endif

private:
  std::shared_ptr<VW::weight> _begin;
  uint32_t _hash_bits;
  uint32_t _feature_width_bits;
  uint32_t _stride_shift;

  // (1 << hash_bits) - 1
  uint64_t _hash_mask;

  // (1 << (hash_bits + feature_width_bits + stride_shift)) - 1
  uint64_t _weight_mask;
};
}  // namespace VW
using dense_parameters VW_DEPRECATED("dense_parameters moved into VW namespace") = VW::dense_parameters;
