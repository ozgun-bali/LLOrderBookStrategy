#include "test_framework.hpp"
#include "book/order_pool.hpp"

#include <vector>

using namespace llob;

TEST(pool_stores_and_finds_an_order) {
  OrderPool p(16);
  CHECK_EQ(p.insert(SIDE_BUY, 77, 104900, 500), INSERT_OK);
  CHECK_EQ(p.size(), 1);

  const Order* o = p.find(SIDE_BUY, 77);
  REQUIRE(o != 0);
  CHECK_EQ(o->id, 77ull);
  CHECK_EQ(o->price, 104900);
  CHECK_EQ(o->remaining, 500ull);
}

TEST(pool_returns_null_for_an_id_it_does_not_have) {
  OrderPool p(16);
  p.insert(SIDE_BUY, 77, 100, 10);

  CHECK(p.find(SIDE_BUY, 99) == 0);
  CHECK(p.find(SIDE_BUY, 0)  == 0);
}

TEST(pool_keeps_the_two_sides_apart) {
  // ITCH id'leri book VE side bazında unique, yani aynı id aynı anda iki side'da
  // da aktif olabiliyor. Birbirlerine çarpmamaları gerekiyor.
  OrderPool p(16);
  CHECK_EQ(p.insert(SIDE_BUY,  77, 104900, 500), INSERT_OK);
  CHECK_EQ(p.insert(SIDE_SELL, 77, 105100, 300), INSERT_OK);
  CHECK_EQ(p.size(), 2);

  const Order* b = p.find(SIDE_BUY, 77);
  const Order* s = p.find(SIDE_SELL, 77);
  REQUIRE(b != 0);
  REQUIRE(s != 0);
  CHECK_EQ(b->price, 104900);
  CHECK_EQ(s->price, 105100);
  CHECK(b != s);
}

TEST(pool_rejects_a_duplicate_id_on_the_same_side) {
  OrderPool p(16);
  CHECK_EQ(p.insert(SIDE_BUY, 77, 100, 10), INSERT_OK);
  CHECK_EQ(p.insert(SIDE_BUY, 77, 200, 20), INSERT_DUPLICATE);

  const Order* o = p.find(SIDE_BUY, 77);
  REQUIRE(o != 0);
  CHECK_EQ(o->price, 100);          // ilkine dokunulmadı
  CHECK_EQ(p.size(), 1);
}

TEST(pool_reports_full_instead_of_overwriting) {
  OrderPool p(4);
  for (OrderId i = 1; i <= 4; ++i)
    CHECK_EQ(p.insert(SIDE_BUY, i, 100, 10), INSERT_OK);

  CHECK(p.full());
  CHECK_EQ(p.insert(SIDE_BUY, 5, 100, 10), INSERT_FULL);
  CHECK_EQ(p.size(), 4);

  for (OrderId i = 1; i <= 4; ++i)   // hiçbir şey bozulmadı
    CHECK(p.find(SIDE_BUY, i) != 0);
}

TEST(pool_reuses_a_slot_after_a_removal) {
  OrderPool p(2);
  p.insert(SIDE_BUY, 1, 100, 10);
  p.insert(SIDE_BUY, 2, 200, 20);
  CHECK(p.full());

  CHECK(p.remove(SIDE_BUY, 1));
  CHECK(!p.full());
  CHECK_EQ(p.size(), 1);

  CHECK_EQ(p.insert(SIDE_BUY, 3, 300, 30), INSERT_OK);
  CHECK(p.find(SIDE_BUY, 1) == 0);
  CHECK(p.find(SIDE_BUY, 3) != 0);
}

TEST(pool_remove_reports_whether_it_found_anything) {
  OrderPool p(16);
  p.insert(SIDE_BUY, 77, 100, 10);

  CHECK(p.remove(SIDE_BUY, 77));
  CHECK(!p.remove(SIDE_BUY, 77));   // zaten silinmişti
  CHECK(!p.remove(SIDE_BUY, 99));   // hiç var olmadı
  CHECK_EQ(p.size(), 0);
}

TEST(pool_removing_one_side_leaves_the_other) {
  OrderPool p(16);
  p.insert(SIDE_BUY,  77, 100, 10);
  p.insert(SIDE_SELL, 77, 200, 20);

  CHECK(p.remove(SIDE_BUY, 77));
  CHECK(p.find(SIDE_BUY, 77) == 0);
  REQUIRE(p.find(SIDE_SELL, 77) != 0);
  CHECK_EQ(p.find(SIDE_SELL, 77)->price, 200);
}

