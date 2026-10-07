#ifndef LLOB_APP_ENGINE_HPP
#define LLOB_APP_ENGINE_HPP

// Wires the pipeline together and owns the timestamp grouping.
//
//   datagram -> MoldUDP64 -> ItchParser -> Engine -> OrderBook
//                                             |
//                                             +-> GapStrategy, once per group
//
// Messages sharing an ITCH timestamp are one event as far as the exchange is
// concerned. Running the strategy between them would show it books that never
// existed: on the sample feed the largest group holds 3,919 messages, and the
// book crosses and uncrosses repeatedly inside it.
//
// So the strategy runs only on a closed group, and a group closes when a
// message arrives carrying a different timestamp - before that message is
// applied, or it would leak into the group it is ending. The last group has no
// such message behind it, which is what finalize() is for.

#include "core/types.hpp"
#include "book/order_book.hpp"
#include "protocol/itch_messages.hpp"

namespace llob {

struct EngineStats {
    uint64_t groups;            // completed timestamp groups
    uint64_t messages;          // target-book messages applied
    uint64_t invariant_breaks;  // groups that ended with a book that cannot be

    EngineStats() : groups(0), messages(0), invariant_breaks(0) {}
};

template <typename Strategy>
class Engine {
public:
    // The position is read, never written: whatever owns it - a fill simulator
    // from here on - updates it between groups.
    Engine(OrderBook& book, Strategy& strategy, const Position& position)
        : book_(book), strategy_(strategy), position_(position),
          current_ts_(0), have_ts_(false), sequence_ok_(true) {}

    const EngineStats& stats() const { return stats_; }
    bool sequence_ok() const { return sequence_ok_; }
    Timestamp current_timestamp() const { return current_ts_; }

    void on_sequence_reliable(bool reliable) { sequence_ok_ = reliable; }

    void on_add_order(Timestamp ts, const AddOrderMsg& m) {
        closeGroupIfTimestampChanged(ts);
        ++stats_.messages;
        book_.add_order(m);
    }

    void on_order_executed(Timestamp ts, const OrderExecutedMsg& m) {
        closeGroupIfTimestampChanged(ts);
        ++stats_.messages;
        book_.order_executed(m);
    }

    void on_order_delete(Timestamp ts, const OrderDeleteMsg& m) {
        closeGroupIfTimestampChanged(ts);
        ++stats_.messages;
        book_.order_delete(m);
    }

    // A state change carries a timestamp like any other message, so it closes
    // the group before it too.
    void on_book_state(Timestamp ts, const BookStateMsg& m) {
        closeGroupIfTimestampChanged(ts);
        ++stats_.messages;
        book_.book_state(m);
    }

    // End of input. The last group is still open and nothing is coming to
    // close it, so it is evaluated here. Easy to forget, and forgetting it
    // silently drops whatever signal the final group held.
    void finalize() {
        if (!have_ts_) return;
        closeGroup();
        have_ts_ = false;
    }

private:
    void closeGroupIfTimestampChanged(Timestamp ts) {
        if (have_ts_ && ts == current_ts_) return;   // still the same event
        if (have_ts_) closeGroup();
        current_ts_ = ts;
        have_ts_    = true;
    }

    void closeGroup() {
        // Checked here rather than after every message: inside a group the
        // book may legitimately cross, because removing the old bid and adding
        // the new ask are two separate messages.
        if (!book_.invariants_hold()) ++stats_.invariant_breaks;

        strategy_.evaluate(book_, sequence_ok_, position_, current_ts_);

        // Only now does the journal forget the group, after the strategy has
        // read what it recorded.
        book_.begin_group();
        ++stats_.groups;
    }

    OrderBook&      book_;
    Strategy&       strategy_;
    const Position& position_;

    Timestamp   current_ts_;
    bool        have_ts_;
    bool        sequence_ok_;
    EngineStats stats_;
};

} // namespace llob

#endif // LLOB_APP_ENGINE_HPP
