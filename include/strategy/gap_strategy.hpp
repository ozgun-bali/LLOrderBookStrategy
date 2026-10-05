#ifndef LLOB_STRATEGY_GAP_STRATEGY_HPP
#define LLOB_STRATEGY_GAP_STRATEGY_HPP

// One-tick gap detection. Evaluated once per completed timestamp group, never
// between messages that share a timestamp.
//
// All of these must hold:
//   1. book state == P_SUREKLI_ISLEM
//   2. MoldUDP64 sequence stream valid
//   3. both sides have at least one level
//   4. best_ask - best_bid == 2 * tick_size
//   5. the gap was created by a level that existed at gap_price BEFORE the group
//      disappearing during it
//   6. a price that flipped bid<->ask within the group does NOT signal
//   7. one order per gap transition
//
//   gap_price = best_bid + tick_size
//
// Direction: gap_price was an ask before the group -> SELL; was a bid -> BUY;
// absent or ambiguous -> no order.
//
// Rules 5 and 6 need the pre-group side of touched prices. A small journal of
// prices modified during the group (price -> side before the group) is cheaper
// than snapshotting the book and is fixed-capacity.
//
// Quantity, clamped by position limits:
//   BUY  send = min(order_quantity, maximum_position - position)
//   SELL send = min(order_quantity, position - minimum_position)
//   send <= 0 -> no order

#include "core/types.hpp"

namespace llob {

// TODO

} // namespace llob

#endif // LLOB_STRATEGY_GAP_STRATEGY_HPP
