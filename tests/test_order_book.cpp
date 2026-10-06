#include "test_framework.hpp"
#include "book/order_book.hpp"

using namespace llob;

namespace {

Config smallConfig() {
    Config c;
    c.target_order_book_id = 73616;
    c.tick_size            = 1;
    c.order_quantity       = 100;
    c.minimum_position     = -1000;
    c.maximum_position     = 1000;
    c.max_active_orders    = 64;
    c.max_price_levels     = 1024;
    return c;
}

AddOrderMsg add(OrderId id, Side side, Quantity qty, Price price) {
    AddOrderMsg m; m.nanos = 0; m.order_id = id; m.side = side;
    m.quantity = qty; m.price = price; return m;
}
OrderExecutedMsg exec(OrderId id, Side side, Quantity qty) {
    OrderExecutedMsg m; m.nanos = 0; m.order_id = id; m.side = side;
    m.quantity = qty; return m;
}
OrderDeleteMsg del(OrderId id, Side side) {
    OrderDeleteMsg m; m.nanos = 0; m.order_id = id; m.side = side; return m;
}
BookStateMsg st(BookState s) {
    BookStateMsg m; m.nanos = 0; m.state = s; return m;
}

} // namespace

TEST(book_add_creates_a_level_and_sets_the_best) {
  OrderBook b(smallConfig());
  b.add_order(add(1, SIDE_BUY, 500, 1049));

  CHECK_EQ(b.live_orders(), 1);
  CHECK_EQ(b.quantity_at(SIDE_BUY, 1049), 500ull);
  CHECK_EQ(b.order_count_at(SIDE_BUY, 1049), 1u);
  CHECK(b.has_best(SIDE_BUY));
  CHECK_EQ(b.best(SIDE_BUY), 1049);
  CHECK_EQ(b.best_quantity(SIDE_BUY), 500ull);
  CHECK(b.healthy());
}

TEST(book_multiple_adds_at_one_price_aggregate) {
  OrderBook b(smallConfig());
  b.add_order(add(1, SIDE_BUY, 500, 1049));
  b.add_order(add(2, SIDE_BUY, 300, 1049));
  b.add_order(add(3, SIDE_BUY, 400, 1049));

  CHECK_EQ(b.quantity_at(SIDE_BUY, 1049), 1200ull);
  CHECK_EQ(b.order_count_at(SIDE_BUY, 1049), 3u);
  CHECK_EQ(b.live_orders(), 3);
}

TEST(book_partial_execution_keeps_the_order_on_the_level) {
  OrderBook b(smallConfig());
  b.add_order(add(1, SIDE_BUY, 500, 1049));
  b.add_order(add(2, SIDE_BUY, 300, 1049));

  b.order_executed(exec(1, SIDE_BUY, 200));

  CHECK_EQ(b.quantity_at(SIDE_BUY, 1049), 600ull);
  CHECK_EQ(b.order_count_at(SIDE_BUY, 1049), 2u);   // order 1 is still resting
  CHECK_EQ(b.live_orders(), 2);
  CHECK_EQ(b.stats().partial_executions, 1ull);
}

TEST(book_full_execution_removes_the_order_without_a_delete) {
  // No Order Delete follows a complete fill. If the book waited for one it
  // would keep a ghost order forever.
  OrderBook b(smallConfig());
  b.add_order(add(1, SIDE_BUY, 500, 1049));
  b.add_order(add(2, SIDE_BUY, 300, 1049));

  b.order_executed(exec(1, SIDE_BUY, 500));

  CHECK_EQ(b.quantity_at(SIDE_BUY, 1049), 300ull);
  CHECK_EQ(b.order_count_at(SIDE_BUY, 1049), 1u);
  CHECK_EQ(b.live_orders(), 1);
  CHECK_EQ(b.stats().partial_executions, 0ull);
}

TEST(book_execution_in_two_steps_ends_with_the_order_gone) {
  OrderBook b(smallConfig());
  b.add_order(add(1, SIDE_BUY, 500, 1049));

  b.order_executed(exec(1, SIDE_BUY, 200));
  CHECK_EQ(b.live_orders(), 1);
  b.order_executed(exec(1, SIDE_BUY, 300));

  CHECK_EQ(b.live_orders(), 0);
  CHECK(!b.occupied_at(SIDE_BUY, 1049));
  CHECK(!b.has_best(SIDE_BUY));
}

