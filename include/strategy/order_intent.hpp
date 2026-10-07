#ifndef LLOB_STRATEGY_ORDER_INTENT_HPP
#define LLOB_STRATEGY_ORDER_INTENT_HPP

// The strategy's output, and where it goes.
//
// The strategy never writes to a file. It hands an intent to a sink, and what
// the sink does with it is the sink's business: in the real run a fill
// simulator takes it, in tests a recorder collects it, and in the benchmark it
// folds into a checksum and nothing else. Writing to a file from the hot path
// would be a system call per signal and would swamp the latency being measured.
//
// The reason is an enum, not text. Formatting the CSV column happens outside
// the timed region; carrying a word around would mean building strings on the
// hot path, which the assignment rules out.

#include "core/types.hpp"

namespace llob {

enum IntentReason {
    REASON_NONE = 0,
    REASON_GAP_WAS_ASK,   // the level that vanished was an ask, so we sell into it
    REASON_GAP_WAS_BID    // it was a bid, so we buy
};

// Matches the struct in the assignment, with the reason tag the CSV needs.
struct OrderIntent {
    Timestamp    timestamp;
    Side         side;
    Price        price;
    Quantity     quantity;
    IntentReason reason;

    OrderIntent()
        : timestamp(0), side(SIDE_INVALID), price(0), quantity(0),
          reason(REASON_NONE) {}
};

inline const char* reason_text(IntentReason r) {
    switch (r) {
        case REASON_GAP_WAS_ASK: return "gap_was_ask";
        case REASON_GAP_WAS_BID: return "gap_was_bid";
        default:                 return "none";
    }
}

inline const char* side_text(Side s) {
    return s == SIDE_BUY ? "BUY" : (s == SIDE_SELL ? "SELL" : "?");
}

// Counts intents and nothing else. Useful on its own for the benchmark, where
// the work has to happen but the output must stay outside the timed region.
struct CountingSink {
    uint64_t count;
    CountingSink() : count(0) {}
    void on_order_intent(const OrderIntent&) { ++count; }
};

} // namespace llob

#endif // LLOB_STRATEGY_ORDER_INTENT_HPP
