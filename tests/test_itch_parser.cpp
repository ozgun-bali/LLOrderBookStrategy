#include "test_framework.hpp"
#include "message_builder.hpp"
#include "protocol/itch_parser.hpp"
#include "protocol/mold_udp64.hpp"

#include <vector>

using namespace llob;
using namespace llobtest;

namespace {

const OrderBookId TARGET = 73616u;
const OrderBookId OTHER  = 11111u;

// Stands in for the order book so dispatch can be tested on its own.
struct Sink {
    struct Ev { char kind; Timestamp ts; OrderId id; Side side;
                Quantity qty; Price price; BookState state; };
    std::vector<Ev> ev;

    void on_sequence_reliable(bool) {}

    void on_add_order(Timestamp ts, const AddOrderMsg& m) {
        Ev e; e.kind='A'; e.ts=ts; e.id=m.order_id; e.side=m.side;
        e.qty=m.quantity; e.price=m.price; e.state=STATE_UNKNOWN; ev.push_back(e);
    }
    void on_order_executed(Timestamp ts, const OrderExecutedMsg& m) {
        Ev e; e.kind='E'; e.ts=ts; e.id=m.order_id; e.side=m.side;
        e.qty=m.quantity; e.price=0; e.state=STATE_UNKNOWN; ev.push_back(e);
    }
    void on_order_delete(Timestamp ts, const OrderDeleteMsg& m) {
        Ev e; e.kind='D'; e.ts=ts; e.id=m.order_id; e.side=m.side;
        e.qty=0; e.price=0; e.state=STATE_UNKNOWN; ev.push_back(e);
    }
    void on_book_state(Timestamp ts, const BookStateMsg& m) {
        Ev e; e.kind='O'; e.ts=ts; e.id=0; e.side=SIDE_INVALID;
        e.qty=0; e.price=0; e.state=m.state; ev.push_back(e);
    }
};

void feed(ItchParser<Sink>& p, const Bytes& msg) {
    p.on_message(ByteReader(&msg[0], msg.size()));
}

} // namespace

TEST(parser_dispatches_each_handled_type_once) {
  Sink sink; ItchParser<Sink> p(sink, TARGET);

  feed(p, add_order(10u, 7ull, TARGET, SIDE_BUY, 500ull, 104900));
  feed(p, executed (20u, 7ull, TARGET, SIDE_BUY, 200ull, 1ull, 0u));
  feed(p, deleted  (30u, 7ull, TARGET, SIDE_BUY));
  feed(p, book_state(40u, TARGET, "P_SUREKLI_ISLEM"));

  REQUIRE(sink.ev.size() == 4);
  CHECK_EQ(sink.ev[0].kind, 'A');
  CHECK_EQ(sink.ev[1].kind, 'E');
  CHECK_EQ(sink.ev[2].kind, 'D');
  CHECK_EQ(sink.ev[3].kind, 'O');

  CHECK_EQ(sink.ev[0].id,    7ull);
  CHECK_EQ(sink.ev[0].qty,   500ull);
  CHECK_EQ(sink.ev[0].price, 104900);
  CHECK_EQ(sink.ev[1].qty,   200ull);
  CHECK_EQ(sink.ev[3].state, STATE_SUREKLI_ISLEM);
  CHECK_EQ(p.stats().target_messages, 4ull);
}

TEST(parser_consumes_the_seconds_message_without_passing_it_on) {
  Sink sink; ItchParser<Sink> p(sink, TARGET);

  CHECK(!p.have_second());
  feed(p, seconds(1000u));

  CHECK(p.have_second());
  CHECK_EQ(sink.ev.size(), 0);                  // timekeeping only
  CHECK_EQ(p.stats().seconds_messages, 1ull);
  CHECK_EQ(p.stats().target_messages, 0ull);
}

TEST(parser_composes_the_timestamp_from_seconds_and_nanos) {
  Sink sink; ItchParser<Sink> p(sink, TARGET);

  feed(p, seconds(1000u));
  feed(p, deleted(500u, 1ull, TARGET, SIDE_BUY));

  REQUIRE(sink.ev.size() == 1);
  CHECK_EQ(sink.ev[0].ts, 1000ull * 1000000000ull + 500ull);
}

TEST(parser_separates_identical_nanos_in_different_seconds) {
  // The nanosecond field restarts every second. Grouping on it alone would
  // merge these two into one logical group, and the strategy would run once
  // where it must run twice.
  Sink sink; ItchParser<Sink> p(sink, TARGET);

  feed(p, seconds(1000u));
  feed(p, deleted(500u, 1ull, TARGET, SIDE_BUY));
  feed(p, seconds(1001u));
  feed(p, deleted(500u, 2ull, TARGET, SIDE_BUY));   // same 500 nanos

  REQUIRE(sink.ev.size() == 2);
  CHECK(sink.ev[0].ts != sink.ev[1].ts);
  CHECK_EQ(sink.ev[1].ts - sink.ev[0].ts, 1000000000ull);
}

