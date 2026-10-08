// Protocol level tests, end to end.
//
// The scenarios the assignment names: truncated header, invalid message length,
// sequence gap, heartbeat, end of session, non-target order book. Each of them
// is already checked against MoldSession and ItchParser directly in
// test_mold_udp64.cpp and test_itch_parser.cpp. What is checked here is that the
// behaviour survives the full chain and reaches the reported counters, which is
// where a reviewer will look for it.

#include "test_framework.hpp"
#include "feed_fixture.hpp"

using namespace llob;
using namespace llobtest;

TEST(protocol_a_truncated_header_stops_the_walk_and_is_reported) {
  // In a file there is no envelope around a packet, so a short header means the
  // next boundary can no longer be computed and everything after it is lost.
  // The run must say so rather than quietly stopping early.
  Feed f;
  open_the_book(f);
  f.append(Bytes(10, 0x00));                  // half a header, then nothing

  Run r(base_config(), f.wire);

  CHECK(r.truncated);
  CHECK_EQ(r.s.packets, 1ull);                // the good one was processed
  CHECK_EQ(r.s.itch_messages, 5ull);
  CHECK(r.s.have_best_bid);
  CHECK_EQ(r.s.best_bid, 1049);               // the work before it still stands
  CHECK_EQ(r.offset, f.wire.size() - 10);     // and it says where it stopped
}

TEST(protocol_a_message_shorter_than_its_type_is_counted_and_skipped) {
  // A block whose length disagrees with what the type requires. The packet is
  // self-consistent, so framing is fine; the parser is the one that has to
  // refuse it.
  Feed f;
  open_the_book(f);

  Bytes stub = add_order(2, 9, TARGET_BOOK, SIDE_SELL, 400, 1050);
  stub.resize(20);                            // 'A' needs more than this
  f.packet({ stub });

  Run r(base_config(), f.wire);

  CHECK(!r.truncated);
  CHECK_EQ(r.s.packets, 2ull);
  CHECK_EQ(r.s.invalid_message_count, 1ull);
  CHECK_EQ(r.s.best_ask, 1050);               // the bad message changed nothing
  CHECK_EQ(r.s.best_ask_quantity, 600ull);
}

TEST(protocol_a_sequence_gap_is_counted_and_silences_the_strategy) {
  // Four messages never arrived, so the book can no longer be trusted. The gap
  // that follows would have signalled; it must not.
  Feed f;
  open_the_book(f);
  f.lose(4);
  f.packet({ deleted(2, 2, TARGET_BOOK, SIDE_SELL) });

  Run r(base_config(), f.wire);

  CHECK_EQ(r.s.sequence_gap_count, 1ull);
  CHECK_EQ(r.s.generated_order_count, 0ull);
  CHECK(r.s.skip_sequence >= 1ull);
  CHECK_EQ(r.s.position, 0);
}

TEST(protocol_a_heartbeat_carries_nothing_and_breaks_nothing) {
  // A packet with no blocks, carrying the sequence number that is next
  // expected. It must not count as a message, and it must not look like a gap.
  Feed f;
  open_the_book(f);
  f.heartbeat();
  f.packet({ deleted(2, 2, TARGET_BOOK, SIDE_SELL) });

  Run r(base_config(), f.wire);

  CHECK_EQ(r.s.packets, 3ull);                // it is a packet
  CHECK_EQ(r.s.itch_messages, 6ull);          // but not a message
  CHECK_EQ(r.s.sequence_gap_count, 0ull);
  CHECK_EQ(r.s.generated_order_count, 1ull);  // the stream was never broken
}

TEST(protocol_end_of_session_is_a_bare_header_and_frames_correctly) {
  // 0xFFFF in the count field is not a count. Reading it as one would make the
  // walk look for blocks that are not there and lose the rest of the file.
  Feed f;
  open_the_book(f);
  f.end_of_session();

  Run r(base_config(), f.wire);

  CHECK(!r.truncated);
  CHECK_EQ(r.s.packets, 2ull);
  CHECK_EQ(r.s.itch_messages, 5ull);
  CHECK_EQ(r.offset, f.wire.size());          // landed exactly on the last byte
}

TEST(protocol_another_order_book_never_reaches_the_engine) {
  // 98% of the real feed is other books. They are rejected after the type byte,
  // the length check and one 32-bit comparison, and nothing above sees them.
  Feed f;
  open_the_book(f);
  f.packet({ add_order(2, 50, OTHER_BOOK, SIDE_BUY,  900, 1049),
             add_order(2, 51, OTHER_BOOK, SIDE_SELL, 900, 1050),
             deleted  (2, 50, OTHER_BOOK, SIDE_BUY) });

  Run r(base_config(), f.wire);

  CHECK_EQ(r.s.itch_messages, 8ull);          // all of them were read
  CHECK_EQ(r.s.target_messages, 4ull);        // state + three adds
  CHECK_EQ(r.s.processed_message_count, 4ull);
  CHECK_EQ(r.s.invalid_message_count, 0ull);  // not ours is not the same as bad
  CHECK_EQ(r.s.best_bid_quantity, 1200ull);   // our book is untouched
  CHECK_EQ(r.s.best_ask, 1050);
}

TEST(protocol_the_second_marker_separates_identical_nanos) {
  // Nanoseconds restart every second, so the same nanos value in two different
  // seconds is two different moments. Grouping them together would merge two
  // groups into one and hide a signal.
  Feed f;
  open_the_book(f);
  f.packet({ seconds(FEED_SECOND + 1),
             deleted(1, 2, TARGET_BOOK, SIDE_SELL) });   // nanos 1 again

  Run r(base_config(), f.wire);

  CHECK_EQ(r.s.groups, 2ull);
  CHECK_EQ(r.s.generated_order_count, 1ull);
  CHECK_EQ(r.log[0].timestamp,
           (Timestamp)(FEED_SECOND + 1) * 1000000000ull + 1ull);
}
