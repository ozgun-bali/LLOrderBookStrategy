#include "test_framework.hpp"
#include "sim/position.hpp"
#include "sim/fill_simulator.hpp"

using namespace llob;

namespace {
OrderIntent intent(Side s, Price p, Quantity q) {
    OrderIntent i; i.timestamp = 1; i.side = s; i.price = p; i.quantity = q;
    i.reason = REASON_GAP_WAS_ASK; return i;
}
}

TEST(position_starts_flat) {
  PositionTracker p;
  CHECK_EQ(p.quantity(), 0);
  CHECK_EQ(p.cash_flow(), 0);
  CHECK_EQ(p.fills(), 0ull);
  CHECK(!p.day_marked());
}

TEST(position_a_sell_takes_in_cash_and_goes_short) {
  PositionTracker p;
  p.apply_fill(SIDE_SELL, 1050, 100);

  CHECK_EQ(p.quantity(), -100);          // sold what we did not hold
  CHECK_EQ(p.cash_flow(), 105000);       // 100 x 1050 came in
  CHECK_EQ(p.fills(), 1ull);
}

TEST(position_a_buy_pays_out_and_goes_long) {
  PositionTracker p;
  p.apply_fill(SIDE_BUY, 1050, 100);

  CHECK_EQ(p.quantity(), 100);
  CHECK_EQ(p.cash_flow(), -105000);
}

TEST(position_a_round_trip_leaves_the_profit_in_the_cash_flow) {
  // Sell high, buy back lower. The position returns to zero and the two ticks
  // of edge are the only thing left.
  PositionTracker p;
  p.apply_fill(SIDE_SELL, 1050, 100);
  p.apply_fill(SIDE_BUY,  1048, 100);

  CHECK_EQ(p.quantity(), 0);
  CHECK_EQ(p.cash_flow(), 200);          // 105000 - 104800
}

TEST(position_accumulates_over_many_fills) {
  PositionTracker p;
  p.apply_fill(SIDE_SELL, 1050, 100);    // -100,  +105000
  p.apply_fill(SIDE_SELL, 1060, 300);    // -400,  +318000
  p.apply_fill(SIDE_BUY,  1040, 250);    // -150,  -260000

  CHECK_EQ(p.quantity(), -150);
  CHECK_EQ(p.cash_flow(), 105000 + 318000 - 260000);
  CHECK_EQ(p.fills(), 3ull);
}

TEST(position_marks_a_short_at_the_closing_price) {
  PositionTracker p;
  p.apply_fill(SIDE_SELL, 1050, 100);    // cash +105000, position -100
  p.mark_end_of_day(true, 1048);

  CHECK(p.day_marked());
  CHECK(p.pnl_known());
  CHECK_EQ(p.final_pnl(), 200);          // 105000 + (-100 x 1048)
  CHECK_EQ(p.mark_price(), 1048);
}

TEST(position_marks_a_long_at_the_closing_price) {
  PositionTracker p;
  p.apply_fill(SIDE_BUY, 1050, 100);     // cash -105000, position +100
  p.mark_end_of_day(true, 1048);

  CHECK_EQ(p.final_pnl(), -200);         // bought at 1050, market closed at 1048
}

TEST(position_flat_at_the_close_needs_no_price) {
  PositionTracker p;
  p.apply_fill(SIDE_SELL, 1050, 100);
  p.apply_fill(SIDE_BUY,  1048, 100);
  p.mark_end_of_day(false, 0);           // nothing ever traded on the feed

  CHECK(p.pnl_known());                  // nothing to value, so the cash is it
  CHECK_EQ(p.final_pnl(), 200);
  CHECK(!p.have_mark_price());
}

TEST(position_open_with_no_traded_price_reports_no_pnl) {
  // Documented policy: an open position and nothing to value it at has no
  // honest answer, so none is given rather than inventing one.
  PositionTracker p;
  p.apply_fill(SIDE_SELL, 1050, 100);
  p.mark_end_of_day(false, 0);

  CHECK(p.day_marked());
  CHECK(!p.pnl_known());
  CHECK(!p.have_mark_price());
}

TEST(position_marks_the_day_only_once) {
  // The close arrives once. Anything after it must not re-value the day at a
  // price the close never saw.
  PositionTracker p;
  p.apply_fill(SIDE_SELL, 1050, 100);

  p.mark_end_of_day(true, 1048);
  CHECK_EQ(p.final_pnl(), 200);

  p.mark_end_of_day(true, 900);          // a later, much lower print
  CHECK_EQ(p.final_pnl(), 200);          // unchanged
  CHECK_EQ(p.mark_price(), 1048);
}

TEST(position_handles_a_large_notional_without_wrapping) {
  // Quantity is unsigned and Price is signed; multiplying them unwidened would
  // promote the price and a big product would land somewhere absurd.
  PositionTracker p;
  p.apply_fill(SIDE_BUY, 2000000, 1000000);

  CHECK_EQ(p.quantity(), 1000000);
  CHECK_EQ(p.cash_flow(), -2000000000000LL);
}

TEST(fill_simulator_moves_the_position_and_counts) {
  PositionTracker p;
  FillSimulator f(p);

  f.on_order_intent(intent(SIDE_SELL, 1050, 100));
  f.on_order_intent(intent(SIDE_BUY,  1048, 100));

  CHECK_EQ(f.fills(), 2ull);
  CHECK_EQ(p.fills(), 2ull);
  CHECK_EQ(p.quantity(), 0);
  CHECK_EQ(p.cash_flow(), 200);
}

TEST(position_ref_stays_live_as_fills_arrive) {
  // The engine binds this once and reads it every group, so it has to follow
  // the tracker rather than snapshot it.
  PositionTracker p;
  const Position& live = p.position_ref();
  CHECK_EQ(live, 0);

  p.apply_fill(SIDE_SELL, 1050, 100);
  CHECK_EQ(live, -100);

  p.apply_fill(SIDE_BUY, 1050, 400);
  CHECK_EQ(live, 300);
}
