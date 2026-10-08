// Risk ve P&L testleri, uçtan uca.
//
// Assignment'ın saydığı senaryolar: limite yaklaşırken quantity'nin kısılması,
// limitte hiç order üretilmemesi, BUY cash flow, SELL cash flow, gün sonu mark
// to market. test_position.cpp aritmetiği doğrudan PositionTracker'a karşı
// kontrol ediyor. Burada ise order strategy'den çıkıyor, fill model tarafından
// fill ediliyor ve raporlanan özete düşüyor - değerlendirmeyi yapan kişinin
// gerçekten okuyacağı sayı o.

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
  CHECK_EQ(r.s.cash_flow, 105000);             // 100 x 1050 içeri girdi
  CHECK_EQ(r.s.fills, 1ull);
}

TEST(pnl_e2e_a_buy_pays_out_and_goes_long) {
  Feed f;
  open_the_book_for_a_bid_gap(f);
  f.packet({ deleted(2, 2, TARGET_BOOK, SIDE_BUY) });

  Run r(base_config(), f.wire);

  REQUIRE(r.s.generated_order_count == 1ull);
  CHECK_EQ(r.s.position, 100);
  CHECK_EQ(r.s.cash_flow, -104900);            // 100 x 1049 dışarı çıktı
}

TEST(pnl_e2e_the_quantity_is_cut_down_to_what_the_limit_allows) {
  // Config'deki order boyutu 100 ama sadece 50 yer kalmış. 100 gönderip
  // position'ın limiti aşmasına izin vermek seçenek değil, hiçbir şey
  // göndermemek ise izin verilen yarım işlemi çöpe atmak olur.
  Config c = base_config();
  c.maximum_position = 50;

  Feed f;
  open_the_book_for_a_bid_gap(f);
  f.packet({ deleted(2, 2, TARGET_BOOK, SIDE_BUY) });

  Run r(c, f.wire);

  REQUIRE(r.log.size() == 1);
  CHECK_EQ(r.log[0].quantity, 50ull);          // kısıldı, reddedilmedi
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
  // 1050'den 100 sat, sonra piyasa 1051'den işlem görsün, sonra kapanış gelsin.
  // Daha yüksek bir price'ta short olmak zarar demek, ve bunun görünmesi
  // gerekiyor.
  Feed f;
  open_the_book(f);
  f.packet({ deleted(2, 2, TARGET_BOOK, SIDE_SELL) });              // 1050'den 100 sat
  f.packet({ executed(3, 3, TARGET_BOOK, SIDE_SELL, 100, 1, 0) });  // piyasa 1051'den işlem gördü
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
  // Bu book'ta hiç execution olmadı, yani position'ı değerleyecek bir price
  // yok. 0 raporlamak "başa baş" diye okunurdu, ki bu "bilinmiyor"dan bambaşka
  // bir iddia.
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
  // Hiç signal üretilmedi, yani position flat. Flat bir book piyasa ne yaparsa
  // yapsın kendi cash flow'u kadar ediyor, yani hiç execution price'ı olmasa
  // bile P&L biliniyor.
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
  // Benchmark'ın iterasyonlar arasında karşılaştırdığı şey checksum. Aynı
  // byte'lar farklı bir özet üretseydi, bir replay'den alınan her performans
  // sayısı anlamsız olurdu.
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
