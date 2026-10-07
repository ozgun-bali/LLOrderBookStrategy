#include "test_framework.hpp"
#include "strategy/gap_strategy.hpp"

#include <vector>

using namespace llob;

namespace {

struct Recorder {
    std::vector<OrderIntent> intents;
    void on_order_intent(const OrderIntent& i) { intents.push_back(i); }
};

Config cfg() {
    Config c;
    c.target_order_book_id = 73616;
    c.tick_size            = 1;
    c.order_quantity       = 100;
    c.minimum_position     = -1000;
    c.maximum_position     = 1000;
    c.max_active_orders    = 256;
    c.max_price_levels     = 1024;
    return c;
}

AddOrderMsg add(OrderId id, Side s, Quantity q, Price p) {
    AddOrderMsg m; m.nanos = 0; m.order_id = id; m.side = s;
    m.quantity = q; m.price = p; return m;
}
OrderDeleteMsg del(OrderId id, Side s) {
    OrderDeleteMsg m; m.nanos = 0; m.order_id = id; m.side = s; return m;
}
BookStateMsg st(BookState s) {
    BookStateMsg m; m.nanos = 0; m.state = s; return m;
}

// A book in continuous trading with bid 1049, ask 1050, ask 1051.
// Deleting the ask at 1050 opens the one-tick gap the strategy looks for.
struct Fixture {
    Config      c;
    OrderBook   book;
    Recorder    sink;
    GapStrategy<Recorder> strat;

    Fixture() : c(cfg()), book(c), sink(), strat(c, sink) {
        book.book_state(st(STATE_SUREKLI_ISLEM));
        book.add_order(add(1, SIDE_BUY,  1200, 1049));
        book.add_order(add(2, SIDE_SELL,  600, 1050));
        book.add_order(add(3, SIDE_SELL,  800, 1051));
        endGroup();
    }

    void endGroup() { strat.evaluate(book, true, 0, 1000); book.begin_group(); }
    void endGroup(Position pos) { strat.evaluate(book, true, pos, 1000); book.begin_group(); }
    void endGroupUnreliable() { strat.evaluate(book, false, 0, 1000); book.begin_group(); }
};

} // namespace

TEST(strategy_signals_a_sell_when_an_ask_level_vanishes) {
  Fixture f;
  CHECK_EQ(f.sink.intents.size(), 0);

  f.book.order_delete(del(2, SIDE_SELL));     // 1050 empties
  f.endGroup();

  REQUIRE(f.sink.intents.size() == 1);
  const OrderIntent& i = f.sink.intents[0];
  CHECK_EQ(i.side, SIDE_SELL);                // refill the side that emptied
  CHECK_EQ(i.price, 1050);
  CHECK_EQ(i.quantity, 100ull);
  CHECK_EQ(i.reason, REASON_GAP_WAS_ASK);
  CHECK_EQ(i.timestamp, 1000ull);
}

TEST(strategy_signals_a_buy_when_a_bid_level_vanishes) {
  Config c = cfg();
  OrderBook book(c);
  Recorder sink;
  GapStrategy<Recorder> strat(c, sink);

  book.book_state(st(STATE_SUREKLI_ISLEM));
  book.add_order(add(1, SIDE_BUY,  500, 1048));
  book.add_order(add(2, SIDE_BUY,  600, 1049));   // this one will go
  book.add_order(add(3, SIDE_SELL, 800, 1050));
  strat.evaluate(book, true, 0, 1); book.begin_group();

  book.order_delete(del(2, SIDE_BUY));            // 1049 empties
  strat.evaluate(book, true, 0, 2);

  REQUIRE(sink.intents.size() == 1);
  CHECK_EQ(sink.intents[0].side, SIDE_BUY);
  CHECK_EQ(sink.intents[0].price, 1049);
  CHECK_EQ(sink.intents[0].reason, REASON_GAP_WAS_BID);
}

TEST(strategy_ignores_a_gap_that_was_already_open) {
  // Rule 5. The level did not disappear during this group, so the chance to
  // be first at it has long gone.
  Fixture f;
  f.book.order_delete(del(2, SIDE_SELL));
  f.endGroup();
  CHECK_EQ(f.sink.intents.size(), 1);

  f.endGroup();                                 // a later group, nothing touched
  f.endGroup();

  CHECK_EQ(f.sink.intents.size(), 1);           // still just the one
  CHECK(f.strat.stats().skip_not_this_group >= 2);
}

TEST(strategy_ignores_a_price_that_changed_sides_in_the_group) {
  // Rule 6. The ask at 1050 goes, a bid appears at 1050, and that goes too.
  // The level ends up empty, but the market moved through it.
  Fixture f;
  f.book.order_delete(del(2, SIDE_SELL));       // ask at 1050 gone
  f.book.add_order(add(9, SIDE_BUY, 300, 1050));
  f.book.order_delete(del(9, SIDE_BUY));        // and the bid too
  f.endGroup();

  CHECK_EQ(f.sink.intents.size(), 0);
  CHECK_EQ(f.strat.stats().skip_flip, 1ull);
}

