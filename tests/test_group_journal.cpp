#include "test_framework.hpp"
#include "book/group_journal.hpp"

using namespace llob;

TEST(journal_starts_with_nothing_recorded) {
  GroupJournal j(64);
  CHECK(!j.changed_this_group(0));
  CHECK(!j.changed_this_group(63));
  CHECK(!j.flipped(0));
  CHECK_EQ(j.touched_mask(0), 0);
}

TEST(journal_records_the_side_a_price_was_on) {
  GroupJournal j(64);
  j.note(10, SIDE_SELL, /*bid*/ false, /*ask*/ true);   // bir ask değiştiriliyor

  CHECK(j.changed_this_group(10));
  CHECK_EQ(j.side_before(10), SIDE_SELL);
  CHECK(!j.flipped(10));
}

TEST(journal_records_an_empty_price_as_invalid) {
  // Öncesinde orada bir şey yoktu, yani hiçbir şey kaybolmadı ve bir yön yok.
  GroupJournal j(64);
  j.note(10, SIDE_BUY, false, false);

  CHECK(j.changed_this_group(10));
  CHECK_EQ(j.side_before(10), SIDE_INVALID);
}

TEST(journal_keeps_the_first_touch_and_ignores_later_ones) {
  // İlk dokunuş, bu group'ta level'ı henüz hiçbir şey değiştirmeden önce
  // gerçekleşiyor; yani group öncesi durum o. Sonraki dokunuşlar onun üzerine
  // yazmamalı.
  GroupJournal j(64);

  j.note(10, SIDE_BUY, /*bid*/ true, /*ask*/ false);    // bid'di
  CHECK_EQ(j.side_before(10), SIDE_BUY);

  // Bu noktada bid gitti ve bir ask belirdi, ama before_ yerinden oynamamalı.
  j.note(10, SIDE_SELL, /*bid*/ false, /*ask*/ true);
  CHECK_EQ(j.side_before(10), SIDE_BUY);
}

TEST(journal_accumulates_every_touch_into_the_mask) {
  GroupJournal j(64);

  j.note(10, SIDE_BUY, true, false);
  CHECK_EQ(j.touched_mask(10), 0x1);     // 0b01
  CHECK(!j.flipped(10));

  j.note(10, SIDE_SELL, false, true);
  CHECK_EQ(j.touched_mask(10), 0x3);     // 0b11
  CHECK(j.flipped(10));
}

TEST(journal_does_not_call_repeated_touches_on_one_side_a_flip) {
  GroupJournal j(64);
  j.note(10, SIDE_BUY, true, false);
  j.note(10, SIDE_BUY, true, false);
  j.note(10, SIDE_BUY, true, false);

  CHECK_EQ(j.touched_mask(10), 0x1);
  CHECK(!j.flipped(10));
}

TEST(journal_forgets_everything_when_the_group_ends) {
  GroupJournal j(64);
  j.note(10, SIDE_BUY, true, false);
  CHECK(j.changed_this_group(10));

  j.begin_group();

  CHECK(!j.changed_this_group(10));      // aritmetikle stale oldu, hiçbir şey silinmedi
  CHECK(!j.flipped(10));
  CHECK_EQ(j.touched_mask(10), 0);
}

TEST(journal_clears_a_stale_mask_on_the_next_first_touch) {
  // Array'ler hiç silinmiyor, yani bir slot hâlâ eski bir group'tan kalan mask'i
  // tutuyor. Sıfırdan başlatmak, yeni group'taki ilk dokunuşun işi.
  GroupJournal j(64);

  j.note(10, SIDE_BUY,  true,  false);
  j.note(10, SIDE_SELL, false, true);
  CHECK(j.flipped(10));                  // bu group'ta mask 0b11

  j.begin_group();
  j.note(10, SIDE_BUY, true, false);     // bu sefer sadece tek side

  CHECK_EQ(j.touched_mask(10), 0x1);     // devreden 0b11 değil
  CHECK(!j.flipped(10));
}

TEST(journal_keeps_prices_independent) {
  GroupJournal j(64);
  j.note(10, SIDE_BUY,  true,  false);
  j.note(20, SIDE_SELL, false, true);

  CHECK_EQ(j.side_before(10), SIDE_BUY);
  CHECK_EQ(j.side_before(20), SIDE_SELL);
  CHECK(!j.flipped(10));
  CHECK(!j.flipped(20));
  CHECK(!j.changed_this_group(15));      // aradaki dokunulmamış price
}

TEST(journal_a_buy_here_and_a_sell_there_is_not_a_flip) {
  // Mask price başına tutuluyor. Bir price'ta bid'e, başka bir price'ta ask'e
  // dokunmak sıradan piyasa hareketi; bir price'ın side değiştirmesi değil.
  GroupJournal j(64);
  j.note(10, SIDE_BUY,  true,  false);
  j.note(11, SIDE_SELL, false, true);

  CHECK(!j.flipped(10));
  CHECK(!j.flipped(11));
}

TEST(journal_treats_a_crossed_price_as_unknown) {
  // Aynı anda iki side'da birden dolu olmak bir group sınırını geçmemeli.
  // Geçerse "hangi side'daydı" sorusunun dürüst bir cevabı yok, yani yön de yok.
  GroupJournal j(64);
  j.note(10, SIDE_BUY, /*bid*/ true, /*ask*/ true);

  CHECK_EQ(j.side_before(10), SIDE_INVALID);
}

TEST(journal_survives_many_groups) {
  GroupJournal j(64);

  for (int g = 0; g < 1000; ++g) {
    j.note(5, (g % 2) ? SIDE_BUY : SIDE_SELL, (g % 2) != 0, (g % 2) == 0);
    CHECK(j.changed_this_group(5));
    CHECK(!j.flipped(5));                // group başına tek side
    j.begin_group();
  }
  CHECK(!j.changed_this_group(5));
  CHECK_EQ(j.group(), 1001u);
}
