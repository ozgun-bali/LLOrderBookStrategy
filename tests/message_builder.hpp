#ifndef LLOB_TESTS_MESSAGE_BUILDER_HPP
#define LLOB_TESTS_MESSAGE_BUILDER_HPP

// Builds synthetic MoldUDP64 packets and ITCH messages for tests.
//
// Worth writing first: it is the only way to exercise sequence gaps, truncated
// headers, invalid lengths, heartbeats, end of session and non-target books
// deterministically. It also unblocks everything if the sample data arrives late.
//
// Should be able to emit: a 'T' seconds message, Add / Executed / Delete / state
// messages with chosen timestamps, and to wrap any set of messages into a packet
// with a chosen session, sequence number and message count - including
// deliberately malformed ones.

// TODO

#endif // LLOB_TESTS_MESSAGE_BUILDER_HPP
