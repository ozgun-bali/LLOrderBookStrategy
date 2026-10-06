#ifndef LLOB_BOOK_ORDER_POOL_HPP
#define LLOB_BOOK_ORDER_POOL_HPP

// Fixed-capacity storage for resting orders, plus the order id index.
//
// Capacity comes from config.max_active_orders and is allocated once. Running
// out returns INSERT_FULL so the caller can stop the strategy; it never grows,
// never overwrites and never corrupts.
//
// ITCH order ids are unique per order book AND side only, so the key is
// (side, order_id). Slots are shared across both sides - an order is an order -
// but there is one index table per side, and every 'E' and 'D' message carries
// the side, so there is never any doubt which table to look in.
//
// Lookup is open addressing with linear probing over a power-of-two table kept
// at most half full. Deletion shifts entries back instead of leaving tombstones,
// because orders are removed constantly and tombstones would slowly turn every
// probe into a scan.

#include "core/types.hpp"

#include <cassert>
#include <vector>

namespace llob {

// Namespace scope rather than a static member: an in-class `static const` is a
// declaration, and C++11 needs a separate definition the moment anything binds
// a reference to it, which a header-only class has nowhere to put.
static const uint32_t NO_SLOT = 0xFFFFFFFFu;

struct Order {
    OrderId  id;
    Quantity remaining;
    Price    price;
    uint32_t next_free;   // only meaningful while the slot is on the free list
};

enum InsertResult {
    INSERT_OK,
    INSERT_DUPLICATE,   // this id is already active on this side
    INSERT_FULL         // capacity reached; caller must stop the strategy
};

class OrderPool {
public:
    explicit OrderPool(std::size_t capacity)
        : slots_(capacity), free_head_(0), size_(0),
          mask_(tableSize(capacity) - 1),
          shift_(shiftFor(tableSize(capacity))) {
        assert(capacity > 0);
        assert(capacity < NO_SLOT);

        // Chain every slot into the free list, last one terminates it.
        for (std::size_t i = 0; i + 1 < capacity; ++i)
            slots_[i].next_free = (uint32_t)(i + 1);
        slots_[capacity - 1].next_free = NO_SLOT;

        for (int s = 0; s < SIDE_COUNT; ++s)
            buckets_[s].assign(mask_ + 1, NO_SLOT);
    }

    std::size_t size()     const { return size_; }
    std::size_t capacity() const { return slots_.size(); }
    bool        full()     const { return free_head_ == NO_SLOT; }

    InsertResult insert(Side side, OrderId id, Price price, Quantity qty) {
        assert(side == SIDE_BUY || side == SIDE_SELL);

        std::vector<uint32_t>& table = buckets_[side];
        std::size_t b = home(id);

        while (table[b] != NO_SLOT) {
            if (slots_[table[b]].id == id) return INSERT_DUPLICATE;
            b = (b + 1) & mask_;
        }
        if (free_head_ == NO_SLOT) return INSERT_FULL;

        const uint32_t slot = free_head_;
        free_head_ = slots_[slot].next_free;

        Order& o = slots_[slot];
        o.id        = id;
        o.remaining = qty;
        o.price     = price;
        o.next_free = NO_SLOT;

        table[b] = slot;
        ++size_;
        return INSERT_OK;
    }

    // Points into the slot array, which never moves, so the pointer stays valid
    // until this order is removed. Callers mutate `remaining` through it.
    Order* find(Side side, OrderId id) {
        const uint32_t slot = findSlot(side, id);
        return slot == NO_SLOT ? 0 : &slots_[slot];
    }

    const Order* find(Side side, OrderId id) const {
        const uint32_t slot = findSlot(side, id);
        return slot == NO_SLOT ? 0 : &slots_[slot];
    }

    // Walks the live orders on one side. The index table only ever points at
    // occupied slots, so iterating it visits each live order exactly once.
    // Order is unspecified, and this is for checking, not for the hot path.
    template <typename Fn>
    void for_each(Side side, Fn fn) const {
        assert(side == SIDE_BUY || side == SIDE_SELL);
        const std::vector<uint32_t>& table = buckets_[side];
        for (std::size_t b = 0; b < table.size(); ++b)
            if (table[b] != NO_SLOT) fn(slots_[table[b]]);
    }

    bool remove(Side side, OrderId id) {
        assert(side == SIDE_BUY || side == SIDE_SELL);

        std::vector<uint32_t>& table = buckets_[side];
        std::size_t b = home(id);

        while (table[b] != NO_SLOT) {
            if (slots_[table[b]].id == id) {
                const uint32_t slot = table[b];

                slots_[slot].next_free = free_head_;   // back onto the free list
                free_head_ = slot;
                --size_;

                closeGap(table, b);
                return true;
            }
            b = (b + 1) & mask_;
        }
        return false;
    }

private:
    // Multiply-shift: the constant is 2^64 / golden ratio. The multiply mixes
    // the bits and the high ones come out best mixed, so the shift takes those.
    // Plain id % size would also work while ids run in sequence, but this keeps
    // probes short whatever pattern the feed produces.
    std::size_t home(OrderId id) const {
        return (std::size_t)((id * 0x9E3779B97F4A7C15ULL) >> shift_) & mask_;
    }

    uint32_t findSlot(Side side, OrderId id) const {
        assert(side == SIDE_BUY || side == SIDE_SELL);

        const std::vector<uint32_t>& table = buckets_[side];
        std::size_t b = home(id);

        // An empty bucket ends the search: had this id been inserted, it would
        // have landed on the first empty bucket along this same path.
        while (table[b] != NO_SLOT) {
            if (slots_[table[b]].id == id) return table[b];
            b = (b + 1) & mask_;
        }
        return NO_SLOT;
    }

    // Backward-shift deletion. Clearing a bucket outright would cut the probe
    // chain: an entry that had been pushed past this bucket would never be
    // reached again. So walk forward and pull back anything whose home bucket
    // is at or before the hole.
    void closeGap(std::vector<uint32_t>& table, std::size_t hole) {
        std::size_t j = hole;
        table[hole] = NO_SLOT;

        for (;;) {
            j = (j + 1) & mask_;
            if (table[j] == NO_SLOT) return;

            const std::size_t k = home(slots_[table[j]].id);

            // Leave it alone while its home lies inside (hole, j]: moving it
            // back would put it before its own starting point.
            const bool inRange = (hole <= j) ? (hole < k && k <= j)
                                             : (hole < k || k <= j);
            if (inRange) continue;

            table[hole] = table[j];
            table[j]    = NO_SLOT;
            hole        = j;
        }
    }

    // Power of two, at least twice the capacity: a load factor under 0.5 keeps
    // the average probe near one.
    static std::size_t tableSize(std::size_t capacity) {
        std::size_t n = 1;
        while (n < capacity * 2) n <<= 1;
        return n;
    }

    static unsigned shiftFor(std::size_t tableSize) {
        unsigned bits = 0;
        while ((std::size_t(1) << bits) < tableSize) ++bits;
        return 64 - bits;
    }

    std::vector<Order>    slots_;
    std::vector<uint32_t> buckets_[SIDE_COUNT];
    uint32_t              free_head_;
    std::size_t           size_;
    std::size_t           mask_;
    unsigned              shift_;
};

} // namespace llob

#endif // LLOB_BOOK_ORDER_POOL_HPP
