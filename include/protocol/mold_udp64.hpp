#ifndef LLOB_PROTOCOL_MOLD_UDP64_HPP
#define LLOB_PROTOCOL_MOLD_UDP64_HPP

// MoldUDP64 framing and sequence tracking.
//
// Header is 20 bytes: Session (10, Alpha) | Sequence Number (8, BE) |
// Message Count (2, BE). Then Message Count blocks of [2-byte BE length][payload].
//
// Sequence numbers count MESSAGES, not packets:
//   expected_next = sequence + message_count
//
// Special message counts:
//   0x0000 -> heartbeat (no blocks)
//   0xFFFF -> end of session
//
// Templated on the handler so the sequence logic can be tested against a
// recorder instead of a live order book, and so the call inlines rather than
// going through a vtable on the hot path. Handler needs one method:
//   void on_message(const ByteReader& msg);

#include "core/types.hpp"
#include "core/byte_reader.hpp"

#include <cstring>

namespace llob {

static const std::size_t MOLD_HEADER_LEN   = 20;
static const std::size_t MOLD_OFF_SESSION  = 0;
static const std::size_t MOLD_LEN_SESSION  = 10;
static const std::size_t MOLD_OFF_SEQUENCE = 10;
static const std::size_t MOLD_OFF_COUNT    = 18;

static const uint16_t MOLD_COUNT_HEARTBEAT      = 0x0000;
static const uint16_t MOLD_COUNT_END_OF_SESSION = 0xFFFF;

struct MoldStats {
    uint64_t packets;
    uint64_t messages;          // blocks actually handed to the handler
    uint64_t sequence_gaps;
    uint64_t duplicates;        // whole packets or leading blocks already seen
    uint64_t invalid_packets;
    uint64_t heartbeats;
    uint64_t session_changes;
    bool     end_of_session;

    MoldStats()
        : packets(0), messages(0), sequence_gaps(0), duplicates(0),
          invalid_packets(0), heartbeats(0), session_changes(0),
          end_of_session(false) {}
};

template <typename Handler>
class MoldSession {
public:
    explicit MoldSession(Handler& handler)
        : handler_(handler), expected_(0), have_session_(false),
          have_expected_(false), reliable_(true) {
        std::memset(session_, 0, MOLD_LEN_SESSION);
    }

    // A gap means updates were missed, and without a snapshot feed the book
    // cannot be trusted again. Stays false until the session changes.
    bool reliable() const { return reliable_; }

    const MoldStats& stats() const { return stats_; }

    void process_datagram(const uint8_t* data, std::size_t length) {
        ++stats_.packets;

        ByteReader r(data, length);
        if (!r.has(MOLD_HEADER_LEN)) { ++stats_.invalid_packets; return; }

        checkSession(r);

        const uint64_t seq   = r.be64(MOLD_OFF_SEQUENCE);
        const uint16_t count = r.be16(MOLD_OFF_COUNT);

        if (count == MOLD_COUNT_END_OF_SESSION) {
            stats_.end_of_session = true;
            return;
        }

        if (count == MOLD_COUNT_HEARTBEAT) {
            ++stats_.heartbeats;
            // A heartbeat carries the sequence of the next message to come, so
            // it still exposes a gap even though it has no payload.
            if (have_expected_ && seq > expected_) {
                ++stats_.sequence_gaps;
                reliable_ = false;
                expected_ = seq;
            }
            return;
        }

        if (!have_expected_) {           // first data packet defines the origin
            expected_      = seq;
            have_expected_ = true;
        }

        const uint64_t end = seq + count;   // one past the last message
        std::size_t skip = 0;

        if (seq > expected_) {
            ++stats_.sequence_gaps;
            reliable_ = false;
        } else if (end <= expected_) {
            ++stats_.duplicates;            // every message already seen
            return;
        } else if (seq < expected_) {
            ++stats_.duplicates;            // retransmission overlapping the tail
            skip = (std::size_t)(expected_ - seq);
        }

        if (!walkBlocks(r, count, skip)) return;

        if (end > expected_) expected_ = end;
    }

private:
    // Returns false when the packet is malformed. Blocks already delivered stay
    // delivered: the damage is in the remainder, not in what was parsed cleanly.
    bool walkBlocks(const ByteReader& r, uint16_t count, std::size_t skip) {
        std::size_t off = MOLD_HEADER_LEN;

        for (uint16_t i = 0; i < count; ++i) {
            if (!r.has(off + 2)) {          // length field itself runs past the end
                ++stats_.invalid_packets;
                reliable_ = false;
                return false;
            }
            const uint16_t blockLen = r.be16(off);

            if (!r.has(off + 2 + blockLen)) {   // payload runs past the end
                ++stats_.invalid_packets;
                reliable_ = false;
                return false;
            }

            if (i >= skip) {
                ++stats_.messages;
                handler_.on_message(r.slice(off + 2, blockLen));
            }
            off += 2 + blockLen;
        }

        // Blocks that do not add up to the datagram mean the count or the
        // lengths lied, even though each block happened to fit.
        if (off != r.size()) ++stats_.invalid_packets;
        return true;
    }

    void checkSession(const ByteReader& r) {
        const uint8_t* s = r.raw(MOLD_OFF_SESSION);

        if (!have_session_) {
            std::memcpy(session_, s, MOLD_LEN_SESSION);
            have_session_ = true;
            return;
        }
        if (std::memcmp(session_, s, MOLD_LEN_SESSION) == 0) return;

        // A different session is a different stream: sequence numbering restarts
        // and nothing carried over from the old one can be trusted.
        ++stats_.session_changes;
        std::memcpy(session_, s, MOLD_LEN_SESSION);
        have_expected_ = false;
        expected_      = 0;
        reliable_      = true;
    }

    Handler&  handler_;
    uint8_t   session_[MOLD_LEN_SESSION];
    uint64_t  expected_;
    bool      have_session_;
    bool      have_expected_;
    bool      reliable_;
    MoldStats stats_;
};

} // namespace llob

#endif // LLOB_PROTOCOL_MOLD_UDP64_HPP
