// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.

#pragma once
#include "vw/common/future_compat.h"

#include <cstddef>
#include <cstdint>

namespace VW
{

namespace details
{
// FNV hash constant for 32bit
// http://www.isthe.com/chongo/tech/comp/fnv/#FNV-param
constexpr uint32_t FNV_32_PRIME = 16777619;
constexpr uint32_t FNV_32_OFFSET = 2166136261;
}  // namespace details

class fnv_hasher
{
private:
  uint32_t _partial_hash;
  fnv_hasher(uint32_t partial_hash) : _partial_hash(partial_hash) {}

public:
  fnv_hasher() : _partial_hash(details::FNV_32_OFFSET) {}

  // Add data to the hash and return a new hasher object
  fnv_hasher hash(uint32_t data) const
  {
    uint32_t new_hash = _partial_hash;
    new_hash *= details::FNV_32_PRIME;
    new_hash ^= data;
    return fnv_hasher(new_hash);
  }

  // Add data to the hash in place
  void hash_in_place(uint32_t data)
  {
    _partial_hash *= details::FNV_32_PRIME;
    _partial_hash ^= data;
  }

  // Get hash value
  uint32_t get_full_hash() const { return _partial_hash; }

  // Get hash value truncated to the specified number of bits
  uint32_t get_truncated_hash(uint32_t bits) const
  {
    // XOR fold the hash
    // http://www.isthe.com/chongo/tech/comp/fnv/#xor-fold
    uint32_t mask = (1 << bits) - 1;
    uint32_t upper_bits = _partial_hash >> bits;
    return (_partial_hash ^ upper_bits) & mask;
  }
};

}  // namespace VW
