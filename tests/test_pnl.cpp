// Risk and P&L tests, end to end.
//
// The scenarios the assignment names: quantity reduction near the limit, no
// order generated at the limit, BUY cash flow, SELL cash flow, end of day mark
// to market. test_position.cpp checks the arithmetic against PositionTracker
// directly. Here the order comes out of the strategy, is filled by the fill
// model and lands in the reported summary, which is the number a reviewer will
// actually read.

#include "test_framework.hpp"
#include "feed_fixture.hpp"

using namespace llob;
using namespace llobtest;

TEST(pnl_e2e_a_sell_takes_in_cash_and_goes_short) {
  Feed f;
  open_the_book(f);
  f.packet({ deleted(2, 2, TARGET_BOOK, SIDE_SELL) });

  Run r(base_config(), f.wire);

  REQUIRE(r.s.generated_order_count == 1ull);
  CHECK_EQ(r.s.position, -100);
  CHECK_EQ(r.s.cash_flow, 105000);             // 100 x 1050 came in
  CHECK_EQ(r.s.fills, 1ull);
}

TEST(pnl_e2e_a_buy_pays_out_and_goes_long) {
  Feed f;
  open_the_book_for_a_bid_gap(f);
  f.packet({ deleted(2, 2, TARGET_BOOK, SIDE_BUY) });

  Run r(base_config(), f.wire);

  REQUIRE(r.s.generated_order_count == 1ull);
  CHECK_EQ(r.s.position, 100);
  CHECK_EQ(r.s.cash_flow, -104900);            // 100 x 1049 went out
}

TEST(pnl_e2e_the_quantity_is_cut_down_to_what_the_limit_allows) {
  // The configured order size is 100 but only 50 of room is left. Sending 100
  // and letting the position break the limit is not an option, and sending
  // nothing throws away half a trade that is allowed.
  Config c = base_config();
  c.maximum_position = 50;

  Feed f;
  open_the_book_for_a_bid_gap(f);
  f.packet({ deleted(2, 2, TARGET_BOOK, SIDE_BUY) });

  Run r(c, f.wire);

  REQUIRE(r.log.size() == 1);
  CHECK_EQ(r.log[0].quantity, 50ull);          // cut, not refused
  CHECK_EQ(r.s.position, 50);
  CHECK_EQ(r.s.cash_flow, -52450);             // 50 x 1049
}

TEST(pnl_e2e_nothing_is_sent_when_there_is_no_room_left) {
  Config c = base_config();
  c.maximum_position = 0;

  Feed f;
  open_the_book_for_a_bid_gap(f);
  f.packet({ deleted(2, 2, TARGET_BOOK, SIDE_BUY) });

  Run r(c, f.wire);

  CHECK_EQ(r.s.generated_order_count, 0ull);
  CHECK_EQ(r.s.skip_position_limit, 1ull);
  CHECK_EQ(r.s.position, 0);
  CHECK_EQ(r.s.cash_flow, 0);
}

TEST(pnl_e2e_the_day_is_marked_at_the_closing_price) {
  // Sell 100 at 1050, then the market trades at 1051, then the close arrives.
  // Being short at a higher price is a loss, and it has to show.
  Feed f;
  open_the_book(f);
  f.packet({ deleted(2, 2, TARGET_BOOK, SIDE_SELL) });              // sell 100 @ 1050
  f.packet({ executed(3, 3, TARGET_BOOK, SIDE_SELL, 100, 1, 0) });  // market trades @ 1051
  f.packet({ state_close(4) });

  Run r(base_config(), f.wire);

  CHECK_EQ(r.s.position, -100);
  CHECK_EQ(r.s.cash_flow, 105000);
  CHECK(r.s.have_last_execution);
  CHECK_EQ(r.s.last_execution_price, 1051);
  CHECK(r.s.have_mark_price);
  CHECK_EQ(r.s.mark_price, 1051);
  CHECK(r.s.pnl_known);
  CHECK_EQ(r.s.final_pnl, -100);               // 105000 - 100 x 1051
}

TEST(pnl_e2e_an_open_position_with_no_traded_price_has_no_pnl) {
  // Nothing ever executed on this book, so there is no price to value the
  // position at. Reporting 0 would read as break-even, which is a different
  // claim from "not known".
  Feed f;
  open_the_book(f);
  f.packet({ deleted(2, 2, TARGET_BOOK, SIDE_SELL) });

  Run r(base_config(), f.wire);

  CHECK_EQ(r.s.position, -100);
  CHECK(!r.s.have_last_execution);
  CHECK(!r.s.pnl_known);
  CHECK(!r.s.have_mark_price);
}

TEST(pnl_e2e_flat_at_the_close_needs_no_price) {
  // No signal was ever produced, so the position is flat. A flat book is worth
  // its cash flow whatever the market does, so the P&L is known even with no
  // execution price at all.
  Feed f;
  open_the_book(f);
  f.packet({ state_close(2) });

  Run r(base_config(), f.wire);

  CHECK_EQ(r.s.position, 0);
  CHECK_EQ(r.s.cash_flow, 0);
  CHECK(r.s.pnl_known);
  CHECK_EQ(r.s.final_pnl, 0);
}

TEST(pnl_e2e_the_summary_is_byte_for_byte_repeatable) {
  // The checksum is what the benchmark compares across iterations. If the same
  // bytes produced a different summary, every performance number taken from a
  // replay would be meaningless.
  Feed f;
  open_the_book(f);
  f.packet({ deleted(2, 2, TARGET_BOOK, SIDE_SELL) });
  f.packet({ executed(3, 3, TARGET_BOOK, SIDE_SELL, 100, 1, 0) });
  f.packet({ state_close(4) });

  Run a(base_config(), f.wire);
  Run b(base_config(), f.wire);

  CHECK_EQ(a.s.checksum, b.s.checksum);
  CHECK(a.s.checksum != 0ull);
}
