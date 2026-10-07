#ifndef LLOB_BOOK_GROUP_JOURNAL_HPP
#define LLOB_BOOK_GROUP_JOURNAL_HPP

// What each price level looked like when the current timestamp group started.
//
// The strategy runs on a completed group and has to answer three questions the
// finished book cannot: was this price occupied before the group, which side
// was it on, and did it change sides during the group. Only a record kept while
// the group was running can say.
//
// Snapshotting the book per group is out of the question - 16k levels across
// 197k groups on the sample feed. Instead only the prices actually touched are
// recorded, and the first touch is the one that counts: at that moment nothing
// in this group has changed the level yet, so what it holds is the pre-group
// state.
//
// Clearing is the other half of the problem. Wiping the arrays between groups
// would be as expensive as snapshotting, so nothing is wiped: every entry
// carries the group number it was written for, and a stale entry is simply one
// whose number no longer matches. Ending a group is ++current_.

#include "core/types.hpp"

#include <cassert>
#include <vector>

namespace llob {

class GroupJournal {
public:
    explicit GroupJournal(std::size_t max_levels)
        : generation_(max_levels, 0), before_(max_levels, 0),
          touched_(max_levels, 0), current_(1) {
        // Entries start at generation 0 and the first group is 1, so every
        // slot reads as untouched before anything happens.
        assert(max_levels > 0);
    }

    uint32_t group() const { return current_; }

    // Ends the group that was running and starts the next one. Every entry
    // written for the old group becomes stale by arithmetic alone.
    void begin_group() { ++current_; }

    // Called by PriceLevels before it changes a level, while the level still
    // holds whatever it held. `side` is the side about to change; the two flags
    // are the occupancy of this price right now, on each side.
    void note(std::size_t index, Side side, bool bid_occupied, bool ask_occupied) {
        assert(index < generation_.size());
        assert(side == SIDE_BUY || side == SIDE_SELL);

        if (generation_[index] != current_) {
            generation_[index] = current_;
            before_[index]     = (uint8_t)sideOf(bid_occupied, ask_occupied);
            touched_[index]    = 0;          // the stale mask from an old group
        }
        touched_[index] |= (uint8_t)(1u << side);
    }

    bool changed_this_group(std::size_t index) const {
        assert(index < generation_.size());
        return generation_[index] == current_;
    }

    // Which side held this price when the group started. SIDE_INVALID means it
    // was empty, so nothing disappeared from it and there is no direction to
    // take. Only meaningful where changed_this_group() is true.
    Side side_before(std::size_t index) const {
        assert(changed_this_group(index));
        return (Side)before_[index];
    }

    // Both sides changed during the group. In a book that never crosses, that
    // means the price moved from one side to the other, which is a price moving
    // through the level rather than liquidity going missing from it.
    bool flipped(std::size_t index) const {
        assert(index < generation_.size());
        if (generation_[index] != current_) return false;
        return touched_[index] == (BID_BIT | ASK_BIT);
    }

    uint8_t touched_mask(std::size_t index) const {
        assert(index < generation_.size());
        return generation_[index] == current_ ? touched_[index] : (uint8_t)0;
    }

private:
    static const uint8_t BID_BIT = 1u << SIDE_BUY;    // 0b01
    static const uint8_t ASK_BIT = 1u << SIDE_SELL;   // 0b10

    // A price on both sides at once means the book was crossed, which should
    // not survive a group boundary. Treated as unknown so no direction is read
    // out of it.
    static Side sideOf(bool bid, bool ask) {
        if (bid && ask) return SIDE_INVALID;
        if (bid)        return SIDE_BUY;
        if (ask)        return SIDE_SELL;
        return SIDE_INVALID;
    }

    std::vector<uint32_t> generation_;
    std::vector<uint8_t>  before_;
    std::vector<uint8_t>  touched_;
    uint32_t              current_;
};

} // namespace llob

#endif // LLOB_BOOK_GROUP_JOURNAL_HPP
