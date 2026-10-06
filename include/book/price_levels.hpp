#ifndef LLOB_BOOK_PRICE_LEVELS_HPP
#define LLOB_BOOK_PRICE_LEVELS_HPP

// Price level aggregates for both sides, and the best price on each.
//
// Prices are whole kurus with tick_size 1, so a level is addressed directly as
// price - base_price. No search, no comparison, no reordering: an update is one
// subtraction and one array write. A sorted vector would put the best price at
// index 0 for free, but the dominant operation here is order id -> price ->
// level, and that would be a binary search every time; worse, insert and erase
// shift indices, so an order could not cache where its level lives.
//
// The two sides are separate arrays over the same index space. One array with a
// side tag per level would halve the memory, but inside a timestamp group a
// price really can be on both sides at once - adding the ask before deleting
// the bid is a legal order of events - and a single tag cannot hold that.
//
// base_price is derived from the first price seen, centred in the band. Sized
// from the sample feed, where the target book moved between 17,060 and 20,840
// kurus: a band of 3,780 ticks against 16,384 levels of room.
//
// An occupancy bitset carries the best price forward when a level empties.
// Walking levels one at a time would be fine most of the time and terrible
// occasionally, which is exactly the shape that ruins a p99.9.

#include "core/types.hpp"

#include <cassert>
#include <vector>

namespace llob {

struct Level {
    Quantity quantity;      // sum of the remaining quantity of its orders
    uint32_t order_count;   // how many orders rest here
};

class PriceLevels {
public:
    explicit PriceLevels(std::size_t max_levels)
        : levels_(), words_((max_levels + 63) / 64),
          max_levels_(max_levels), base_price_(0), have_base_(false),
          out_of_band_(false) {
        assert(max_levels > 0);

        for (int s = 0; s < SIDE_COUNT; ++s) {
            levels_[s].assign(max_levels, zeroLevel());
            occupied_[s].assign(words_, 0);
            best_[s] = NO_LEVEL;
        }
    }

    // Set once the first price arrives and never moved afterwards. A feed that
    // walked outside the band would need a rebase, which is a safe error here.
    bool have_base()  const { return have_base_; }
    Price base_price() const { return base_price_; }

    // Latches on the first price outside the band. The book is wrong from that
    // point on, so the caller stops the strategy rather than carrying on.
    bool out_of_band() const { return out_of_band_; }

    bool  has_best(Side side) const { return best_[side] != NO_LEVEL; }
    Price best(Side side) const {
        assert(has_best(side));
        return toPrice(best_[side]);
    }
    Quantity best_quantity(Side side) const {
        assert(has_best(side));
        return levels_[side][best_[side]].quantity;
    }
    uint32_t best_order_count(Side side) const {
        assert(has_best(side));
        return levels_[side][best_[side]].order_count;
    }

    Quantity quantity_at(Side side, Price price) const {
        const std::size_t i = indexOf(price);
        return i == NO_LEVEL ? 0 : levels_[side][i].quantity;
    }
    uint32_t order_count_at(Side side, Price price) const {
        const std::size_t i = indexOf(price);
        return i == NO_LEVEL ? 0 : levels_[side][i].order_count;
    }
    bool occupied_at(Side side, Price price) const {
        const std::size_t i = indexOf(price);
        return i != NO_LEVEL && testBit(side, i);
    }

    // An order joins this level: quantity grows and the level gains a member.
    bool add_order(Side side, Price price, Quantity qty) {
        if (!have_base_) setBase(price);

        const std::size_t i = indexOf(price);
        if (i == NO_LEVEL) { out_of_band_ = true; return false; }

        Level& lv = levels_[side][i];
        lv.quantity += qty;
        ++lv.order_count;

        if (lv.order_count == 1) {          // the level just came alive
            setBit(side, i);
            raiseBest(side, i);
        }
        return true;
    }

    // Part of an order filled. It is still resting here, so the count holds.
    void take_quantity(Side side, Price price, Quantity qty) {
        const std::size_t i = indexOf(price);
        if (i == NO_LEVEL) { out_of_band_ = true; return; }

        Level& lv = levels_[side][i];
        assert(lv.quantity >= qty);
        lv.quantity -= qty;
    }

