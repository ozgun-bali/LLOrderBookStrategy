#ifndef LLOB_BOOK_ORDER_BOOK_HPP
#define LLOB_BOOK_ORDER_BOOK_HPP

// The order book for the single target instrument.
//
// Composes OrderPool (orders by (side, id)) and PriceLevels (aggregates per side).
// Applies Add / Executed / Delete and exposes best bid, best ask, their
// quantities, the current book state and the last market execution price.
//
// Message handling:
//   Add      - insert order, bump level aggregate and order count
//   Executed - subtract the executed quantity (it is a delta); when the order's
//              remaining quantity reaches zero, remove it. NO Delete message is
//              sent for a fully filled order - this removal is our job.
//   Delete   - subtract the order's remaining quantity, remove the order
//
// Invariants (assert under debug, compiled out with NDEBUG):
//   - one order id maps to at most one active order per side
//   - quantities never go negative
//   - level aggregate == sum of its orders' remaining quantities
//   - best bid is the highest buy, best ask the lowest sell
//   - an empty side has no best price
//   - for a valid two-sided book, best_bid < best_ask

#include "core/types.hpp"

namespace llob {

// TODO

} // namespace llob

#endif // LLOB_BOOK_ORDER_BOOK_HPP