TEST(book_last_execution_price_comes_from_the_resting_order) {
  // The Order Executed message carries no price of its own.
  OrderBook b(smallConfig());
  CHECK(!b.has_last_execution());

  b.add_order(add(1, SIDE_BUY, 500, 1049));
  b.order_executed(exec(1, SIDE_BUY, 500));

  CHECK(b.has_last_execution());
  CHECK_EQ(b.last_execution_price(), 1049);
}

TEST(book_delete_removes_the_order_and_its_quantity) {
  OrderBook b(smallConfig());
  b.add_order(add(1, SIDE_BUY, 500, 1049));
  b.add_order(add(2, SIDE_BUY, 300, 1049));

  b.order_delete(del(1, SIDE_BUY));

  CHECK_EQ(b.quantity_at(SIDE_BUY, 1049), 300ull);
  CHECK_EQ(b.order_count_at(SIDE_BUY, 1049), 1u);
  CHECK_EQ(b.live_orders(), 1);
}

TEST(book_best_falls_back_when_the_top_level_is_emptied) {
  OrderBook b(smallConfig());
  b.add_order(add(1, SIDE_BUY, 100, 1047));
  b.add_order(add(2, SIDE_BUY, 200, 1048));
  b.add_order(add(3, SIDE_BUY, 300, 1049));
  CHECK_EQ(b.best(SIDE_BUY), 1049);

  b.order_delete(del(3, SIDE_BUY));
  CHECK_EQ(b.best(SIDE_BUY), 1048);

  b.order_executed(exec(2, SIDE_BUY, 200));
  CHECK_EQ(b.best(SIDE_BUY), 1047);
}

TEST(book_keeps_both_sides_and_their_bests) {
  OrderBook b(smallConfig());
  b.add_order(add(1, SIDE_BUY,  500, 1049));
  b.add_order(add(2, SIDE_SELL, 300, 1051));

  CHECK_EQ(b.best(SIDE_BUY), 1049);
  CHECK_EQ(b.best(SIDE_SELL), 1051);
  CHECK(b.invariants_hold());
}

TEST(book_allows_the_same_order_id_on_both_sides) {
  // ITCH ids are unique per book and side only.
  OrderBook b(smallConfig());
  b.add_order(add(7, SIDE_BUY,  500, 1049));
  b.add_order(add(7, SIDE_SELL, 300, 1051));

  CHECK_EQ(b.live_orders(), 2);
  CHECK_EQ(b.stats().duplicate_order_ids, 0ull);

  b.order_delete(del(7, SIDE_BUY));

  CHECK_EQ(b.live_orders(), 1);
  CHECK(!b.has_best(SIDE_BUY));
  CHECK_EQ(b.best(SIDE_SELL), 1051);
}

TEST(book_ignores_a_duplicate_id_and_keeps_the_resting_order) {
  OrderBook b(smallConfig());
  b.add_order(add(1, SIDE_BUY, 500, 1049));
  b.add_order(add(1, SIDE_BUY, 999, 1040));     // same id, same side

  CHECK_EQ(b.stats().duplicate_order_ids, 1ull);
  CHECK_EQ(b.live_orders(), 1);
  CHECK_EQ(b.quantity_at(SIDE_BUY, 1049), 500ull);
  CHECK_EQ(b.quantity_at(SIDE_BUY, 1040), 0ull);   // the second never landed
  CHECK(b.healthy());                              // not an error, just ignored
}

TEST(book_counts_an_execution_for_an_order_it_never_saw) {
  OrderBook b(smallConfig());
  b.order_executed(exec(99, SIDE_BUY, 100));
  b.order_delete(del(99, SIDE_BUY));

  CHECK_EQ(b.stats().unknown_order_ids, 2ull);
  CHECK_EQ(b.live_orders(), 0);
  CHECK(b.healthy());
  CHECK(!b.has_last_execution());   // nothing was executed
}

