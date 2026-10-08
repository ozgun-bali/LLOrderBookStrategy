#include "test_framework.hpp"
#include "message_builder.hpp"
#include "protocol/mold_udp64.hpp"
#include "protocol/itch_messages.hpp"

#include <vector>

using namespace llob;
using namespace llobtest;

namespace {

// ITCH parser'ın yerine geçiyor ki sequence logic'i tek başına test edilebilsin.
struct Recorder {
    struct Seen { uint8_t type; OrderId id; std::size_t len; };
    std::vector<Seen> seen;

    void on_sequence_reliable(bool) {}

    void on_message(const ByteReader& m) {
        Seen s;
        s.type = m.u8(0);
        s.len  = m.size();
        s.id   = m.has(13) ? m.be64(5) : 0;
        seen.push_back(s);
    }
};

// Order id'leri aynı zamanda message etiketi, böylece assertion'lar neyin
// geldiğini adıyla söyleyebiliyor.
Bytes del(OrderId id) { return deleted(1u, id, 73616u, SIDE_BUY); }

Bytes packetOf(uint64_t seq, const std::vector<Bytes>& msgs) {
    return serialize(make_packet("BIST01", seq, msgs));
}

void feed(MoldSession<Recorder>& s, const Bytes& wire) {
    s.process_datagram(&wire[0], wire.size());
}

} // namespace

TEST(mold_delivers_every_block_of_an_in_order_packet) {
  Recorder rec;
  MoldSession<Recorder> s(rec);

  feed(s, packetOf(1, { del(10), del(11), del(12) }));

  REQUIRE(rec.seen.size() == 3);
  CHECK_EQ(rec.seen[0].id, 10ull);
  CHECK_EQ(rec.seen[1].id, 11ull);
  CHECK_EQ(rec.seen[2].id, 12ull);
  CHECK_EQ(rec.seen[0].type, (uint8_t)'D');
  CHECK_EQ(rec.seen[0].len, LEN_ORDER_DELETE);
  CHECK_EQ(s.stats().messages, 3ull);
  CHECK(s.reliable());
}

TEST(mold_follows_the_sequence_across_consecutive_packets) {
  Recorder rec;
  MoldSession<Recorder> s(rec);

  feed(s, packetOf(1, { del(1), del(2) }));   // 1 ve 2'yi tüketiyor
  feed(s, packetOf(3, { del(3) }));           // beklenen sonraki 3
  feed(s, packetOf(4, { del(4), del(5) }));

  CHECK_EQ(rec.seen.size(), 5);
  CHECK_EQ(s.stats().sequence_gaps, 0ull);
  CHECK_EQ(s.stats().duplicates, 0ull);
  CHECK(s.reliable());
}

TEST(mold_flags_a_gap_and_stops_trusting_the_book) {
  Recorder rec;
  MoldSession<Recorder> s(rec);

  feed(s, packetOf(1, { del(1), del(2) }));   // beklenen 3 oluyor
  feed(s, packetOf(7, { del(7) }));           // 3..6 hiç gelmedi

  CHECK_EQ(s.stats().sequence_gaps, 1ull);
  CHECK(!s.reliable());
  CHECK_EQ(rec.seen.size(), 3);               // packet'in kendisi yine de parse ediliyor
}

TEST(mold_drops_a_packet_it_has_already_seen_entirely) {
  Recorder rec;
  MoldSession<Recorder> s(rec);

  Bytes first = packetOf(1, { del(1), del(2) });
  feed(s, first);
  feed(s, first);                             // birebir retransmission

  CHECK_EQ(rec.seen.size(), 2);               // dört değil
  CHECK_EQ(s.stats().duplicates, 1ull);
  CHECK(s.reliable());                        // duplicate bir sequence gap değil
}

TEST(mold_skips_only_the_overlapping_prefix_of_a_retransmission) {
  Recorder rec;
  MoldSession<Recorder> s(rec);

  feed(s, packetOf(1, { del(1), del(2) }));          // beklenen 3 oluyor
  feed(s, packetOf(2, { del(2), del(3), del(4) }));  // 2 eski, 3 ve 4 yeni

  REQUIRE(rec.seen.size() == 4);
  CHECK_EQ(rec.seen[2].id, 3ull);             // 2'nin tekrarı bastırıldı
  CHECK_EQ(rec.seen[3].id, 4ull);
  CHECK_EQ(s.stats().duplicates, 1ull);
  CHECK(s.reliable());
}