TEST(parser_drops_messages_belonging_to_another_order_book) {
  Sink sink; ItchParser<Sink> p(sink, TARGET);

  feed(p, add_order(10u, 1ull, OTHER,  SIDE_BUY, 100ull, 100));
  feed(p, executed (11u, 1ull, OTHER,  SIDE_BUY, 100ull, 1ull, 0u));
  feed(p, deleted  (12u, 1ull, OTHER,  SIDE_BUY));
  feed(p, book_state(13u, OTHER, "P_SUREKLI_ISLEM"));
  feed(p, add_order(14u, 2ull, TARGET, SIDE_BUY, 100ull, 100));

  REQUIRE(sink.ev.size() == 1);                 // only the target one
  CHECK_EQ(sink.ev[0].id, 2ull);
  CHECK_EQ(p.stats().messages, 5ull);           // all five were seen
  CHECK_EQ(p.stats().target_messages, 1ull);    // one was ours
}

TEST(parser_filters_book_state_at_its_own_offset) {
  // 'O' carries the book id at offset 5, not 13. Using the wrong offset would
  // read into the state name and drop every state message.
  Sink sink; ItchParser<Sink> p(sink, TARGET);

  feed(p, book_state(1u, TARGET, "P_SUREKLI_ISLEM"));
  feed(p, book_state(2u, OTHER,  "P_SUREKLI_ISLEM"));

  REQUIRE(sink.ev.size() == 1);
  CHECK_EQ(sink.ev[0].state, STATE_SUREKLI_ISLEM);
}

TEST(parser_skips_types_it_does_not_handle) {
  Sink sink; ItchParser<Sink> p(sink, TARGET);

  Bytes c(60, 0); c[0] = 'C';      // Order Executed with Price
  Bytes y(10, 0); y[0] = 'Y';      // Order Book Flush
  Bytes r(50, 0); r[0] = 'R';      // Order Book Directory
  feed(p, c); feed(p, y); feed(p, r);

  CHECK_EQ(sink.ev.size(), 0);
  CHECK_EQ(p.stats().skipped_types, 3ull);
  CHECK_EQ(p.stats().invalid_messages, 0ull);   // skipped is not malformed
}

TEST(parser_rejects_a_handled_type_with_the_wrong_length) {
  Sink sink; ItchParser<Sink> p(sink, TARGET);

  Bytes m = add_order(10u, 1ull, TARGET, SIDE_BUY, 100ull, 100);
  m.resize(40);                                 // 'A' must be exactly 45
  feed(p, m);

  CHECK_EQ(sink.ev.size(), 0);
  CHECK_EQ(p.stats().invalid_messages, 1ull);
}

TEST(parser_rejects_a_side_byte_that_is_neither_b_nor_s) {
  // Side becomes an array index downstream, so a corrupt byte must never reach
  // the order book.
  Sink sink; ItchParser<Sink> p(sink, TARGET);

  Bytes m = add_order(10u, 1ull, TARGET, SIDE_BUY, 100ull, 100);
  m[OFF_ADD_SIDE] = 'X';
  feed(p, m);

  CHECK_EQ(sink.ev.size(), 0);
  CHECK_EQ(p.stats().invalid_messages, 1ull);
  CHECK_EQ(p.stats().target_messages, 1ull);    // it was ours, it was just broken
}

TEST(parser_counts_messages_that_arrive_before_any_seconds_marker) {
  Sink sink; ItchParser<Sink> p(sink, TARGET);

  feed(p, deleted(500u, 1ull, TARGET, SIDE_BUY));   // no 'T' yet

  REQUIRE(sink.ev.size() == 1);
  CHECK_EQ(sink.ev[0].ts, 500ull);              // second treated as 0
  CHECK_EQ(p.stats().before_first_second, 1ull);
  CHECK(!p.have_second());
}

TEST(parser_runs_end_to_end_behind_mold_udp64) {
  Sink sink;
  ItchParser<Sink> parser(sink, TARGET);
  MoldSession<ItchParser<Sink> > mold(parser);

  Bytes wire = serialize(make_packet("BIST01", 1, {
      seconds(1000u),
      add_order(10u, 7ull, TARGET, SIDE_BUY, 500ull, 104900),
      add_order(10u, 8ull, OTHER,  SIDE_BUY, 500ull, 104900),   // other book
      deleted  (20u, 7ull, TARGET, SIDE_BUY)
  }));
  mold.process_datagram(&wire[0], wire.size());

  CHECK_EQ(mold.stats().messages, 4ull);        // four blocks framed
  REQUIRE(sink.ev.size() == 2);                 // two survived the filter
  CHECK_EQ(sink.ev[0].kind, 'A');
  CHECK_EQ(sink.ev[0].ts, 1000ull * 1000000000ull + 10ull);
  CHECK_EQ(sink.ev[1].kind, 'D');
  CHECK_EQ(sink.ev[1].ts, 1000ull * 1000000000ull + 20ull);
  CHECK(mold.reliable());
}
