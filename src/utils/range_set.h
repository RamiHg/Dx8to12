#pragma once

#include <vector>

#include "util.h"

namespace Dx8to12 {

// A collection of ranges that automatically coalesces consecutive ranges.
class RangeSet {
 public:
  struct Range {
    int offset;
    int size;
    bool operator<(const Range &rhs) const { return offset < rhs.offset; };
    bool operator==(const Range &rhs) const = default;
  };

  void insert(Range range) {
    auto iter = std::lower_bound(ranges.begin(), ranges.end(), range);
    if (iter != ranges.begin()) {
      if (auto prev = std::prev(iter);
          prev->offset + prev->size >= range.offset) {
        ASSERT(prev->offset + prev->size == range.offset);
        prev->size += range.size;
        // TODO: Handle case where we can coalesce with next element. Never
        // happens in practice.
        return;
      }
    }
    if (iter != ranges.end() && range.offset + range.size >= iter->offset) {
      // Are these the same range?
      if (*iter == range) return;
      ASSERT(range.offset + range.size == iter->offset);
      // Warning: Untested path.
      iter->offset = range.offset;
      iter->size += range.size;
    } else {
      ranges.insert(iter, range);
    }
  }

  std::vector<Range> ranges;
};

}  // namespace Dx8to12