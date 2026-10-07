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
  j.note(10, SIDE_SELL, /*bid*/ false, /*ask*/ true);   // an ask is being changed

  CHECK(j.changed_this_group(10));
  CHECK_EQ(j.side_before(10), SIDE_SELL);
  CHECK(!j.flipped(10));
}

TEST(journal_records_an_empty_price_as_invalid) {
  // Nothing was there before, so nothing disappeared and there is no direction.
  GroupJournal j(64);
  j.note(10, SIDE_BUY, false, false);

  CHECK(j.changed_this_group(10));
  CHECK_EQ(j.side_before(10), SIDE_INVALID);
}

TEST(journal_keeps_the_first_touch_and_ignores_later_ones) {
  // The first touch happens before anything in this group has changed the
  // level, so that is the pre-group state. Later touches must not overwrite it.
  GroupJournal j(64);

  j.note(10, SIDE_BUY, /*bid*/ true, /*ask*/ false);    // was a bid
  CHECK_EQ(j.side_before(10), SIDE_BUY);

  // The bid is gone by now and an ask has appeared, but before_ must not move.
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

  CHECK(!j.changed_this_group(10));      // stale by arithmetic, nothing was wiped
  CHECK(!j.flipped(10));
  CHECK_EQ(j.touched_mask(10), 0);
}

TEST(journal_clears_a_stale_mask_on_the_next_first_touch) {
  // The arrays are never wiped, so a slot still holds the mask from an older
  // group. Starting fresh is the job of the first touch in the new group.
  GroupJournal j(64);

  j.note(10, SIDE_BUY,  true,  false);
  j.note(10, SIDE_SELL, false, true);
  CHECK(j.flipped(10));                  // mask is 0b11 in this group

  j.begin_group();
  j.note(10, SIDE_BUY, true, false);     // only one side this time

  CHECK_EQ(j.touched_mask(10), 0x1);     // not 0b11 carried over
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
  CHECK(!j.changed_this_group(15));      // untouched price in between
}

TEST(journal_a_buy_here_and_a_sell_there_is_not_a_flip) {
  // The mask is per price. Touching the bid at one price and the ask at
  // another is ordinary market activity, not a price changing sides.
  GroupJournal j(64);
  j.note(10, SIDE_BUY,  true,  false);
  j.note(11, SIDE_SELL, false, true);

  CHECK(!j.flipped(10));
  CHECK(!j.flipped(11));
}

TEST(journal_treats_a_crossed_price_as_unknown) {
  // Occupied on both sides at once should not survive a group boundary. If it
  // does, there is no honest answer to "which side was it", so no direction.
  GroupJournal j(64);
  j.note(10, SIDE_BUY, /*bid*/ true, /*ask*/ true);

  CHECK_EQ(j.side_before(10), SIDE_INVALID);
}

TEST(journal_survives_many_groups) {
  GroupJournal j(64);

  for (int g = 0; g < 1000; ++g) {
    j.note(5, (g % 2) ? SIDE_BUY : SIDE_SELL, (g % 2) != 0, (g % 2) == 0);
    CHECK(j.changed_this_group(5));
    CHECK(!j.flipped(5));                // one side per group
    j.begin_group();
  }
  CHECK(!j.changed_this_group(5));
  CHECK_EQ(j.group(), 1001u);
}
