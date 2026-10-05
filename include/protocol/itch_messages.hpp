#ifndef LLOB_PROTOCOL_ITCH_MESSAGES_HPP
#define LLOB_PROTOCOL_ITCH_MESSAGES_HPP

// ITCH message field offsets and decoded views.
//
// All offsets verified against BISTECH ITCH Protocol Specification (BIST v2112).
// Numeric = unsigned big-endian. Price = SIGNED big-endian 4 bytes.
// Alpha = left justified, space padded, ISO 8859-1, NOT null terminated.
//
//   'T' Seconds           5 bytes   type(0,1) second(1,4)
//   'O' Order Book State 29 bytes   type(0,1) nanos(1,4) bookId(5,4) state(9,20)
//   'A' Add Order        45 bytes   type(0,1) nanos(1,4) orderId(5,8) bookId(13,4)
//                                   side(17,1) rankSeq(18,4) qty(22,8) price(30,4)
//                                   attrs(34,2) lotType(36,1) rankTime(37,8)
//   'E' Order Executed   52 bytes   type(0,1) nanos(1,4) orderId(5,8) bookId(13,4)
//                                   side(17,1) execQty(18,8) matchId(26,8)
//                                   comboGroupId(34,4) reserved(38,7) reserved(45,7)
//   'D' Order Delete     18 bytes   type(0,1) nanos(1,4) orderId(5,8) bookId(13,4)
//                                   side(17,1)
//
// Notes that bite:
//   - 'E' carries no price. Last execution price comes from the resting order,
//     so read it BEFORE removing the order.
//   - Add Order Quantity == 0 means undisclosed (iceberg).
//   - Add Order Price == PRICE_NOT_AVAILABLE means market order - no level.

#include "core/types.hpp"

namespace llob {

// TODO

} // namespace llob

#endif // LLOB_PROTOCOL_ITCH_MESSAGES_HPP
