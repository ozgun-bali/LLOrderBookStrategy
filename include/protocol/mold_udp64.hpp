#ifndef LLOB_PROTOCOL_MOLD_UDP64_HPP
#define LLOB_PROTOCOL_MOLD_UDP64_HPP

// MoldUDP64 framing and sequence tracking.
//
// Header is 20 bytes: Session (10, Alpha) | Sequence Number (8, BE) |
// Message Count (2, BE). Then Message Count blocks of [2-byte BE length][payload].
//
// Sequence numbers count MESSAGES, not packets:
//   expected_next = sequence + message_count
//
// Special message counts:
//   0x0000 -> heartbeat (no blocks)
//   0xFFFF -> end of session
//
// Must distinguish and count: in-order, gap, duplicate, past sequence,
// invalid message length, truncated header, unexpected session change.
// Session is Alpha - compare with memcmp over 10 bytes, never strcmp.
//
// On gap: mark the book unreliable and suppress strategy orders. Does not
// request retransmission.

#include "core/types.hpp"

namespace llob {

// TODO

} // namespace llob

#endif // LLOB_PROTOCOL_MOLD_UDP64_HPP
