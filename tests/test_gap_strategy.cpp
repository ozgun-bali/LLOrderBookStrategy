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

// Continuous trading'de bir book: bid 1049, ask 1050, ask 1051.
// 1050'deki ask'i silmek, strategy'nin aradığı tek tick'lik gap'i açıyor.
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

  f.book.order_delete(del(2, SIDE_SELL));     // 1050 boşalıyor
  f.endGroup();

  REQUIRE(f.sink.intents.size() == 1);
  const OrderIntent& i = f.sink.intents[0];
  CHECK_EQ(i.side, SIDE_SELL);                // boşalan side'ı doldur
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
  book.add_order(add(2, SIDE_BUY,  600, 1049));   // gidecek olan bu
  book.add_order(add(3, SIDE_SELL, 800, 1050));
  strat.evaluate(book, true, 0, 1); book.begin_group();

  book.order_delete(del(2, SIDE_BUY));            // 1049 boşalıyor
  strat.evaluate(book, true, 0, 2);

  REQUIRE(sink.intents.size() == 1);
  CHECK_EQ(sink.intents[0].side, SIDE_BUY);
  CHECK_EQ(sink.intents[0].price, 1049);
  CHECK_EQ(sink.intents[0].reason, REASON_GAP_WAS_BID);
}

TEST(strategy_ignores_a_gap_that_was_already_open) {
  // Kural 5. Level bu group sırasında kaybolmadı, yani orada ilk olma şansı
  // çoktan geçti.
  Fixture f;
  f.book.order_delete(del(2, SIDE_SELL));
  f.endGroup();
  CHECK_EQ(f.sink.intents.size(), 1);

  f.endGroup();                                 // sonraki bir group, hiçbir şeye dokunulmadı
  f.endGroup();

  CHECK_EQ(f.sink.intents.size(), 1);           // hâlâ sadece o bir tane
  CHECK(f.strat.stats().skip_not_this_group >= 2);
}

TEST(strategy_ignores_a_price_that_changed_sides_in_the_group) {
  // Kural 6. 1050'deki ask gidiyor, 1050'de bir bid beliriyor, o da gidiyor.
  // Level sonunda boş kalıyor, ama piyasa onun içinden geçip gitti.
  Fixture f;
  f.book.order_delete(del(2, SIDE_SELL));       // 1050'deki ask gitti
  f.book.add_order(add(9, SIDE_BUY, 300, 1050));
  f.book.order_delete(del(9, SIDE_BUY));        // bid de gitti
  f.endGroup();

  CHECK_EQ(f.sink.intents.size(), 0);
  CHECK_EQ(f.strat.stats().skip_flip, 1ull);
}

TEST(strategy_sends_only_one_order_while_the_hole_churns) {
  // Kural 7'nin şartı: gap başına tek order. Ama burada ikinciyi asıl durduran
  // şey duplicate hafızası değil, yön kontrolü. Bu group'un başında 1050 zaten
  // boştu, yani oradan bir şey kaybolmadı ve doldurulacak bir side yok. Kural 5
  // ile yön kuralı ikisi birlikte her tekrarı kapsıyor; duplicate hafızası
  // ikisinin de arkasında duran bir emniyet.
  Fixture f;
  f.book.order_delete(del(2, SIDE_SELL));
  f.endGroup();
  CHECK_EQ(f.sink.intents.size(), 1);

  f.book.add_order(add(10, SIDE_SELL, 100, 1050));
  f.book.order_delete(del(10, SIDE_SELL));      // tek bir group'ta doldu ve boşaldı
  f.endGroup();

  CHECK_EQ(f.sink.intents.size(), 1);
  CHECK_EQ(f.strat.stats().skip_no_direction, 1ull);
}

TEST(strategy_signals_again_once_the_gap_really_closes_and_reopens) {
  Fixture f;
  f.book.order_delete(del(2, SIDE_SELL));
  f.endGroup();
  CHECK_EQ(f.sink.intents.size(), 1);

  f.book.add_order(add(10, SIDE_SELL, 100, 1050));   // gap gerçekten kapanıyor
  f.endGroup();

  f.book.order_delete(del(10, SIDE_SELL));           // ve yeniden açılıyor
  f.endGroup();

  CHECK_EQ(f.sink.intents.size(), 2);                // yeni bir fırsat
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
  f.book.book_state(st(STATE_SUREKLI_ISLEM));   // state geri gelse bile
  f.book.order_delete(del(2, SIDE_SELL));
  f.endGroup();

  CHECK_EQ(f.sink.intents.size(), 0);
}

TEST(strategy_stays_quiet_when_the_sequence_stream_is_broken) {
  // Kaçırılmış message'lar üzerine kurulmuş bir book kurgudur, ve gap gerçek
  // olmayabilir.
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
  book.order_delete(del(1, SIDE_SELL));         // 1050 boş, ama hiç bid yok
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
  book.add_order(add(2, SIDE_SELL, 600, 1052));   // üç tick arayla
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
  f.endGroup(-924);                 // 924 short, taban -1000, yani 76 kaldı

  REQUIRE(f.sink.intents.size() == 1);
  CHECK_EQ(f.sink.intents[0].side, SIDE_SELL);
  CHECK_EQ(f.sink.intents[0].quantity, 76ull);
}

TEST(strategy_sends_nothing_when_the_limit_is_already_reached) {
  Fixture f;
  f.book.order_delete(del(2, SIDE_SELL));
  f.endGroup(-1000);                // tam short limitinde

  CHECK_EQ(f.sink.intents.size(), 0);
  CHECK_EQ(f.strat.stats().skip_position_limit, 1ull);
}

TEST(strategy_handles_a_negative_position_without_wrapping) {
  // minimum_position negatif, yani kalan yer hesabının her adımı signed kalmak
  // zorunda. Buradaki unsigned bir kayma sınırsız satışa izin verirdi.
  Fixture f;
  f.book.order_delete(del(2, SIDE_SELL));
  f.endGroup(-500);                 // -500 - (-1000) = 500 yer

  REQUIRE(f.sink.intents.size() == 1);
  CHECK_EQ(f.sink.intents[0].quantity, 100ull);   // order_quantity ile sınırlandı
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
