#include "test_framework.hpp"
#include "app/engine.hpp"
#include "strategy/gap_strategy.hpp"

#include <vector>

using namespace llob;

namespace {

struct Recorder {
    std::vector<OrderIntent> intents;
    void on_order_intent(const OrderIntent& i) { intents.push_back(i); }
};

// Strategy'nin ne zaman sorulduğunu ve o anda book'un nasıl göründüğünü
// kaydediyor; böylece grouping, gerçek kurallar araya girmeden kontrol
// edilebiliyor.
struct SpyStrategy {
    struct Call { Timestamp ts; bool sequence_ok; Position position;
                  bool has_bid; Price bid; };
    std::vector<Call> calls;

    void evaluate(const OrderBook& book, bool sequence_ok,
                  Position position, Timestamp ts) {
        Call c;
        c.ts = ts; c.sequence_ok = sequence_ok; c.position = position;
        c.has_bid = book.has_best(SIDE_BUY);
        c.bid = c.has_bid ? book.best(SIDE_BUY) : 0;
        calls.push_back(c);
    }
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

} // namespace

TEST(engine_runs_the_strategy_once_per_group_not_once_per_message) {
  Config c = cfg(); OrderBook book(c); SpyStrategy spy; Position pos = 0;
  Engine<SpyStrategy> e(book, spy, pos);

  e.on_add_order(100, add(1, SIDE_BUY,  10, 1049));   // group 100
  e.on_add_order(100, add(2, SIDE_SELL, 10, 1051));
  e.on_add_order(100, add(3, SIDE_SELL, 10, 1052));
  CHECK_EQ(spy.calls.size(), 0);                      // group hâlâ açık

  e.on_add_order(200, add(4, SIDE_BUY, 10, 1048));    // group 200 açılıyor
  REQUIRE(spy.calls.size() == 1);
  CHECK_EQ(spy.calls[0].ts, 100ull);                  // kapanan group
}

TEST(engine_closes_a_group_before_applying_the_message_that_ends_it) {
  // Yeni message bir sonraki group'a ait. Önce uygulansaydı strategy onu,
  // kapatmakta olduğu group'un parçası olarak görürdü.
  Config c = cfg(); OrderBook book(c); SpyStrategy spy; Position pos = 0;
  Engine<SpyStrategy> e(book, spy, pos);

  e.on_add_order(100, add(1, SIDE_BUY, 10, 1049));
  e.on_add_order(200, add(2, SIDE_BUY, 10, 1060));    // çok daha iyi bir bid

  REQUIRE(spy.calls.size() == 1);
  CHECK(spy.calls[0].has_bid);
  CHECK_EQ(spy.calls[0].bid, 1049);                   // 1060 değil
}

TEST(engine_finalize_closes_the_last_group) {
  Config c = cfg(); OrderBook book(c); SpyStrategy spy; Position pos = 0;
  Engine<SpyStrategy> e(book, spy, pos);

  e.on_add_order(100, add(1, SIDE_BUY, 10, 1049));
  e.on_add_order(200, add(2, SIDE_BUY, 10, 1050));
  CHECK_EQ(spy.calls.size(), 1);                      // şimdilik sadece group 100

  e.finalize();

  REQUIRE(spy.calls.size() == 2);
  CHECK_EQ(spy.calls[1].ts, 200ull);
  CHECK_EQ(e.stats().groups, 2ull);
}

TEST(engine_finalize_is_safe_to_call_twice_or_on_nothing) {
  Config c = cfg(); OrderBook book(c); SpyStrategy spy; Position pos = 0;
  Engine<SpyStrategy> e(book, spy, pos);

  e.finalize();                                       // hiç veri verilmedi
  CHECK_EQ(spy.calls.size(), 0);

  e.on_add_order(100, add(1, SIDE_BUY, 10, 1049));
  e.finalize();
  e.finalize();
  CHECK_EQ(spy.calls.size(), 1);                      // iki kez değil
}

TEST(engine_treats_a_state_message_as_part_of_the_timeline) {
  Config c = cfg(); OrderBook book(c); SpyStrategy spy; Position pos = 0;
  Engine<SpyStrategy> e(book, spy, pos);

  e.on_add_order(100, add(1, SIDE_BUY, 10, 1049));
  e.on_book_state(200, st(STATE_SUREKLI_ISLEM));      // farklı timestamp

  CHECK_EQ(spy.calls.size(), 1);
  CHECK_EQ(spy.calls[0].ts, 100ull);
}

TEST(engine_passes_the_live_position_through) {
  Config c = cfg(); OrderBook book(c); SpyStrategy spy; Position pos = 0;
  Engine<SpyStrategy> e(book, spy, pos);

  e.on_add_order(100, add(1, SIDE_BUY, 10, 1049));
  pos = 350;                                          // bir fill'in yapacağı gibi
  e.on_add_order(200, add(2, SIDE_BUY, 10, 1048));

  REQUIRE(spy.calls.size() == 1);
  CHECK_EQ(spy.calls[0].position, 350);               // okunuyor, erkenden kopyalanmıyor
}

TEST(engine_remembers_a_sequence_gap_until_a_session_restores_it) {
  Config c = cfg(); OrderBook book(c); SpyStrategy spy; Position pos = 0;
  Engine<SpyStrategy> e(book, spy, pos);

  CHECK(e.sequence_ok());
  e.on_add_order(100, add(1, SIDE_BUY, 10, 1049));

  e.on_sequence_reliable(false);                      // framing layer'ı bir sequence gap gördü
  e.on_add_order(200, add(2, SIDE_BUY, 10, 1048));

  REQUIRE(spy.calls.size() == 1);
  CHECK(!spy.calls[0].sequence_ok);
  CHECK(!e.sequence_ok());
}

TEST(engine_counts_a_group_that_ends_crossed) {
  Config c = cfg(); OrderBook book(c); SpyStrategy spy; Position pos = 0;
  Engine<SpyStrategy> e(book, spy, pos);

  e.on_add_order(100, add(1, SIDE_BUY,  10, 1050));
  e.on_add_order(100, add(2, SIDE_SELL, 10, 1049));   // ask, bid'in altında
  e.finalize();

  CHECK_EQ(e.stats().invariant_breaks, 1ull);
}

TEST(engine_does_not_complain_about_a_cross_inside_a_group) {
  // Bir price'ı bir side'dan diğerine taşımak iki message. Aralarında book cross
  // oluyor, ve bu normal.
  Config c = cfg(); OrderBook book(c); SpyStrategy spy; Position pos = 0;
  Engine<SpyStrategy> e(book, spy, pos);

  e.on_add_order(100, add(1, SIDE_BUY,  10, 1048));
  e.on_add_order(100, add(2, SIDE_BUY,  10, 1049));
  e.on_add_order(100, add(3, SIDE_SELL, 10, 1051));

  e.on_add_order   (200, add(4, SIDE_SELL, 10, 1049));  // cross: bid 1049, ask 1049
  e.on_order_delete(200, del(2, SIDE_BUY));             // cross çözüldü: bid 1048, ask 1049
  e.finalize();

  CHECK_EQ(e.stats().groups, 2ull);
  CHECK_EQ(e.stats().invariant_breaks, 0ull);         // iki group da temiz bitti
}

TEST(engine_drives_the_real_strategy_end_to_end) {
  Config c = cfg();
  OrderBook book(c);
  Recorder sink;
  GapStrategy<Recorder> strat(c, sink);
  Position pos = 0;
  Engine<GapStrategy<Recorder> > e(book, strat, pos);

  e.on_book_state(10, st(STATE_SUREKLI_ISLEM));
  e.on_add_order (20, add(1, SIDE_BUY,  1200, 1049));
  e.on_add_order (20, add(2, SIDE_SELL,  600, 1050));
  e.on_add_order (20, add(3, SIDE_SELL,  800, 1051));

  e.on_order_delete(30, del(2, SIDE_SELL));   // 1050, group 30'da boşalıyor
  e.finalize();

  REQUIRE(sink.intents.size() == 1);
  CHECK_EQ(sink.intents[0].side, SIDE_SELL);
  CHECK_EQ(sink.intents[0].price, 1050);
  CHECK_EQ(sink.intents[0].timestamp, 30ull);
  CHECK_EQ(e.stats().groups, 3ull);
}

TEST(engine_counts_only_target_messages_it_was_given) {
  Config c = cfg(); OrderBook book(c); SpyStrategy spy; Position pos = 0;
  Engine<SpyStrategy> e(book, spy, pos);

  e.on_add_order(100, add(1, SIDE_BUY, 10, 1049));
  e.on_add_order(100, add(2, SIDE_BUY, 10, 1048));
  e.on_order_delete(100, del(1, SIDE_BUY));
  e.finalize();

  CHECK_EQ(e.stats().messages, 3ull);
  CHECK_EQ(e.stats().groups, 1ull);
}
