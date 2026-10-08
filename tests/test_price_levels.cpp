#include "test_framework.hpp"
#include "book/price_levels.hpp"

using namespace llob;

namespace {
const std::size_t LEVELS = 1024;
}

TEST(levels_centres_the_band_on_the_first_price) {
  PriceLevels L(LEVELS);
  CHECK(!L.have_base());

  L.add_order(SIDE_BUY, 10000, 100);

  CHECK(L.have_base());
  CHECK_EQ(L.base_price(), 10000 - (Price)(LEVELS / 2));   // iki yanda da yer var
}

TEST(levels_tracks_quantity_and_count_separately) {
  PriceLevels L(LEVELS);
  L.add_order(SIDE_BUY, 1049, 500);
  L.add_order(SIDE_BUY, 1049, 300);
  L.add_order(SIDE_BUY, 1049, 400);

  CHECK_EQ(L.quantity_at(SIDE_BUY, 1049), 1200ull);
  CHECK_EQ(L.order_count_at(SIDE_BUY, 1049), 3u);
}

TEST(levels_partial_fill_lowers_quantity_but_not_count) {
  // Order hâlâ burada bekliyor, yani hâlâ bir tane sayılıyor.
  PriceLevels L(LEVELS);
  L.add_order(SIDE_BUY, 1049, 500);
  L.add_order(SIDE_BUY, 1049, 300);

  L.take_quantity(SIDE_BUY, 1049, 200);

  CHECK_EQ(L.quantity_at(SIDE_BUY, 1049), 600ull);
  CHECK_EQ(L.order_count_at(SIDE_BUY, 1049), 2u);
}

TEST(levels_removing_an_order_lowers_both) {
  PriceLevels L(LEVELS);
  L.add_order(SIDE_BUY, 1049, 500);
  L.add_order(SIDE_BUY, 1049, 300);

  L.remove_order(SIDE_BUY, 1049, 300);

  CHECK_EQ(L.quantity_at(SIDE_BUY, 1049), 500ull);
  CHECK_EQ(L.order_count_at(SIDE_BUY, 1049), 1u);
  CHECK(L.occupied_at(SIDE_BUY, 1049));
}

TEST(levels_a_level_dies_when_its_count_reaches_zero) {
  PriceLevels L(LEVELS);
  L.add_order(SIDE_BUY, 1049, 500);
  CHECK(L.occupied_at(SIDE_BUY, 1049));

  L.remove_order(SIDE_BUY, 1049, 500);

  CHECK(!L.occupied_at(SIDE_BUY, 1049));
  CHECK_EQ(L.order_count_at(SIDE_BUY, 1049), 0u);
  CHECK_EQ(L.quantity_at(SIDE_BUY, 1049), 0ull);
  CHECK(!L.has_best(SIDE_BUY));
}

TEST(levels_a_zero_quantity_order_keeps_the_level_alive) {
  // Undisclosed bir order'ın görünür quantity'si sıfır. Boşluğa quantity'ye
  // bakarak karar vermek bu level'ı haksız yere öldürür ve best price'ı
  // kaydırırdı.
  PriceLevels L(LEVELS);
  L.add_order(SIDE_BUY, 1049, 0);

  CHECK_EQ(L.quantity_at(SIDE_BUY, 1049), 0ull);
  CHECK_EQ(L.order_count_at(SIDE_BUY, 1049), 1u);
  CHECK(L.occupied_at(SIDE_BUY, 1049));
  CHECK(L.has_best(SIDE_BUY));
  CHECK_EQ(L.best(SIDE_BUY), 1049);
}

TEST(levels_best_bid_is_the_highest_and_best_ask_the_lowest) {
  PriceLevels L(LEVELS);
  L.add_order(SIDE_BUY, 1047, 10);
  L.add_order(SIDE_BUY, 1049, 20);
  L.add_order(SIDE_BUY, 1048, 30);

  L.add_order(SIDE_SELL, 1053, 10);
  L.add_order(SIDE_SELL, 1051, 20);
  L.add_order(SIDE_SELL, 1052, 30);

  CHECK_EQ(L.best(SIDE_BUY), 1049);
  CHECK_EQ(L.best(SIDE_SELL), 1051);
  CHECK_EQ(L.best_quantity(SIDE_BUY), 20ull);
  CHECK_EQ(L.best_quantity(SIDE_SELL), 20ull);
}

TEST(levels_a_better_price_takes_over_the_best_immediately) {
  PriceLevels L(LEVELS);
  L.add_order(SIDE_BUY, 1047, 10);
  CHECK_EQ(L.best(SIDE_BUY), 1047);

  L.add_order(SIDE_BUY, 1049, 10);     // yüksek bid kazanıyor
  CHECK_EQ(L.best(SIDE_BUY), 1049);

  L.add_order(SIDE_BUY, 1048, 10);     // mevcut best'ten kötü
  CHECK_EQ(L.best(SIDE_BUY), 1049);
}

