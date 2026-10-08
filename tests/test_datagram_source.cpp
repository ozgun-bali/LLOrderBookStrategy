#include "test_framework.hpp"
#include "message_builder.hpp"
#include "io/datagram_source.hpp"
#include "app/order_log.hpp"
#include "app/stats.hpp"

using namespace llob;
using namespace llobtest;

namespace {

Bytes del(OrderId id) { return deleted(1u, id, 73616u, SIDE_BUY); }

} // namespace

TEST(source_finds_the_boundary_between_glued_packets) {
  Bytes p1 = serialize(make_packet("BIST01", 1, { del(1), del(2) }));
  Bytes p2 = serialize(make_packet("BIST01", 3, { del(3) }));
  Bytes all = glue(std::vector<Bytes>{ p1, p2 });

  DatagramSource src(&all[0], all.size());
  const uint8_t* ptr = 0; std::size_t len = 0;

  REQUIRE(src.next(ptr, len));
  CHECK_EQ(len, p1.size());
  CHECK_EQ(ptr, &all[0]);                 // bir view, hiçbir şey kopyalanmadı

  REQUIRE(src.next(ptr, len));
  CHECK_EQ(len, p2.size());
  CHECK_EQ(ptr, &all[0] + p1.size());

  CHECK(!src.next(ptr, len));             // data'nın sonu
  CHECK_EQ(src.packets(), 2ull);
  CHECK(!src.truncated());
  CHECK_EQ(src.offset(), all.size());     // tam olarak son byte'a oturdu
}

TEST(source_handles_packets_of_different_sizes) {
  Bytes p1 = serialize(make_packet("BIST01", 1, {
      add_order(1u, 1ull, 73616u, SIDE_BUY, 10ull, 100) }));      // 20 + 2 + 45
  Bytes p2 = serialize(make_packet("BIST01", 2, { del(2) }));     // 20 + 2 + 18
  Bytes p3 = serialize(make_packet("BIST01", 3, {
      seconds(1000u), del(3), del(4) }));
  Bytes all = glue(std::vector<Bytes>{ p1, p2, p3 });

  DatagramSource src(&all[0], all.size());
  const uint8_t* ptr = 0; std::size_t len = 0;

  REQUIRE(src.next(ptr, len)); CHECK_EQ(len, 67);
  REQUIRE(src.next(ptr, len)); CHECK_EQ(len, 40);
  REQUIRE(src.next(ptr, len)); CHECK_EQ(len, p3.size());
  CHECK(!src.next(ptr, len));
  CHECK_EQ(src.offset(), all.size());
}

TEST(source_treats_a_heartbeat_as_a_bare_header) {
  Bytes hb = serialize(make_packet("BIST01", 7, {}));    // count 0, block yok
  Bytes p2 = serialize(make_packet("BIST01", 7, { del(1) }));
  Bytes all = glue(std::vector<Bytes>{ hb, p2 });

  DatagramSource src(&all[0], all.size());
  const uint8_t* ptr = 0; std::size_t len = 0;

  REQUIRE(src.next(ptr, len));
  CHECK_EQ(len, 20);                      // sadece header
  REQUIRE(src.next(ptr, len));
  CHECK_EQ(len, 40);
  CHECK_EQ(src.offset(), all.size());
}

TEST(source_treats_end_of_session_as_a_bare_header_too) {
  // 0xFFFF bir marker, block count değil. Onu count sanıp yürümek dosyanın
  // sonundan taşardı.
  MoldUDP64Packet p = make_packet("BIST01", 9, {});
  p.header.mesCount = 0xFFFF;
  Bytes eos = serialize(p);
  Bytes tail = serialize(make_packet("BIST01", 9, { del(1) }));
  Bytes all = glue(std::vector<Bytes>{ eos, tail });

  DatagramSource src(&all[0], all.size());
  const uint8_t* ptr = 0; std::size_t len = 0;

  REQUIRE(src.next(ptr, len));
  CHECK_EQ(len, 20);
  REQUIRE(src.next(ptr, len));
  CHECK_EQ(len, 40);
  CHECK(!src.truncated());
}

