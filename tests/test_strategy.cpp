// Strategy tests, end to end.
//
// The scenarios the assignment names: a transient gap inside one timestamp group
// must not signal, a real BUY gap, a real SELL gap, repeated signal
// suppression. test_gap_strategy.cpp checks the rules against a book built by
// hand; here the same situations are built out of wire bytes and run through the
// whole program, so the grouping, the parsing and the fill all take part.

#include "test_framework.hpp"
#include "feed_fixture.hpp"

using namespace llob;
using namespace llobtest;

TEST(strategy_e2e_an_ask_that_vanishes_produces_one_sell) {
  Feed f;
  open_the_book(f);
  f.packet({ deleted(2, 2, TARGET_BOOK, SIDE_SELL) });   // 1050 empties

  Run r(base_config(), f.wire);

  REQUIRE(r.s.generated_order_count == 1ull);
  REQUIRE(r.log.size() == 1);
  CHECK_EQ(r.log[0].side, SIDE_SELL);          // refill the side that emptied
  CHECK_EQ(r.log[0].price, 1050);
  CHECK_EQ(r.log[0].quantity, 100ull);
  CHECK_EQ(r.log[0].reason, REASON_GAP_WAS_ASK);
  CHECK_EQ(r.log[0].timestamp, at(2));         // the group it was decided in
  CHECK_EQ(r.s.groups, 2ull);
}

TEST(strategy_e2e_a_bid_that_vanishes_produces_one_buy) {
  Feed f;
  open_the_book_for_a_bid_gap(f);
  f.packet({ deleted(2, 2, TARGET_BOOK, SIDE_BUY) });    // 1049 empties

  Run r(base_config(), f.wire);

  REQUIRE(r.log.size() == 1);
  CHECK_EQ(r.log[0].side, SIDE_BUY);
  CHECK_EQ(r.log[0].price, 1049);
  CHECK_EQ(r.log[0].reason, REASON_GAP_WAS_BID);
}

TEST(strategy_e2e_a_gap_that_opens_and_closes_in_one_group_is_not_a_signal) {
  // The assignment's own example. Inside one timestamp the ask at 1050 is
  // deleted and replaced. At the end of the group the level is occupied, so
  // there was never an opportunity: by the time anyone could have acted on it,
  // it was gone. This is the whole reason the strategy runs per group rather
  // than per message.
  Feed f;
  open_the_book(f);
  f.packet({ deleted  (2, 2,  TARGET_BOOK, SIDE_SELL),
             add_order(2, 10, TARGET_BOOK, SIDE_SELL, 400, 1050) });

  Run r(base_config(), f.wire);

  CHECK_EQ(r.s.generated_order_count, 0ull);
  CHECK_EQ(r.s.groups, 2ull);                  // the group did happen
  CHECK_EQ(r.s.best_ask, 1050);                // and the level is full again
  CHECK_EQ(r.s.best_ask_quantity, 400ull);
}

TEST(strategy_e2e_one_order_per_gap_however_long_it_stays_open) {
  // The hole at 1050 is still there in the groups that follow, but the chance
  // to be first at it has gone. A second order would be chasing a price that
  // has already moved on.
  Feed f;
  open_the_book(f);
  f.packet({ deleted(2, 2, TARGET_BOOK, SIDE_SELL) });        // signal here
  f.packet({ add_order(3, 11, TARGET_BOOK, SIDE_BUY, 100, 1040) });  // elsewhere
  f.packet({ add_order(4, 12, TARGET_BOOK, SIDE_BUY, 100, 1041) });

  Run r(base_config(), f.wire);

  CHECK_EQ(r.s.generated_order_count, 1ull);
  CHECK_EQ(r.s.groups, 4ull);
  CHECK(r.s.skip_not_this_group >= 2ull);      // and it says why it kept quiet
}

TEST(strategy_e2e_nothing_is_sent_outside_continuous_trading) {
  // The same gap, with the book never opened. Market and state rules come
  // before everything else.
  Feed f;
  f.packet({ seconds(FEED_SECOND),
             add_order(1, 1, TARGET_BOOK, SIDE_BUY,  1200, 1049),
             add_order(1, 2, TARGET_BOOK, SIDE_SELL,  600, 1050),
             add_order(1, 3, TARGET_BOOK, SIDE_SELL,  800, 1051) });
  f.packet({ deleted(2, 2, TARGET_BOOK, SIDE_SELL) });

  Run r(base_config(), f.wire);

  CHECK_EQ(r.s.generated_order_count, 0ull);
  CHECK(r.s.skip_state >= 1ull);
}

TEST(strategy_e2e_a_two_tick_hole_is_not_a_one_tick_gap) {
  // Both asks go, leaving 1049 against 1052. There are two empty levels
  // between them and no single price to be first at.
  Feed f;
  open_the_book(f);
  f.packet({ add_order(1, 4, TARGET_BOOK, SIDE_SELL, 700, 1052) });
  f.packet({ deleted(2, 2, TARGET_BOOK, SIDE_SELL),
             deleted(2, 3, TARGET_BOOK, SIDE_SELL) });

  Run r(base_config(), f.wire);

  CHECK_EQ(r.s.generated_order_count, 0ull);
  CHECK(r.s.skip_spread >= 1ull);
  CHECK_EQ(r.s.best_ask, 1052);
}
