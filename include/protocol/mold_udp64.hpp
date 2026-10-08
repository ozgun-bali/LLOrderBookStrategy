#ifndef LLOB_PROTOCOL_MOLD_UDP64_HPP
#define LLOB_PROTOCOL_MOLD_UDP64_HPP

// MoldUDP64 framing ve sequence takibi.
//
// Header 20 byte: Session (10, Alpha) | Sequence Number (8, BE) |
// Message Count (2, BE). Ardından Message Count adet block geliyor, her biri
// [2 byte BE length][payload] biçiminde.
//
// Sequence number PACKET değil, MESSAGE sayıyor:
//   expected_next = sequence + message_count
//
// Özel message count değerleri:
//   0x0000 -> heartbeat (block yok)
//   0xFFFF -> end of session
//
// Handler üzerinden template: böylece sequence logic'i canlı bir order book
// yerine bir recorder'a karşı test edilebiliyor, ve çağrı hot path'te vtable
// üzerinden gitmek yerine inline oluyor. Handler'ın tek bir method'a ihtiyacı var:
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
    uint64_t messages;          // handler'a gerçekten teslim edilen block'lar
    uint64_t sequence_gaps;
    uint64_t duplicates;        // tamamı ya da baş kısmı daha önce görülmüş packet'ler
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

            if (have_expected_ && seq > expected_) {
                ++stats_.sequence_gaps;
                setReliable(false);
                expected_ = seq;
            }
            return;
        }

        if (!have_expected_) {
            expected_      = seq;
            have_expected_ = true;
        }

        const uint64_t end = seq + count;
        std::size_t skip = 0;

        if (seq > expected_) {
            ++stats_.sequence_gaps;
            setReliable(false);
        } else if (end <= expected_) {
            ++stats_.duplicates;
            return;
        } else if (seq < expected_) {
            ++stats_.duplicates;
            skip = (std::size_t)(expected_ - seq);
        }

        if (!walkBlocks(r, count, skip)) return;

        if (end > expected_) expected_ = end;
    }

private:
    // Reliability bilgisi poll edilmiyor, push ediliyor. Bir sequence gap packet
    // frame'lenirken fark ediliyor, yani içindeki hiçbir message strategy'ye
    // ulaşmadan önce; o anda handler'a haber vermek, strategy'nin bunu zamanında
    // görmesinin tek yolu.
    void setReliable(bool v) {
        if (reliable_ == v) return;
        reliable_ = v;
        handler_.on_sequence_reliable(v);
    }

    // Packet malformed ise false döner. Daha önce teslim edilmiş block'lar teslim
    // edilmiş kalıyor: hasar geri kalanda, temiz parse edilmiş kısımda değil.
    bool walkBlocks(const ByteReader& r, uint16_t count, std::size_t skip) {
        std::size_t off = MOLD_HEADER_LEN;

        for (uint16_t i = 0; i < count; ++i) {
            if (!r.has(off + 2)) {
                ++stats_.invalid_packets;
                setReliable(false);
                return false;
            }
            const uint16_t blockLen = r.be16(off);

            if (!r.has(off + 2 + blockLen)) {   // payload packet'in dışına taşıyor
                ++stats_.invalid_packets;
                setReliable(false);
                return false;
            }

            if (i >= skip) {
                ++stats_.messages;
                handler_.on_message(r.slice(off + 2, blockLen));
            }
            off += 2 + blockLen;
        }

        // Block'ların toplamı datagram'ı tam doldurmuyorsa, her block tek tek sığmış
        // olsa bile count ya da length'lerden biri yalan söylemiş demektir.
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

        // Farklı bir session farklı bir stream demek: sequence numaralandırması
        // baştan başlıyor ve eskisinden taşınan hiçbir şeye güvenilemez.
        ++stats_.session_changes;
        std::memcpy(session_, s, MOLD_LEN_SESSION);
        have_expected_ = false;
        expected_      = 0;
        setReliable(true);
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