    // An order left: filled in full, or deleted. The level may now be empty.
    void remove_order(Side side, Price price, Quantity qty) {
        const std::size_t i = indexOf(price);
        if (i == NO_LEVEL) { out_of_band_ = true; return; }

        Level& lv = levels_[side][i];
        assert(lv.quantity >= qty);
        assert(lv.order_count > 0);

        lv.quantity -= qty;
        --lv.order_count;

        // Emptiness is decided by the count, not the quantity: an undisclosed
        // order shows a visible quantity of zero while still resting here.
        if (lv.order_count == 0) {
            lv.quantity = 0;
            clearBit(side, i);
            if (best_[side] == i) carryBestForward(side);
        }
    }

private:
    static const std::size_t NO_LEVEL = (std::size_t)-1;

    static Level zeroLevel() { Level l; l.quantity = 0; l.order_count = 0; return l; }

    void setBase(Price price) {
        // Centre the band on the first price so the book can move either way.
        base_price_ = price - (Price)(max_levels_ / 2);
        have_base_  = true;
    }

    std::size_t indexOf(Price price) const {
        if (!have_base_) return NO_LEVEL;
        const int64_t i = (int64_t)price - (int64_t)base_price_;
        if (i < 0 || (uint64_t)i >= (uint64_t)max_levels_) return NO_LEVEL;
        return (std::size_t)i;
    }

    Price toPrice(std::size_t index) const {
        return (Price)((int64_t)base_price_ + (int64_t)index);
    }

    bool testBit(Side side, std::size_t i) const {
        return (occupied_[side][i >> 6] >> (i & 63)) & 1ULL;
    }
    void setBit(Side side, std::size_t i) {
        occupied_[side][i >> 6] |= (1ULL << (i & 63));
    }
    void clearBit(Side side, std::size_t i) {
        occupied_[side][i >> 6] &= ~(1ULL << (i & 63));
    }

    // A new level can only improve the best, never worsen it, so one comparison
    // settles it. Buy wants the highest price, sell the lowest.
    void raiseBest(Side side, std::size_t i) {
        if (best_[side] == NO_LEVEL) { best_[side] = i; return; }
        if (side == SIDE_BUY) { if (i > best_[side]) best_[side] = i; }
        else                  { if (i < best_[side]) best_[side] = i; }
    }

    // The best level emptied. The next one is in the same direction the side
    // worsens in: downward for buys, upward for sells.
    void carryBestForward(Side side) {
        const std::size_t from = best_[side];
        best_[side] = (side == SIDE_BUY) ? scanDown(side, from) : scanUp(side, from);
    }

    // Lowest set bit strictly above `from`. 64 levels are tested per word, and
    // __builtin_ctzll turns a word into its first set bit in one instruction.
    std::size_t scanUp(Side side, std::size_t from) const {
        std::size_t i = from + 1;
        if (i >= max_levels_) return NO_LEVEL;

        std::size_t w = i >> 6;
        uint64_t    v = occupied_[side][w] & (~0ULL << (i & 63));

        while (true) {
            if (v) return (w << 6) + (std::size_t)__builtin_ctzll(v);
            if (++w >= words_) return NO_LEVEL;
            v = occupied_[side][w];
        }
    }

    // Highest set bit strictly below `from`. __builtin_clzll counts the leading
    // zeros, so 63 minus that is the position of the top set bit.
    std::size_t scanDown(Side side, std::size_t from) const {
        if (from == 0) return NO_LEVEL;
        std::size_t i = from - 1;

        std::size_t w = i >> 6;
        const unsigned keep = (unsigned)(i & 63);
        uint64_t v = occupied_[side][w];
        if (keep != 63) v &= ~(~0ULL << (keep + 1));

        while (true) {
            if (v) return (w << 6) + (63 - (std::size_t)__builtin_clzll(v));
            if (w == 0) return NO_LEVEL;
            v = occupied_[side][--w];
        }
    }

    std::vector<Level>    levels_[SIDE_COUNT];
    std::vector<uint64_t> occupied_[SIDE_COUNT];
    std::size_t           best_[SIDE_COUNT];
    std::size_t           words_;
    std::size_t           max_levels_;
    Price                 base_price_;
    bool                  have_base_;
    bool                  out_of_band_;
};

} // namespace llob

#endif // LLOB_BOOK_PRICE_LEVELS_HPP
