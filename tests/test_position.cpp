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

  CHECK_EQ(p.quantity(), -100);          // elimizde olmayanı sattık
  CHECK_EQ(p.cash_flow(), 105000);       // 100 x 1050 içeri girdi
  CHECK_EQ(p.fills(), 1ull);
}

TEST(position_a_buy_pays_out_and_goes_long) {
  PositionTracker p;
  p.apply_fill(SIDE_BUY, 1050, 100);

  CHECK_EQ(p.quantity(), 100);
  CHECK_EQ(p.cash_flow(), -105000);
}

TEST(position_a_round_trip_leaves_the_profit_in_the_cash_flow) {
  // Yüksekten sat, düşükten geri al. Position sıfıra dönüyor ve geriye sadece
  // iki tick'lik kazanç kalıyor.
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

  CHECK_EQ(p.final_pnl(), -200);         // 1050'den alındı, piyasa 1048'de kapandı
}

TEST(position_flat_at_the_close_needs_no_price) {
  PositionTracker p;
  p.apply_fill(SIDE_SELL, 1050, 100);
  p.apply_fill(SIDE_BUY,  1048, 100);
  p.mark_end_of_day(false, 0);           // feed'de hiç işlem olmadı

  CHECK(p.pnl_known());                  // değerlenecek bir şey yok, sonuç cash
  CHECK_EQ(p.final_pnl(), 200);
  CHECK(!p.have_mark_price());
}

TEST(position_open_with_no_traded_price_reports_no_pnl) {
  // Dokümante edilmiş politika: açık bir position ve onu değerleyecek hiçbir şey
  // yoksa dürüst bir cevap da yoktur; uydurmak yerine hiçbiri verilmiyor.
  PositionTracker p;
  p.apply_fill(SIDE_SELL, 1050, 100);
  p.mark_end_of_day(false, 0);

  CHECK(p.day_marked());
  CHECK(!p.pnl_known());
  CHECK(!p.have_mark_price());
}

TEST(position_marks_the_day_only_once) {
  // Kapanış bir kez geliyor. Sonrasındaki hiçbir şey, günü kapanışın hiç
  // görmediği bir price'tan yeniden değerlememeli.
  PositionTracker p;
  p.apply_fill(SIDE_SELL, 1050, 100);

  p.mark_end_of_day(true, 1048);
  CHECK_EQ(p.final_pnl(), 200);

  p.mark_end_of_day(true, 900);          // daha sonraki, çok daha düşük bir işlem
  CHECK_EQ(p.final_pnl(), 200);          // değişmedi
  CHECK_EQ(p.mark_price(), 1048);
}

TEST(position_handles_a_large_notional_without_wrapping) {
  // Quantity unsigned, Price signed; genişletmeden çarpmak price'ı promote eder
  // ve büyük bir çarpım saçma bir yere düşerdi.
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
  // Engine bunu bir kez bağlıyor ve her group'ta okuyor, yani tracker'ın
  // anlık kopyasını değil kendisini takip etmek zorunda.
  PositionTracker p;
  const Position& live = p.position_ref();
  CHECK_EQ(live, 0);

  p.apply_fill(SIDE_SELL, 1050, 100);
  CHECK_EQ(live, -100);

  p.apply_fill(SIDE_BUY, 1050, 400);
  CHECK_EQ(live, 300);
}