TEST(levels_best_falls_back_to_the_next_level_when_the_top_empties) {
  PriceLevels L(LEVELS);
  L.add_order(SIDE_BUY, 1047, 10);
  L.add_order(SIDE_BUY, 1048, 20);
  L.add_order(SIDE_BUY, 1049, 30);
  CHECK_EQ(L.best(SIDE_BUY), 1049);

  L.remove_order(SIDE_BUY, 1049, 30);
  CHECK_EQ(L.best(SIDE_BUY), 1048);    // bid'ler aşağı doğru yürüyor

  L.remove_order(SIDE_BUY, 1048, 20);
  CHECK_EQ(L.best(SIDE_BUY), 1047);

  L.remove_order(SIDE_BUY, 1047, 10);
  CHECK(!L.has_best(SIDE_BUY));
}

TEST(levels_ask_side_walks_upward_instead) {
  PriceLevels L(LEVELS);
  L.add_order(SIDE_SELL, 1051, 10);
  L.add_order(SIDE_SELL, 1052, 20);
  L.add_order(SIDE_SELL, 1053, 30);
  CHECK_EQ(L.best(SIDE_SELL), 1051);

  L.remove_order(SIDE_SELL, 1051, 10);
  CHECK_EQ(L.best(SIDE_SELL), 1052);

  L.remove_order(SIDE_SELL, 1052, 20);
  CHECK_EQ(L.best(SIDE_SELL), 1053);
}

TEST(levels_emptying_a_level_that_is_not_the_best_leaves_it_alone) {
  PriceLevels L(LEVELS);
  L.add_order(SIDE_BUY, 1047, 10);
  L.add_order(SIDE_BUY, 1049, 30);

  L.remove_order(SIDE_BUY, 1047, 10);

  CHECK_EQ(L.best(SIDE_BUY), 1049);
  CHECK(!L.occupied_at(SIDE_BUY, 1047));
}

TEST(levels_scan_crosses_bitset_words) {
  // Bir 64 bit word'e 64 level sığıyor. Bir sonraki level'ı uzağa koymak scan'i
  // komple boş word'lerin üzerinden atlamaya zorluyor.
  PriceLevels L(LEVELS);
  L.add_order(SIDE_BUY, 10000, 10);     // base burada sabitleniyor
  L.add_order(SIDE_BUY, 10200, 20);     // 200 tick yukarı: üç word uzakta
  CHECK_EQ(L.best(SIDE_BUY), 10200);

  L.remove_order(SIDE_BUY, 10200, 20);
  CHECK_EQ(L.best(SIDE_BUY), 10000);    // boşluğun ötesinde bulundu

  L.add_order(SIDE_SELL, 10500, 10);
  L.add_order(SIDE_SELL, 10300, 20);
  CHECK_EQ(L.best(SIDE_SELL), 10300);
  L.remove_order(SIDE_SELL, 10300, 20);
  CHECK_EQ(L.best(SIDE_SELL), 10500);
}

TEST(levels_keep_the_two_sides_independent) {
  PriceLevels L(LEVELS);
  L.add_order(SIDE_BUY,  1049, 100);
  L.add_order(SIDE_SELL, 1049, 200);     // aynı price, diğer side

  CHECK_EQ(L.quantity_at(SIDE_BUY,  1049), 100ull);
  CHECK_EQ(L.quantity_at(SIDE_SELL, 1049), 200ull);

  L.remove_order(SIDE_BUY, 1049, 100);

  CHECK(!L.occupied_at(SIDE_BUY, 1049));
  CHECK(L.occupied_at(SIDE_SELL, 1049));
  CHECK_EQ(L.best(SIDE_SELL), 1049);
}

TEST(levels_flag_a_price_outside_the_band_instead_of_writing_past_the_array) {
  PriceLevels L(LEVELS);
  L.add_order(SIDE_BUY, 10000, 10);      // band 9488 .. 10511 oluyor
  CHECK(!L.out_of_band());

  CHECK(!L.add_order(SIDE_BUY, 99999, 10));
  CHECK(L.out_of_band());
}

TEST(levels_flag_a_price_below_the_band_too) {
  PriceLevels L(LEVELS);
  L.add_order(SIDE_BUY, 10000, 10);
  CHECK(!L.out_of_band());

  CHECK(!L.add_order(SIDE_BUY, 1, 10));
  CHECK(L.out_of_band());
}

TEST(levels_handle_the_sample_feeds_price_band) {
  // Target book o gün 17.060 ile 20.840 arasında işlem gördü: config'in izin
  // verdiği 16.384 level'a karşı 3.780 tick.
  PriceLevels L(16384);
  L.add_order(SIDE_BUY, 19000, 100);     // görülen ilk price, band buraya ortalanıyor

  CHECK(L.add_order(SIDE_BUY, 17060, 10));
  CHECK(L.add_order(SIDE_SELL, 20840, 10));
  CHECK(!L.out_of_band());

  CHECK_EQ(L.best(SIDE_BUY), 19000);
  CHECK_EQ(L.best(SIDE_SELL), 20840);
}
