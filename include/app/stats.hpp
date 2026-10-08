#ifndef LLOB_APP_STATS_HPP
#define LLOB_APP_STATS_HPP

// The run's result, and the checksum over it.
//
// The checksum does two jobs. It proves determinism: the same input and config
// must produce the same value every time. And it keeps the optimiser honest in
// benchmark mode, where nothing is printed - without a value that leaves the
// program, -O3 is free to decide the whole computation is dead and delete it,
// and the measurement would be of nothing.
//
// FNV-1a is enough for both. It is order dependent, cheap, and not pretending
// to be cryptographic.

#include "core/types.hpp"
#include "app/order_log.hpp"

namespace llob {

static const uint64_t FNV_OFFSET = 14695981039346656037ULL;
static const uint64_t FNV_PRIME  = 1099511628211ULL;

inline uint64_t fnv1a_byte(uint64_t hash, uint8_t b) {
    return (hash ^ (uint64_t)b) * FNV_PRIME;
}

inline uint64_t fnv1a_u64(uint64_t hash, uint64_t v) {
    for (int i = 7; i >= 0; --i)
        hash = fnv1a_byte(hash, (uint8_t)((v >> (i * 8)) & 0xFF));
    return hash;
}

// Everything the assignment asks the program to print, gathered in one place
// so the reporter takes one argument and the checksum has one thing to fold.
struct RunSummary {
    Position position;
    Cash     cash_flow;
    bool     pnl_known;
    Cash     final_pnl;

    bool  have_last_execution;
    Price last_execution_price;

    // The price the day was marked at, captured when P_MARJ_YAYIN_KAPANIS
    // arrived. The feed keeps trading afterwards, so this is usually not the
    // last price in the file, and without it the P&L does not appear to add up.
    bool  have_mark_price;
    Price mark_price;

    bool     have_best_bid;
    Price    best_bid;
    Quantity best_bid_quantity;
    bool     have_best_ask;
    Price    best_ask;
    Quantity best_ask_quantity;

    uint64_t packets;
    uint64_t itch_messages;
    uint64_t target_messages;
    uint64_t processed_message_count;
    uint64_t sequence_gap_count;
    uint64_t invalid_message_count;
    uint64_t generated_order_count;
    uint64_t groups;
    uint64_t fills;

    uint64_t checksum;

    // Why the strategy stayed quiet. Not part of the required output, but the
    // difference between "no opportunity arose" and "a rule is wrong" is only
    // visible here.
    uint64_t evaluations;
    uint64_t skip_state;
    uint64_t skip_sequence;
    uint64_t skip_one_sided;
    uint64_t skip_spread;
    uint64_t skip_not_this_group;
    uint64_t skip_flip;
    uint64_t skip_no_direction;
    uint64_t skip_duplicate;
    uint64_t skip_position_limit;

    RunSummary()
        : position(0), cash_flow(0), pnl_known(false), final_pnl(0),
          have_last_execution(false), last_execution_price(0),
          have_mark_price(false), mark_price(0),
          have_best_bid(false), best_bid(0), best_bid_quantity(0),
          have_best_ask(false), best_ask(0), best_ask_quantity(0),
          packets(0), itch_messages(0), target_messages(0),
          processed_message_count(0), sequence_gap_count(0),
          invalid_message_count(0), generated_order_count(0),
          groups(0), fills(0), checksum(0),
          evaluations(0), skip_state(0), skip_sequence(0), skip_one_sided(0),
          skip_spread(0), skip_not_this_group(0), skip_flip(0),
          skip_no_direction(0), skip_duplicate(0), skip_position_limit(0) {}
};

// Folds the orders first, in the order they were produced, then the end state.
// Any difference in what was decided, or in what the book ended up as, moves
// the value.
inline uint64_t compute_checksum(const RunSummary& s, const OrderLog& log) {
    uint64_t h = FNV_OFFSET;

    for (std::size_t i = 0; i < log.size(); ++i) {
        const OrderIntent& o = log[i];
        h = fnv1a_u64(h, o.timestamp);
        h = fnv1a_u64(h, (uint64_t)o.side);
        h = fnv1a_u64(h, (uint64_t)(uint32_t)o.price);   // signed, keep the bits
        h = fnv1a_u64(h, o.quantity);
        h = fnv1a_u64(h, (uint64_t)o.reason);
    }

    h = fnv1a_u64(h, (uint64_t)s.position);
    h = fnv1a_u64(h, (uint64_t)s.cash_flow);
    h = fnv1a_u64(h, s.pnl_known ? (uint64_t)s.final_pnl : 0ULL);
    h = fnv1a_u64(h, (uint64_t)(uint32_t)s.last_execution_price);
    h = fnv1a_u64(h, s.processed_message_count);
    h = fnv1a_u64(h, s.sequence_gap_count);
    h = fnv1a_u64(h, s.invalid_message_count);
    h = fnv1a_u64(h, s.generated_order_count);
    return h;
}

} // namespace llob

#endif // LLOB_APP_STATS_HPP