TEST(book_skips_a_market_order_instead_of_placing_it) {
  // The price sentinel is INT32_MIN; indexing a level with it would run far
  // outside the band.
  OrderBook b(smallConfig());
  b.add_order(add(1, SIDE_BUY, 500, 1049));
  b.add_order(add(2, SIDE_BUY, 300, PRICE_NOT_AVAILABLE));

  CHECK_EQ(b.stats().market_orders, 1ull);
  CHECK_EQ(b.live_orders(), 1);
  CHECK_EQ(b.best(SIDE_BUY), 1049);
  CHECK(b.healthy());
}

TEST(book_keeps_an_undisclosed_order_on_its_level) {
  OrderBook b(smallConfig());
  b.add_order(add(1, SIDE_BUY, 0, 1049));      // visible quantity zero

  CHECK_EQ(b.quantity_at(SIDE_BUY, 1049), 0ull);
  CHECK_EQ(b.order_count_at(SIDE_BUY, 1049), 1u);
  CHECK(b.occupied_at(SIDE_BUY, 1049));
  CHECK_EQ(b.best(SIDE_BUY), 1049);
}

TEST(book_clamps_an_execution_larger_than_what_is_left) {
  OrderBook b(smallConfig());
  b.add_order(add(1, SIDE_BUY, 500, 1049));

  b.order_executed(exec(1, SIDE_BUY, 900));    // more than the order holds

  CHECK_EQ(b.stats().oversized_executions, 1ull);
  CHECK_EQ(b.live_orders(), 0);
  CHECK_EQ(b.quantity_at(SIDE_BUY, 1049), 0ull);
  CHECK(!b.has_best(SIDE_BUY));                // no negative quantity left behind
}

TEST(book_stops_being_healthy_when_the_pool_fills) {
  Config c = smallConfig();
  c.max_active_orders = 4;
  OrderBook b(c);

  for (OrderId i = 1; i <= 4; ++i) b.add_order(add(i, SIDE_BUY, 10, 1000 + (Price)i));
  CHECK(b.healthy());

  b.add_order(add(5, SIDE_BUY, 10, 1005));

  CHECK(!b.healthy());
  CHECK_EQ(b.live_orders(), 4);     // nothing was overwritten
}

TEST(book_stops_being_healthy_on_a_price_outside_the_band) {
  Config c = smallConfig();
  c.max_price_levels = 128;
  OrderBook b(c);

  b.add_order(add(1, SIDE_BUY, 10, 10000));     // band is 9936 .. 10063
  CHECK(b.healthy());

  b.add_order(add(2, SIDE_BUY, 10, 99999));

  CHECK(!b.healthy());
  CHECK(!b.invariants_hold());
  CHECK_EQ(b.live_orders(), 1);     // the failed add did not stay in the pool
}

TEST(book_tracks_state_and_the_end_of_day) {
  OrderBook b(smallConfig());
  CHECK_EQ(b.state(), STATE_UNKNOWN);
  CHECK(!b.day_closed());

  b.book_state(st(STATE_SUREKLI_ISLEM));
  CHECK_EQ(b.state(), STATE_SUREKLI_ISLEM);
  CHECK(!b.day_closed());

  b.book_state(st(STATE_MARJ_YAYIN_KAPANIS));
  CHECK(b.day_closed());

  b.book_state(st(STATE_OTHER));
  CHECK_EQ(b.state(), STATE_OTHER);
  CHECK(b.day_closed());            // the close does not get undone
}

TEST(book_invariants_catch_a_crossed_book) {
  OrderBook b(smallConfig());
  b.add_order(add(1, SIDE_BUY,  100, 1050));
  b.add_order(add(2, SIDE_SELL, 100, 1049));    // ask below bid

  CHECK(!b.invariants_hold());
}

TEST(book_level_sums_stay_consistent_through_a_long_sequence) {
  OrderBook b(smallConfig());

  for (OrderId i = 1; i <= 20; ++i)
    b.add_order(add(i, (i % 2) ? SIDE_BUY : SIDE_SELL,
                    i * 10, (Price)(1040 + (i % 7))));
  CHECK(b.verify_level_sums());

  for (OrderId i = 1; i <= 20; i += 3) b.order_executed(exec(i, (i % 2) ? SIDE_BUY : SIDE_SELL, 5));
  CHECK(b.verify_level_sums());

  for (OrderId i = 2; i <= 20; i += 4) b.order_delete(del(i, (i % 2) ? SIDE_BUY : SIDE_SELL));
  CHECK(b.verify_level_sums());

  CHECK(b.healthy());
}
