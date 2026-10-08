// Strategy testleri, uçtan uca.
//
// Assignment'ın saydığı senaryolar: tek bir timestamp group'unun içinde açılıp
// kapanan bir gap signal vermemeli, gerçek bir BUY gap'i, gerçek bir SELL gap'i,
// tekrar eden signal'in bastırılması. test_gap_strategy.cpp kuralları elle
// kurulmuş bir book'a karşı kontrol ediyor; burada aynı durumlar wire byte'ları
// kurulup bütün programın içinden geçiriliyor, yani grouping, parsing ve fill
// de işin içinde.

#include "test_framework.hpp"
#include "feed_fixture.hpp"

using namespace llob;
using namespace llobtest;

TEST(strategy_e2e_an_ask_that_vanishes_produces_one_sell) {
  Feed f;
  open_the_book(f);
  f.packet({ deleted(2, 2, TARGET_BOOK, SIDE_SELL) });   // 1050 boşalıyor

  Run r(base_config(), f.wire);

  REQUIRE(r.s.generated_order_count == 1ull);
  REQUIRE(r.log.size() == 1);
  CHECK_EQ(r.log[0].side, SIDE_SELL);          // boşalan side'ı dolduruyoruz
  CHECK_EQ(r.log[0].price, 1050);
  CHECK_EQ(r.log[0].quantity, 100ull);
  CHECK_EQ(r.log[0].reason, REASON_GAP_WAS_ASK);
  CHECK_EQ(r.log[0].timestamp, at(2));         // karara bağlandığı group
  CHECK_EQ(r.s.groups, 2ull);
}

TEST(strategy_e2e_a_bid_that_vanishes_produces_one_buy) {
  Feed f;
  open_the_book_for_a_bid_gap(f);
  f.packet({ deleted(2, 2, TARGET_BOOK, SIDE_BUY) });    // 1049 boşalıyor

  Run r(base_config(), f.wire);

  REQUIRE(r.log.size() == 1);
  CHECK_EQ(r.log[0].side, SIDE_BUY);
  CHECK_EQ(r.log[0].price, 1049);
  CHECK_EQ(r.log[0].reason, REASON_GAP_WAS_BID);
}

TEST(strategy_e2e_a_gap_that_opens_and_closes_in_one_group_is_not_a_signal) {
  // Assignment'ın kendi örneği. Tek bir timestamp'in içinde 1050'deki ask
  // siliniyor ve yerine yenisi konuyor. Group'un sonunda level dolu, yani ortada
  // hiç fırsat olmadı: biri harekete geçebilecek hale geldiğinde o çoktan
  // kaybolmuştu. Strategy'nin message başına değil group başına çalışmasının
  // bütün sebebi bu.
  Feed f;
  open_the_book(f);
  f.packet({ deleted  (2, 2,  TARGET_BOOK, SIDE_SELL),
             add_order(2, 10, TARGET_BOOK, SIDE_SELL, 400, 1050) });

  Run r(base_config(), f.wire);

  CHECK_EQ(r.s.generated_order_count, 0ull);
  CHECK_EQ(r.s.groups, 2ull);                  // group gerçekten yaşandı
  CHECK_EQ(r.s.best_ask, 1050);                // ve level yeniden dolu
  CHECK_EQ(r.s.best_ask_quantity, 400ull);
}

TEST(strategy_e2e_one_order_per_gap_however_long_it_stays_open) {
  // 1050'deki delik sonraki group'larda da duruyor, ama orada ilk olma şansı
  // gitti. İkinci bir order, çoktan geçip gitmiş bir price'ın peşinden koşmak
  // olurdu.
  Feed f;
  open_the_book(f);
  f.packet({ deleted(2, 2, TARGET_BOOK, SIDE_SELL) });        // signal burada
  f.packet({ add_order(3, 11, TARGET_BOOK, SIDE_BUY, 100, 1040) });  // başka yerde
  f.packet({ add_order(4, 12, TARGET_BOOK, SIDE_BUY, 100, 1041) });

  Run r(base_config(), f.wire);

  CHECK_EQ(r.s.generated_order_count, 1ull);
  CHECK_EQ(r.s.groups, 4ull);
  CHECK(r.s.skip_not_this_group >= 2ull);      // ve neden sessiz kaldığını söylüyor
}

TEST(strategy_e2e_nothing_is_sent_outside_continuous_trading) {
  // Aynı gap, ama book hiç açılmamış. Piyasa ve state kuralları her şeyin
  // önünde geliyor.
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
  // İki ask de gidiyor, geriye 1049'a karşı 1052 kalıyor. Aralarında iki boş
  // level var ve ilk olunacak tek bir price yok.
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
