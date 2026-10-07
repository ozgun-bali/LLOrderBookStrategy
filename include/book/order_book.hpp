#ifndef LLOB_BOOK_ORDER_BOOK_HPP
#define LLOB_BOOK_ORDER_BOOK_HPP

// The order book for the single target instrument.
//
// Composes OrderPool, which holds the orders and finds them by (side, id), and
// PriceLevels, which holds the per-price totals and the best price on each side.
// This class is the part that knows what each ITCH message means.
//
// Messages for other books never arrive here: the parser drops them before
// decoding. Everything below assumes the message belongs to this book.

#include "core/config.hpp"
#include "core/types.hpp"
#include "book/order_pool.hpp"
#include "book/price_levels.hpp"
#include "protocol/itch_messages.hpp"

namespace llob {

struct BookStats {
    uint64_t adds;
    uint64_t executions;
    uint64_t partial_executions;
    uint64_t deletes;
    uint64_t state_changes;

    uint64_t duplicate_order_ids;   // an id already active on that side
    uint64_t unknown_order_ids;     // execute or delete for an order we never saw
    uint64_t market_orders;         // price sentinel: no level to rest on
    uint64_t oversized_executions;  // executed quantity above what was left

    BookStats()
        : adds(0), executions(0), partial_executions(0), deletes(0),
          state_changes(0), duplicate_order_ids(0), unknown_order_ids(0),
          market_orders(0), oversized_executions(0) {}
};

class OrderBook {
public:
    explicit OrderBook(const Config& cfg)
        : orders_(cfg.max_active_orders), levels_(cfg.max_price_levels),
          state_(STATE_UNKNOWN), last_exec_price_(0),
          have_last_exec_(false), healthy_(true), day_closed_(false) {}

    // False once the pool filled up or a price fell outside the band. The book
    // is wrong from that point on, so the strategy has to stop rather than
    // trade on it.
    bool healthy() const { return healthy_; }

    BookState state()      const { return state_; }
    bool      day_closed() const { return day_closed_; }

    bool  has_best(Side s)      const { return levels_.has_best(s); }
    Price best(Side s)          const { return levels_.best(s); }
    Quantity best_quantity(Side s) const { return levels_.best_quantity(s); }

    bool  has_last_execution()  const { return have_last_exec_; }
    Price last_execution_price() const { return last_exec_price_; }

    Quantity quantity_at(Side s, Price p) const { return levels_.quantity_at(s, p); }
    uint32_t order_count_at(Side s, Price p) const { return levels_.order_count_at(s, p); }
    bool     occupied_at(Side s, Price p) const { return levels_.occupied_at(s, p); }

    std::size_t      live_orders() const { return orders_.size(); }
    const BookStats& stats()       const { return stats_; }

    // Timestamp group boundary. The engine calls this once the previous group
    // has been evaluated; everything the journal recorded for it goes stale.
    void begin_group() { levels_.begin_group(); }

    // What the current group did to a price, for the strategy's rules 5 and 6.
    bool changed_this_group(Price p) const { return levels_.changed_this_group(p); }
    bool flipped_this_group(Price p) const { return levels_.flipped_this_group(p); }
    Side side_before_group(Price p) const { return levels_.side_before_group(p); }

    void add_order(const AddOrderMsg& m) {
        ++stats_.adds;

        // A market order has no price, so it never rests on a level. Letting
        // the sentinel through would index far outside the band.
        if (m.price == PRICE_NOT_AVAILABLE) { ++stats_.market_orders; return; }

        const InsertResult r = orders_.insert(m.side, m.order_id, m.price, m.quantity);
        if (r == INSERT_DUPLICATE) {
            // Keep the order already resting and ignore the new one: the book
            // we have is the one the earlier messages described.
            ++stats_.duplicate_order_ids;
            return;
        }
        if (r == INSERT_FULL) { healthy_ = false; return; }

        if (!levels_.add_order(m.side, m.price, m.quantity)) {
            orders_.remove(m.side, m.order_id);   // undo, keep the two in step
            healthy_ = false;
        }
    }

    void order_executed(const OrderExecutedMsg& m) {
        ++stats_.executions;

        Order* o = orders_.find(m.side, m.order_id);
        if (!o) { ++stats_.unknown_order_ids; return; }

        // Read the price before anything is removed: the message carries no
        // price of its own, and this is where last_execution_price comes from.
        const Price    price = o->price;
        const Quantity left  = o->remaining;

        last_exec_price_ = price;
        have_last_exec_  = true;

        Quantity filled = m.quantity;
        if (filled > left) { ++stats_.oversized_executions; filled = left; }

        if (filled < left) {
            o->remaining -= filled;
            levels_.take_quantity(m.side, price, filled);
            ++stats_.partial_executions;
            return;
        }

        // Filled in full. No Order Delete follows a complete fill, so removing
        // it here is the only thing that takes it off the book.
        levels_.remove_order(m.side, price, left);
        orders_.remove(m.side, m.order_id);
    }

    void order_delete(const OrderDeleteMsg& m) {
        ++stats_.deletes;

        Order* o = orders_.find(m.side, m.order_id);
        if (!o) { ++stats_.unknown_order_ids; return; }

        levels_.remove_order(m.side, o->price, o->remaining);
        orders_.remove(m.side, m.order_id);
    }

    void book_state(const BookStateMsg& m) {
        ++stats_.state_changes;
        state_ = m.state;
        if (m.state == STATE_MARJ_YAYIN_KAPANIS) day_closed_ = true;
    }

    // Cheap checks, meant for a completed timestamp group rather than for every
    // message: inside a group the book may legitimately cross, because deleting
    // the old bid and adding the new ask are two separate messages.
    bool invariants_hold() const {
        if (levels_.out_of_band()) return false;
        if (has_best(SIDE_BUY) && has_best(SIDE_SELL))
            if (best(SIDE_BUY) >= best(SIDE_SELL)) return false;
        return true;
    }

    // Walks every live order and compares the totals against the level
    // aggregates. Linear in the number of orders, so tests only.
    bool verify_level_sums() const {
        for (int s = 0; s < SIDE_COUNT; ++s) {
            const Side side = (Side)s;
            SumCheck check(levels_, side);
            orders_.for_each(side, check);
            if (!check.ok) return false;
        }
        return true;
    }

private:
    // A functor rather than a lambda: C++11 lambdas are fine, but this keeps
    // the capture explicit and the failure reason easy to extend.
    struct SumCheck {
        const PriceLevels& levels;
        Side side;
        bool ok;
        SumCheck(const PriceLevels& l, Side s) : levels(l), side(s), ok(true) {}

        void operator()(const Order& o) {
            if (!levels.occupied_at(side, o.price)) ok = false;
            if (levels.quantity_at(side, o.price) < o.remaining) ok = false;
        }
    };

    OrderPool   orders_;
    PriceLevels levels_;
    BookState   state_;
    Price       last_exec_price_;
    bool        have_last_exec_;
    bool        healthy_;
    bool        day_closed_;
    BookStats   stats_;
};

} // namespace llob

#endif // LLOB_BOOK_ORDER_BOOK_HPP