TEST(strategy_sends_only_one_order_while_the_hole_churns) {
  // Rule 7's requirement: one order per gap. What actually stops the second
  // one here is the direction check, not the duplicate memory. At the start of
  // this group 1050 was already empty, so nothing disappeared from it and
  // there is no side to refill. Rule 5 and the direction rule between them
  // cover every repeat; the duplicate memory is a backstop behind both.
  Fixture f;
  f.book.order_delete(del(2, SIDE_SELL));
  f.endGroup();
  CHECK_EQ(f.sink.intents.size(), 1);

  f.book.add_order(add(10, SIDE_SELL, 100, 1050));
  f.book.order_delete(del(10, SIDE_SELL));      // filled and emptied in one group
  f.endGroup();

  CHECK_EQ(f.sink.intents.size(), 1);
  CHECK_EQ(f.strat.stats().skip_no_direction, 1ull);
}

TEST(strategy_signals_again_once_the_gap_really_closes_and_reopens) {
  Fixture f;
  f.book.order_delete(del(2, SIDE_SELL));
  f.endGroup();
  CHECK_EQ(f.sink.intents.size(), 1);

  f.book.add_order(add(10, SIDE_SELL, 100, 1050));   // gap closes for real
  f.endGroup();

  f.book.order_delete(del(10, SIDE_SELL));           // and opens again
  f.endGroup();

  CHECK_EQ(f.sink.intents.size(), 2);                // a new opportunity
  CHECK_EQ(f.sink.intents[1].price, 1050);
}

TEST(strategy_stays_quiet_outside_continuous_trading) {
  Fixture f;
  f.book.book_state(st(STATE_OTHER));
  f.book.order_delete(del(2, SIDE_SELL));
  f.endGroup();

  CHECK_EQ(f.sink.intents.size(), 0);
  CHECK(f.strat.stats().skip_state >= 1);
}

TEST(strategy_stays_quiet_after_the_day_closes) {
  Fixture f;
  f.book.book_state(st(STATE_MARJ_YAYIN_KAPANIS));
  f.book.book_state(st(STATE_SUREKLI_ISLEM));   // even if the state comes back
  f.book.order_delete(del(2, SIDE_SELL));
  f.endGroup();

  CHECK_EQ(f.sink.intents.size(), 0);
}

TEST(strategy_stays_quiet_when_the_sequence_stream_is_broken) {
  // A book built on missed messages is fiction, and the gap may not be real.
  Fixture f;
  f.book.order_delete(del(2, SIDE_SELL));
  f.endGroupUnreliable();

  CHECK_EQ(f.sink.intents.size(), 0);
  CHECK_EQ(f.strat.stats().skip_sequence, 1ull);
}

TEST(strategy_needs_a_level_on_both_sides) {
  Config c = cfg();
  OrderBook book(c);
  Recorder sink;
  GapStrategy<Recorder> strat(c, sink);

  book.book_state(st(STATE_SUREKLI_ISLEM));
  book.add_order(add(1, SIDE_SELL, 600, 1050));
  book.add_order(add(2, SIDE_SELL, 800, 1051));
  book.order_delete(del(1, SIDE_SELL));         // 1050 empty, but no bids at all
  strat.evaluate(book, true, 0, 1);

  CHECK_EQ(sink.intents.size(), 0);
  CHECK_EQ(strat.stats().skip_one_sided, 1ull);
}

TEST(strategy_ignores_a_spread_wider_than_one_tick) {
  Config c = cfg();
  OrderBook book(c);
  Recorder sink;
  GapStrategy<Recorder> strat(c, sink);

  book.book_state(st(STATE_SUREKLI_ISLEM));
  book.add_order(add(1, SIDE_BUY,  500, 1049));
  book.add_order(add(2, SIDE_SELL, 600, 1052));   // three ticks apart
  book.add_order(add(3, SIDE_SELL, 600, 1053));
  strat.evaluate(book, true, 0, 1); book.begin_group();

  book.order_delete(del(2, SIDE_SELL));
  strat.evaluate(book, true, 0, 2);

  CHECK_EQ(sink.intents.size(), 0);
  CHECK(strat.stats().skip_spread >= 1);
}

TEST(strategy_cuts_the_quantity_down_to_the_position_limit) {
  Fixture f;
  f.book.order_delete(del(2, SIDE_SELL));
  f.endGroup(-924);                 // short 924, floor is -1000, so 76 left

  REQUIRE(f.sink.intents.size() == 1);
  CHECK_EQ(f.sink.intents[0].side, SIDE_SELL);
  CHECK_EQ(f.sink.intents[0].quantity, 76ull);
}

TEST(strategy_sends_nothing_when_the_limit_is_already_reached) {
  Fixture f;
  f.book.order_delete(del(2, SIDE_SELL));
  f.endGroup(-1000);                // exactly at the short limit

  CHECK_EQ(f.sink.intents.size(), 0);
  CHECK_EQ(f.strat.stats().skip_position_limit, 1ull);
}

TEST(strategy_handles_a_negative_position_without_wrapping) {
  // minimum_position is negative, so every step of the room calculation has to
  // stay signed. An unsigned slip here would allow unlimited selling.
  Fixture f;
  f.book.order_delete(del(2, SIDE_SELL));
  f.endGroup(-500);                 // -500 - (-1000) = 500 of room

  REQUIRE(f.sink.intents.size() == 1);
  CHECK_EQ(f.sink.intents[0].quantity, 100ull);   // capped by order_quantity
}

TEST(strategy_runs_once_per_group_not_once_per_message) {
  Fixture f;
  const uint64_t before = f.strat.stats().evaluations;

  f.book.order_delete(del(2, SIDE_SELL));
  f.book.add_order(add(20, SIDE_SELL, 100, 1052));
  f.book.add_order(add(21, SIDE_BUY,  100, 1047));
  f.endGroup();

  CHECK_EQ(f.strat.stats().evaluations, before + 1);
}
