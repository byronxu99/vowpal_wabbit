#include "vw/slim/vw_slim_predict.h"

#include <algorithm>
#include <cctype>

namespace vw_slim
{
uint64_t ceil_log_2(uint64_t v)
{
  if (v == 0) { return 0; }
  else { return 1 + ceil_log_2(v >> 1); }
}

namespace_copy_guard::namespace_copy_guard(VW::example_predict& ex, VW::namespace_index ns) : _ex(ex), _ns(ns)
{
  if (!ex.contains(ns))
  {
    // New namespace will be created
    // We must delete the entire namespace when we are done
    _remove_ns = true;
  }
  else
  {
    // Namespace already exists
    // We must restore the namespace's features when we are done
    _remove_ns = false;
    _restore_guard = ex[ns].stash_features();
  }
}

namespace_copy_guard::~namespace_copy_guard()
{
  if (_remove_ns) { _ex.delete_namespace(_ns); }
}

void namespace_copy_guard::feature_push_back(VW::feature_value v, VW::feature_index idx)
{
  _ex[_ns].add_feature_raw(idx, v);
}

feature_offset_guard::feature_offset_guard(VW::example_predict& ex, uint64_t ft_index_offset)
    : _ex(ex), _old_ft_index_offset(ex.ft_index_offset)
{
  _ex.ft_index_offset = ft_index_offset;
}

feature_offset_guard::~feature_offset_guard() { _ex.ft_index_offset = _old_ft_index_offset; }

stride_shift_guard::stride_shift_guard(VW::example_predict& ex, uint64_t shift) : _ex(ex), _shift(shift)
{
  if (_shift > 0)
  {
    for (auto ns : _ex)
    {
      for (auto& f : _ex[ns]) { f.index() <<= _shift; }
    }
  }
}

stride_shift_guard::~stride_shift_guard()
{
  if (_shift > 0)
  {
    for (auto ns : _ex)
    {
      for (auto& f : _ex[ns]) { f.index() >>= _shift; }
    }
  }
}

}  // namespace vw_slim