TEST(mold_treats_a_past_sequence_as_a_duplicate_not_a_gap) {
  Recorder rec;
  MoldSession<Recorder> s(rec);

  feed(s, packetOf(10, { del(10), del(11) }));   // başlangıç 10, beklenen 12
  feed(s, packetOf(5,  { del(5) }));             // çok geride

  CHECK_EQ(rec.seen.size(), 2);
  CHECK_EQ(s.stats().duplicates, 1ull);
  CHECK_EQ(s.stats().sequence_gaps, 0ull);
  CHECK(s.reliable());
}

TEST(mold_counts_a_heartbeat_without_delivering_anything) {
  Recorder rec;
  MoldSession<Recorder> s(rec);

  feed(s, packetOf(1, { del(1) }));
  feed(s, packetOf(2, {}));                   // count 0, sadece header

  CHECK_EQ(s.stats().heartbeats, 1ull);
  CHECK_EQ(rec.seen.size(), 1);
  CHECK(s.reliable());
}

TEST(mold_sees_a_gap_announced_by_a_heartbeat) {
  Recorder rec;
  MoldSession<Recorder> s(rec);

  feed(s, packetOf(1, { del(1) }));           // beklenen 2 oluyor
  feed(s, packetOf(9, {}));                   // heartbeat sonrakinin 9 olduğunu söylüyor

  CHECK_EQ(s.stats().heartbeats, 1ull);
  CHECK_EQ(s.stats().sequence_gaps, 1ull);
  CHECK(!s.reliable());
}

TEST(mold_records_end_of_session) {
  Recorder rec;
  MoldSession<Recorder> s(rec);

  MoldUDP64Packet p = make_packet("BIST01", 5, {});
  p.header.mesCount = 0xFFFF;
  Bytes wire = serialize(p);

  CHECK(!s.stats().end_of_session);
  feed(s, wire);
  CHECK(s.stats().end_of_session);
  CHECK_EQ(rec.seen.size(), 0);
}

TEST(mold_rejects_a_truncated_header) {
  Recorder rec;
  MoldSession<Recorder> s(rec);

  Bytes wire = packetOf(1, { del(1) });
  truncate_to(wire, 15);                      // header 20 byte
  feed(s, wire);

  CHECK_EQ(s.stats().invalid_packets, 1ull);
  CHECK_EQ(rec.seen.size(), 0);
}

TEST(mold_rejects_a_block_length_that_overruns_the_packet) {
  Recorder rec;
  MoldSession<Recorder> s(rec);

  Bytes wire = packetOf(1, { del(1) });
  set_block_length(wire, 0, 9999);
  feed(s, wire);

  CHECK_EQ(s.stats().invalid_packets, 1ull);
  CHECK_EQ(rec.seen.size(), 0);               // bozuk bir block'tan hiçbir şey teslim edilmiyor
  CHECK(!s.reliable());
}

TEST(mold_stops_when_the_count_claims_more_blocks_than_the_packet_holds) {
  Recorder rec;
  MoldSession<Recorder> s(rec);

  Bytes wire = packetOf(1, { del(1), del(2) });
  set_message_count(wire, 5);                 // 5 iddia ediyor, 2 taşıyor
  feed(s, wire);

  CHECK_EQ(rec.seen.size(), 2);               // gerçek olan ikisi yine de geliyor
  CHECK_EQ(s.stats().invalid_packets, 1ull);  // sonra byte'ları tükeniyor
  CHECK(!s.reliable());
}

TEST(mold_notices_an_unexpected_session_change) {
  Recorder rec;
  MoldSession<Recorder> s(rec);

  feed(s, serialize(make_packet("BIST01", 100, { del(1) })));
  CHECK_EQ(s.stats().session_changes, 0ull);

  // Yeni session: numaralandırma baştan başlıyor, yani düşük bir sequence burada
  // duplicate değil.
  feed(s, serialize(make_packet("BIST02", 1, { del(2) })));

  CHECK_EQ(s.stats().session_changes, 1ull);
  CHECK_EQ(rec.seen.size(), 2);
  CHECK_EQ(s.stats().duplicates, 0ull);
}

TEST(mold_counts_packets_even_when_it_rejects_them) {
  Recorder rec;
  MoldSession<Recorder> s(rec);

  feed(s, packetOf(1, { del(1) }));
  Bytes bad = packetOf(2, { del(2) });
  truncate_to(bad, 4);
  feed(s, bad);

  CHECK_EQ(s.stats().packets, 2ull);
  CHECK_EQ(s.stats().messages, 1ull);
}
