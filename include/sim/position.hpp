#ifndef LLOB_SIM_POSITION_HPP
#define LLOB_SIM_POSITION_HPP

// Position and cash flow, in whole kurus.
//
// Two running numbers carry everything. The position is how many lots are held
// net, with no memory of what they cost; the cash flow is what moved through
// the account, which is where the prices are. Together they price the book at
// any moment, and neither needs the individual trades kept.
//
//   BUY : cash_flow -= quantity * price;  position += quantity;
//   SELL: cash_flow += quantity * price;  position -= quantity;
//
// Only our own simulated fills reach this. Public ITCH executions belong to
// other participants and must never touch it.
//
// No floating point anywhere. Every product is widened to int64_t first: a
// Quantity is unsigned and a Price is signed, so multiplying them directly
// would promote the price to unsigned and turn a negative one into a vast
// positive number.

#include "core/types.hpp"

#include <cassert>

namespace llob {

class PositionTracker {
public:
    PositionTracker()
        : quantity_(0), cash_flow_(0), final_pnl_(0),
          fills_(0), mark_price_(0), day_marked_(false),
          pnl_known_(false), have_mark_(false) {}

    // Bound by the engine so the strategy can read the live position when it
    // sizes an order against the limits.
    const Position& position_ref() const { return quantity_; }

    Position quantity()  const { return quantity_; }
    Cash     cash_flow() const { return cash_flow_; }
    uint64_t fills()     const { return fills_; }

    bool day_marked() const { return day_marked_; }
    bool pnl_known()  const { return pnl_known_; }

    // What the open position was valued at, for a report that reconciles.
    bool  have_mark_price() const { return have_mark_; }
    Price mark_price()      const { return mark_price_; }
    Cash final_pnl()  const { assert(pnl_known_); return final_pnl_; }

    void apply_fill(Side side, Price price, Quantity qty) {
        assert(side == SIDE_BUY || side == SIDE_SELL);

        const int64_t notional = mul(qty, price);
        if (side == SIDE_BUY) {
            cash_flow_ = sub(cash_flow_, notional);
            quantity_ += (Position)qty;
        } else {
            cash_flow_ = add(cash_flow_, notional);
            quantity_ -= (Position)qty;
        }
        ++fills_;
    }

    // Taken when P_MARJ_YAYIN_KAPANIS arrives, not at end of file. The feed
    // keeps running after the close - auctions, more executions - and the last
    // traded price would drift away from the one the day ended on.
    void mark_end_of_day(bool have_last_price, Price last_price) {
        if (day_marked_) return;        // the first close is the one that counts
        day_marked_ = true;

        if (quantity_ == 0) {
            // Flat, so there is nothing to value: the cash flow is the result.
            final_pnl_ = cash_flow_;
            pnl_known_ = true;
        } else if (have_last_price) {
            have_mark_ = true;
            mark_price_ = last_price;
            // Both sides are already signed, so a short position gives a
            // negative mark and the sign takes care of itself.
            final_pnl_ = add(cash_flow_, mulSigned(quantity_, (int64_t)last_price));
            pnl_known_ = true;
        } else {
            // A position still open and no price the feed ever traded at. There
            // is no honest number to print, so none is printed.
            pnl_known_ = false;
        }
    }

private:
    // Widen before multiplying. A Quantity is unsigned and a Price is signed;
    // multiplying them as they are would promote the price to unsigned.
    static int64_t mul(Quantity q, Price p) {
        return mulSigned((int64_t)q, (int64_t)p);
    }

    // __builtin_*_overflow does the arithmetic and reports whether it wrapped.
    // On x86-64 that is the instruction you wanted anyway plus its overflow
    // flag, so with the flag unread in a release build the compiler emits a
    // plain imul or add and the check costs nothing.
    //
    // The operation deliberately sits outside the assert: code with a side
    // effect inside one disappears with NDEBUG, and here the result is the
    // side effect.
    static int64_t mulSigned(int64_t a, int64_t b) {
        int64_t out = 0;
        const bool overflowed = __builtin_mul_overflow(a, b, &out);
        assert(!overflowed);
        (void)overflowed;
        return out;
    }

    static int64_t add(int64_t a, int64_t b) {
        int64_t out = 0;
        const bool overflowed = __builtin_add_overflow(a, b, &out);
        assert(!overflowed);
        (void)overflowed;
        return out;
    }

    static int64_t sub(int64_t a, int64_t b) {
        int64_t out = 0;
        const bool overflowed = __builtin_sub_overflow(a, b, &out);
        assert(!overflowed);
        (void)overflowed;
        return out;
    }

    Position quantity_;
    Cash     cash_flow_;
    Cash     final_pnl_;
    uint64_t fills_;
    Price    mark_price_;
    bool     day_marked_;
    bool     pnl_known_;
    bool     have_mark_;
};

} // namespace llob

#endif // LLOB_SIM_POSITION_HPP
