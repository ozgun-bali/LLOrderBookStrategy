#ifndef LLOB_PROTOCOL_ITCH_PARSER_HPP
#define LLOB_PROTOCOL_ITCH_PARSER_HPP

// ITCH message dispatch.
//
// Owns current_second, updated from 'T' Seconds messages. 'T' is tracked but
// never applied to the book - the nanosecond field in every other message is
// relative to the last 'T', so grouping needs the composed 64-bit timestamp.
//
// Dispatch order matters for throughput: switch on type byte, then read the
// Order Book ID at its offset and bail immediately if it is not the target.
// Most messages in the feed belong to other books.
//
// Handled: 'O' 'A' 'E' 'D'. Everything else is skipped by advancing the
// MoldUDP64 block length - never by interpreting the ITCH payload.
//
// Skipped types that would matter on real data (document in README):
//   'C' Order Executed with Price - also reduces order quantity
//   'Y' Order Book Flush          - clears the whole book

#include "core/types.hpp"

namespace llob {

// TODO

} // namespace llob

#endif // LLOB_PROTOCOL_ITCH_PARSER_HPP
