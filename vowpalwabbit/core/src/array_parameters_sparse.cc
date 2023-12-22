// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.

#include "vw/core/array_parameters_sparse.h"

#include "vw/common/vw_exception.h"
#include "vw/core/memory.h"

#include <cstddef>
#include <functional>
#include <unordered_map>

// Allocate a contiguous block of (feature_width * stride) float elements
// hash_index is the feature hash of the start of the block
// If defined, _default_func is used to initialize the block
// _default_func is called with 1-dimensional array index for each feature width increment
std::shared_ptr<VW::weight> VW::sparse_parameters::allocate_block(size_t hash_index) const
{
  size_t feature_width = static_cast<uint64_t>(1) << _feature_width_bits;
  size_t block_size = static_cast<uint64_t>(1) << (_feature_width_bits + _stride_shift);

  // memory allocated by calloc should be freed by C free()
  std::shared_ptr<VW::weight> new_block(VW::details::calloc_mergable_or_throw<VW::weight>(block_size), free);

  if (_default_func)
  {
    // Call _default_func once for each increment in feature width
    // Stride dimension is always zero
    for (size_t i = 0; i < feature_width; i++)
    {
      size_t strided_width_index = i << _stride_shift;
      auto* weight_ptr = &new_block.get()[strided_width_index];
      size_t weight_index = (hash_index << (_feature_width_bits + _stride_shift)) + strided_width_index;
      _default_func(weight_ptr, weight_index);
    }
  }

  return new_block;
}

VW::weight* VW::sparse_parameters::get_or_default_and_get(size_t i) const
{
  size_t hash_index = (i & _weight_mask) >> (_feature_width_bits + _stride_shift);
  size_t width_stride_index = i & ((static_cast<uint64_t>(1) << (_feature_width_bits + _stride_shift)) - 1);

  auto iter = _map.find(hash_index);
  if (iter == _map.end())
  {
    _map.insert(std::make_pair(hash_index, allocate_block(hash_index)));
    iter = _map.find(hash_index);
  }
  return iter->second.get() + width_stride_index;
}

VW::weight* VW::sparse_parameters::get_impl(size_t i) const
{
  // Static value to return if entry is not found in the map and no default function exists
  static auto default_value = allocate_block(0);

  size_t hash_index = (i & _weight_mask) >> (_feature_width_bits + _stride_shift);
  size_t width_stride_index = i & ((static_cast<uint64_t>(1) << (_feature_width_bits + _stride_shift)) - 1);

  auto iter = _map.find(hash_index);
  if (iter == _map.end())
  {
    // Add entry to map if _default_func is defined
    if (_default_func != nullptr)
    {
      _map.insert(std::make_pair(hash_index, allocate_block(hash_index)));
      iter = _map.find(hash_index);
      return iter->second.get() + width_stride_index;
    }
    // Return default value if _default_func is not defined
    return default_value.get() + width_stride_index;
  }

  // Get entry if it exists in the map
  return iter->second.get() + width_stride_index;
}

VW::sparse_parameters::sparse_parameters(uint32_t feature_hash_bits, uint32_t feature_width_bits, uint32_t stride_shift)
    : _default_func(nullptr)
    , _feature_hash_bits(feature_hash_bits)
    , _feature_width_bits(feature_width_bits)
    , _stride_shift(stride_shift)
{
  _hash_mask = (static_cast<uint64_t>(1) << feature_hash_bits) - 1;
  _weight_mask = (static_cast<uint64_t>(1) << (feature_hash_bits + feature_width_bits + stride_shift)) - 1;
}

VW::sparse_parameters::sparse_parameters()
    : _default_func(nullptr)
    , _feature_hash_bits(0)
    , _feature_width_bits(0)
    , _stride_shift(0)
    , _hash_mask(0)
    , _weight_mask(0)
{
}

void VW::sparse_parameters::shallow_copy(const sparse_parameters& input)
{
  // TODO: this is level-1 copy (VW::weight* are stilled shared)
  _map = input._map;
  _default_func = input._default_func;
  _feature_hash_bits = input._feature_hash_bits;
  _feature_width_bits = input._feature_width_bits;
  _stride_shift = input._stride_shift;
  _hash_mask = input._hash_mask;
  _weight_mask = input._weight_mask;
}

void VW::sparse_parameters::set_zero(size_t offset)
{
  size_t feature_width = static_cast<uint64_t>(1) << _feature_width_bits;
  for (auto& iter : _map)
  {
    auto* block_start_ptr = iter.second.get();
    for (size_t i = 0; i < feature_width; i++)
    {
      size_t strided_width_index = i << _stride_shift;
      block_start_ptr[strided_width_index + offset] = 0;
    }
  }
}

#ifndef _WIN32
void VW::sparse_parameters::share(size_t /* length */) { THROW_OR_RETURN("Operation not supported on Windows"); }
#endif
