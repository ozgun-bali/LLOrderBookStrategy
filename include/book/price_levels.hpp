#ifndef LLOB_BOOK_PRICE_LEVELS_HPP
#define LLOB_BOOK_PRICE_LEVELS_HPP

// Price level aggregates for one side, and the best price.
//
// Per level: total quantity and active order count.
//
// KEY DECISION: prices are integer kurus with tick_size == 1, so levels can be
// a dense array indexed by (price - base_price) rather than a sorted container.
// That makes update O(1) with no comparisons and no reordering. Cost is memory
// over the price band and a bounded scan when the best level empties.
// The alternative (sorted vector + lower_bound) is worth benchmarking - whichever
// wins, the README has to justify the choice.
//
// Best price is maintained incrementally: it only moves when the current best
// level empties, or when an order arrives beyond it. Never rescan all levels.
//
// Capacity from config.max_price_levels; overflow is a safe error.

#include "core/types.hpp"

namespace llob {

// TODO

} // namespace llob

#endif // LLOB_BOOK_PRICE_LEVELS_HPP
