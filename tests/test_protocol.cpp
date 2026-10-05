// Protocol level tests.
//
// Required by the assignment: truncated header, invalid message length,
// sequence gap, heartbeat, end of session, non-target order book.
//
// Worth adding: duplicate sequence, past sequence, unexpected session change,
// multiple messages in one packet, unknown ITCH message type, a message whose
// block length disagrees with its type's fixed size, and the 'T' seconds
// rollover (same nanosecond value in two different seconds must not group).

#include "test_framework.hpp"

// TODO
