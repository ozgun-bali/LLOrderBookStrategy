#ifndef LLOB_PROTOCOL_ITCH_PARSER_HPP
#define LLOB_PROTOCOL_ITCH_PARSER_HPP

// ITCH message dispatch.
//
// Sits between MoldUDP64 and the order book: it is the handler MoldSession
// delivers blocks to, and it hands decoded messages upward.
//
// Owns current_second_, updated from 'T' Seconds messages. 'T' is tracked but
// never applied to the book: the nanosecond field in every other message is
// relative to the last 'T', so a composed 64-bit timestamp is the only value
// that stays ordered across a second boundary.
//
// Dispatch order matters for throughput: switch on the type byte, validate the
// length, then read the Order Book ID and bail if it is not the target. Most
// messages in the feed belong to other books and must cost as little as possible.
//
// Handled: 'O' 'A' 'E' 'D', plus 'T' for timekeeping. Everything else is skipped.
// Skipped types that would matter on real data (document in README):
//   'C' Order Executed with Price - also reduces order quantity
//   'Y' Order Book Flush          - clears the whole book
//
// Templated on the handler for the same reasons as MoldSession: the call
// inlines, and tests can substitute a recorder for the order book.
// Handler needs four methods:
//   void on_add_order     (Timestamp, const AddOrderMsg&);
//   void on_order_executed(Timestamp, const OrderExecutedMsg&);
//   void on_order_delete  (Timestamp, const OrderDeleteMsg&);
//   void on_book_state    (Timestamp, const BookStateMsg&);

#include "core/types.hpp"
#include "core/byte_reader.hpp"
#include "protocol/itch_messages.hpp"

namespace llob {

static const uint64_t NANOS_PER_SECOND = 1000000000ULL;

struct ItchStats {
    uint64_t messages;          // every ITCH message seen, any book
    uint64_t target_messages;   // messages for the configured order book
    uint64_t invalid_messages;  // wrong length, or a side byte that is neither B nor S
    uint64_t skipped_types;     // types this build does not handle, 'C' and 'Y' among them
    uint64_t seconds_messages;  // 'T' markers consumed for timekeeping
    uint64_t before_first_second;

    ItchStats()
        : messages(0), target_messages(0), invalid_messages(0),
          skipped_types(0), seconds_messages(0), before_first_second(0) {}
};

template <typename Handler>
class ItchParser {
public:
    ItchParser(Handler& handler, OrderBookId target)
        : handler_(handler), target_(target),
          current_second_(0), have_second_(false) {}

    const ItchStats& stats() const { return stats_; }

    // Messages carry nanoseconds since the last 'T'. Without one, absolute time
    // is unknown; the feed normally opens with 'T', so this only matters when
    // the capture starts mid-stream.
    bool have_second() const { return have_second_; }

    // Passed straight through. The parser has no opinion on sequence health,
    // but the strategy below it needs to hear about a gap the moment the
    // framing layer notices one.
    void on_sequence_reliable(bool reliable) {
        handler_.on_sequence_reliable(reliable);
    }

    void on_message(const ByteReader& m) {
        if (!m.has(1)) { ++stats_.invalid_messages; return; }
        ++stats_.messages;

        const uint8_t type = m.u8(OFF_MSG_TYPE);

        const std::size_t want = expected_length(type);
        if (want == 0) { ++stats_.skipped_types; return; }
        if (m.size() != want) { ++stats_.invalid_messages; return; }

        if (type == MSG_SECONDS) {
            current_second_ = m.be32(OFF_SECOND);
            have_second_    = true;
            ++stats_.seconds_messages;
            return;                      // timekeeping only, never reaches the book
        }

        // Early filter. Everything below this line runs only for the target book.
        if (m.be32(bookIdOffset(type)) != target_) return;
        ++stats_.target_messages;

        if (!have_second_) ++stats_.before_first_second;
        const Timestamp ts = composeTimestamp(m.be32(OFF_NANOS));

        switch (type) {
            case MSG_ADD_ORDER: {
                AddOrderMsg a;
                decode_add_order(m, a);
                if (a.side == SIDE_INVALID) { ++stats_.invalid_messages; return; }
                handler_.on_add_order(ts, a);
                break;
            }
            case MSG_ORDER_EXECUTED: {
                OrderExecutedMsg e;
                decode_order_executed(m, e);
                if (e.side == SIDE_INVALID) { ++stats_.invalid_messages; return; }
                handler_.on_order_executed(ts, e);
                break;
            }
            case MSG_ORDER_DELETE: {
                OrderDeleteMsg d;
                decode_order_delete(m, d);
                if (d.side == SIDE_INVALID) { ++stats_.invalid_messages; return; }
                handler_.on_order_delete(ts, d);
                break;
            }
            case MSG_BOOK_STATE: {
                BookStateMsg s;
                decode_book_state(m, s);
                handler_.on_book_state(ts, s);
                break;
            }
            default:
                break;   // unreachable: expected_length already rejected the rest
        }
    }

private:
    // 'O' carries the book id at 5, the order messages carry it at 13.
    static std::size_t bookIdOffset(uint8_t type) {
        return (type == MSG_BOOK_STATE) ? OFF_STATE_BOOK_ID : OFF_ADD_BOOK_ID;
    }

    Timestamp composeTimestamp(uint32_t nanos) const {
        return (Timestamp)current_second_ * NANOS_PER_SECOND + nanos;
    }

    Handler&    handler_;
    OrderBookId target_;
    uint32_t    current_second_;
    bool        have_second_;
    ItchStats   stats_;
};

} // namespace llob

#endif // LLOB_PROTOCOL_ITCH_PARSER_HPP