TEST(source_stops_on_a_header_cut_short) {
  Bytes all = serialize(make_packet("BIST01", 1, { del(1) }));
  all.resize(15);                         // 20 byte'lık header'dan kısa

  DatagramSource src(&all[0], all.size());
  const uint8_t* ptr = 0; std::size_t len = 0;

  CHECK(!src.next(ptr, len));
  CHECK(src.truncated());
  CHECK_EQ(src.packets(), 0ull);
}

TEST(source_stops_when_a_block_runs_past_the_end) {
  // Sınır artık hesaplanamıyor, yani ondan sonraki hiçbir sınır da
  // hesaplanamaz. Durmak, tahmin etmekten iyidir.
  Bytes p1 = serialize(make_packet("BIST01", 1, { del(1) }));
  Bytes p2 = serialize(make_packet("BIST01", 2, { del(2) }));
  set_block_length(p2, 0, 9999);
  Bytes all = glue(std::vector<Bytes>{ p1, p2 });

  DatagramSource src(&all[0], all.size());
  const uint8_t* ptr = 0; std::size_t len = 0;

  REQUIRE(src.next(ptr, len));            // sağlam olan yine de geçiyor
  CHECK_EQ(len, p1.size());

  CHECK(!src.next(ptr, len));
  CHECK(src.truncated());
  CHECK_EQ(src.packets(), 1ull);
}

TEST(source_on_an_empty_buffer_just_ends) {
  Bytes all(1, 0);
  DatagramSource src(&all[0], 0);
  const uint8_t* ptr = 0; std::size_t len = 0;

  CHECK(!src.next(ptr, len));
  CHECK(!src.truncated());                // okunacak bir şey olmaması hata değil
}

TEST(order_log_keeps_what_it_is_given) {
  OrderLog log(8);
  OrderIntent a; a.timestamp = 10; a.side = SIDE_SELL; a.price = 1050;
  a.quantity = 100; a.reason = REASON_GAP_WAS_ASK;

  log.add(a);
  CHECK_EQ(log.size(), 1);
  CHECK_EQ(log[0].price, 1050);
  CHECK_EQ(log[0].side, SIDE_SELL);
  CHECK(!log.grew());
}

TEST(order_log_reports_outgrowing_its_reserve) {
  OrderLog log(2);
  OrderIntent a;
  log.add(a); log.add(a);
  CHECK(!log.grew());
  log.add(a);
  CHECK(log.grew());                      // sayıldı, gizlenmedi
  CHECK_EQ(log.size(), 3);                // ve hiçbir şey kaybolmadı
}

TEST(checksum_is_stable_and_sensitive) {
  RunSummary s;
  s.position = 300; s.cash_flow = -6549000;
  s.processed_message_count = 308209; s.generated_order_count = 2;

  OrderLog a;
  OrderIntent i1; i1.timestamp = 10; i1.side = SIDE_SELL; i1.price = 1050;
  i1.quantity = 100; i1.reason = REASON_GAP_WAS_ASK;
  OrderIntent i2 = i1; i2.timestamp = 20; i2.side = SIDE_BUY;
  i2.reason = REASON_GAP_WAS_BID;
  a.add(i1); a.add(i2);

  const uint64_t h = compute_checksum(s, a);
  CHECK_EQ(compute_checksum(s, a), h);    // aynı input, aynı değer

  OrderLog b; b.add(i2); b.add(i1);       // aynı order'lar, farklı sıra
  CHECK(compute_checksum(s, b) != h);

  RunSummary s2 = s; s2.cash_flow += 1;   // bir kuruş farklı
  CHECK(compute_checksum(s2, a) != h);
}

TEST(checksum_keeps_the_bits_of_a_negative_price) {
  // Market order sentinel'i INT32_MIN. Onu signed bir genişletmeyle katlamak
  // sign-extend ederdi ve iki farklı price çakışabilirdi.
  RunSummary s;
  OrderLog a, b;
  OrderIntent i; i.timestamp = 1; i.side = SIDE_BUY; i.quantity = 1;
  i.reason = REASON_GAP_WAS_BID;

  i.price = PRICE_NOT_AVAILABLE; a.add(i);
  i.price = 0;                   b.add(i);

  CHECK(compute_checksum(s, a) != compute_checksum(s, b));
}
