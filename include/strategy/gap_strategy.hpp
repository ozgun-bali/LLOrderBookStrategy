#ifndef LLOB_STRATEGY_GAP_STRATEGY_HPP
#define LLOB_STRATEGY_GAP_STRATEGY_HPP

// One-tick gap detection, evaluated once per completed timestamp group.
//
// The bet is that a level vanished by accident - someone cancelled, or a small
// order swept it - while the market still agrees on the price. Step into the
// hole, become the best quote, and collect the spread. That only pays if you
// are first, which is why the whole project is built around latency.
//
// Every condition below exists to protect that bet:
//
//   1. P_SUREKLI_ISLEM        only continuous trading has a live two-sided book
//   2. sequence stream valid  a book built on missed messages is fiction
//   3. a level on both sides  no spread to measure otherwise
//   4. exactly two ticks      one empty step; three or more is a thin market,
//                             not a hole that just opened
//   5. created by this group  the opportunity is in the event, not the state.
//                             a gap that has been open for a while was already
//                             everyone else's to take
//   6. no flip at that price  if the price went bid to ask and then emptied,
//                             the market moved through the level. Filling that
//                             is stepping in front of a move, not replacing
//                             liquidity
//   7. one order per gap      the same hole would otherwise fire every group
//
// Templated on the sink for the same reason as the layers below: the call
// inlines, and tests can record intents without a fill simulator behind them.

#include "core/config.hpp"
#include "core/types.hpp"
#include "book/order_book.hpp"
#include "strategy/order_intent.hpp"

namespace llob {

struct StrategyStats {
    uint64_t evaluations;
    uint64_t intents;

    // Why nothing was sent. Separated so the report can say what the day
    // actually looked like rather than just how many orders came out.
    uint64_t skip_state;
    uint64_t skip_sequence;
    uint64_t skip_one_sided;
    uint64_t skip_spread;
    uint64_t skip_not_this_group;
    uint64_t skip_flip;
    uint64_t skip_no_direction;
    uint64_t skip_duplicate;
    uint64_t skip_position_limit;

    StrategyStats()
        : evaluations(0), intents(0), skip_state(0), skip_sequence(0),
          skip_one_sided(0), skip_spread(0), skip_not_this_group(0),
          skip_flip(0), skip_no_direction(0), skip_duplicate(0),
          skip_position_limit(0) {}
};

template <typename Sink>
class GapStrategy {
public:
    GapStrategy(const Config& cfg, Sink& sink)
        : cfg_(cfg), sink_(sink), last_price_(0), last_side_(SIDE_INVALID),
          have_last_(false) {}

    const StrategyStats& stats() const { return stats_; }

    // Called once per completed timestamp group, never between messages of one.
    void evaluate(const OrderBook& book, bool sequence_ok,
                  Position position, Timestamp ts) {
        ++stats_.evaluations;

        forgetIfGapClosed(book);

        if (book.state() != STATE_SUREKLI_ISLEM || book.day_closed()) {
            ++stats_.skip_state; return;
        }
        if (!sequence_ok || !book.healthy()) {
            ++stats_.skip_sequence; return;
        }
        if (!book.has_best(SIDE_BUY) || !book.has_best(SIDE_SELL)) {
            ++stats_.skip_one_sided; return;
        }

        const Price bid = book.best(SIDE_BUY);
        const Price ask = book.best(SIDE_SELL);
        if (ask - bid != 2 * cfg_.tick_size) {
            ++stats_.skip_spread; return;
        }

        const Price gap = bid + cfg_.tick_size;

        // Rule 5. Untouched means the level is exactly as the group found it,
        // so the hole was already there and is nobody's opportunity now.
        if (!book.changed_this_group(gap)) {
            ++stats_.skip_not_this_group; return;
        }
        // Rule 6.
        if (book.flipped_this_group(gap)) {
            ++stats_.skip_flip; return;
        }

        // Refill the side that emptied: an ask vanished, so we offer.
        const Side was = book.side_before_group(gap);
        Side side;
        IntentReason reason;
        if (was == SIDE_SELL)      { side = SIDE_SELL; reason = REASON_GAP_WAS_ASK; }
        else if (was == SIDE_BUY)  { side = SIDE_BUY;  reason = REASON_GAP_WAS_BID; }
        else { ++stats_.skip_no_direction; return; }   // it was empty, or crossed

        // Rule 7.
        if (have_last_ && last_price_ == gap && last_side_ == side) {
            ++stats_.skip_duplicate; return;
        }

        const Quantity qty = sendQuantity(side, position);
        if (qty == 0) { ++stats_.skip_position_limit; return; }

        OrderIntent intent;
        intent.timestamp = ts;
        intent.side      = side;
        intent.price     = gap;
        intent.quantity  = qty;
        intent.reason    = reason;

        ++stats_.intents;
        last_price_ = gap;
        last_side_  = side;
        have_last_  = true;

        sink_.on_order_intent(intent);
    }

private:
    // The remembered gap counts as the same opportunity until it genuinely
    // closes. Once something rests at that price again, a later reopening is a
    // new event and may signal afresh.
    void forgetIfGapClosed(const OrderBook& book) {
        if (!have_last_) return;
        if (book.occupied_at(SIDE_BUY, last_price_) ||
            book.occupied_at(SIDE_SELL, last_price_)) {
            have_last_ = false;
        }
    }

    // Room left before the position limit, capped at the configured size.
    // Everything is signed: minimum_position is normally negative, and an
    // unsigned step anywhere here would turn a short limit into no limit.
    Quantity sendQuantity(Side side, Position position) const {
        const int64_t room = (side == SIDE_BUY)
            ? (int64_t)cfg_.maximum_position - (int64_t)position
            : (int64_t)position - (int64_t)cfg_.minimum_position;

        if (room <= 0) return 0;

        const int64_t want = (int64_t)cfg_.order_quantity;
        return (Quantity)(room < want ? room : want);
    }

    const Config& cfg_;
    Sink&         sink_;

    Price         last_price_;
    Side          last_side_;
    bool          have_last_;
    StrategyStats stats_;
};

} // namespace llob

#endif // LLOB_STRATEGY_GAP_STRATEGY_HPP
