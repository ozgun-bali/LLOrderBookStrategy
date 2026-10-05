#ifndef LLOB_BOOK_ORDER_POOL_HPP
#define LLOB_BOOK_ORDER_POOL_HPP

// Fixed-capacity storage for resting orders, plus the order id index.
//
// Capacity comes from config.max_active_orders and is allocated once. Exceeding
// it must produce a safe error and stop the strategy - never silent corruption.
//
// KEY DECISION: ITCH order ids are unique per (order book, side) only. Since we
// filter to one book, the lookup key is (side, order_id). Two separate index
// tables - one per side - is simpler and faster than hashing a composite key,
// and every 'E'/'D' message carries the Side field so there is no ambiguity.
//
// Lookup must be average O(1). Open addressing over a power-of-two table keeps
// the probe in cache and avoids the node-per-entry indirection of a chained map.
// Preallocate; never rehash on the hot path.
//
// Per order: order id, price, remaining quantity, side.

#include "core/types.hpp"

namespace llob {

// TODO

} // namespace llob

#endif // LLOB_BOOK_ORDER_POOL_HPP