TEST(pool_lets_the_caller_change_the_remaining_quantity) {
  // Bir Order Executed message'ı tam olarak böyle uygulanıyor: bul, çıkar, ve
  // değişiklik kalıcı olmak zorunda.
  OrderPool p(16);
  p.insert(SIDE_BUY, 77, 100, 500);

  Order* o = p.find(SIDE_BUY, 77);
  REQUIRE(o != 0);
  o->remaining -= 200;

  CHECK_EQ(p.find(SIDE_BUY, 77)->remaining, 300ull);
}

TEST(pool_pointers_stay_valid_while_other_orders_come_and_go) {
  OrderPool p(64);
  p.insert(SIDE_BUY, 1, 100, 500);
  Order* first = p.find(SIDE_BUY, 1);
  REQUIRE(first != 0);

  for (OrderId i = 2; i < 40; ++i) p.insert(SIDE_BUY, i, 100, 10);
  for (OrderId i = 2; i < 40; ++i) p.remove(SIDE_BUY, i);

  CHECK_EQ(first->id, 1ull);              // slot array'i hiç yer değiştirmiyor
  CHECK_EQ(first->remaining, 500ull);
  CHECK_EQ(p.find(SIDE_BUY, 1), first);
}

TEST(pool_survives_a_deletion_in_the_middle_of_a_probe_chain) {
  // Klasik open-addressing tuzağı. 1, 1+N ve 1+2N id'leri aynı home bucket'a
  // düşüyor, yani ikincisi ve üçüncüsü zincirin ilerisinde duruyor. İlkini
  // silmek diğerlerini ulaşılamaz hale getirmemeli.
  const std::size_t CAP = 8;
  OrderPool p(CAP);
  const OrderId STEP = 16;       // capacity 8 için table 16 bucket

  CHECK_EQ(p.insert(SIDE_BUY, 1,            100, 10), INSERT_OK);
  CHECK_EQ(p.insert(SIDE_BUY, 1 + STEP,     200, 20), INSERT_OK);
  CHECK_EQ(p.insert(SIDE_BUY, 1 + 2 * STEP, 300, 30), INSERT_OK);

  CHECK(p.remove(SIDE_BUY, 1));

  REQUIRE(p.find(SIDE_BUY, 1 + STEP) != 0);
  REQUIRE(p.find(SIDE_BUY, 1 + 2 * STEP) != 0);
  CHECK_EQ(p.find(SIDE_BUY, 1 + STEP)->price, 200);
  CHECK_EQ(p.find(SIDE_BUY, 1 + 2 * STEP)->price, 300);
  CHECK(p.find(SIDE_BUY, 1) == 0);
}

TEST(pool_handles_many_inserts_and_removals_without_losing_anything) {
  // Tekrar tekrar dolduruyor, boşaltıyor, yeniden dolduruyor. Delete tombstone
  // bıraksaydı ya da bir zinciri kırsaydı burada bir şey kaybolurdu.
  const std::size_t CAP = 256;
  OrderPool p(CAP);

  for (int round = 0; round < 4; ++round) {
    const OrderId base = (OrderId)round * 1000 + 1;

    for (std::size_t i = 0; i < CAP; ++i)
      CHECK_EQ(p.insert(SIDE_BUY, base + i, (Price)i, i), INSERT_OK);
    CHECK_EQ(p.size(), CAP);

    for (std::size_t i = 0; i < CAP; ++i) {
      const Order* o = p.find(SIDE_BUY, base + i);
      REQUIRE(o != 0);
      CHECK_EQ(o->remaining, (Quantity)i);
    }

    for (std::size_t i = 0; i < CAP; ++i)
      CHECK(p.remove(SIDE_BUY, base + i));
    CHECK_EQ(p.size(), 0);
  }
}

TEST(pool_removing_from_the_middle_of_a_full_table_keeps_the_rest_findable) {
  const std::size_t CAP = 128;
  OrderPool p(CAP);
  for (std::size_t i = 0; i < CAP; ++i)
    p.insert(SIDE_BUY, i * 7 + 3, (Price)i, 1);

  for (std::size_t i = 0; i < CAP; i += 2)      // bir atlayarak sil
    CHECK(p.remove(SIDE_BUY, i * 7 + 3));

  for (std::size_t i = 1; i < CAP; i += 2)      // geri kalanı hâlâ orada olmalı
    CHECK(p.find(SIDE_BUY, i * 7 + 3) != 0);
  for (std::size_t i = 0; i < CAP; i += 2)
    CHECK(p.find(SIDE_BUY, i * 7 + 3) == 0);

  CHECK_EQ(p.size(), CAP / 2);
}
