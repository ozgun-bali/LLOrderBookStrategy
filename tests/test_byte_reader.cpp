#include "test_framework.hpp"
#include "message_builder.hpp"
#include "core/byte_reader.hpp"

#include <cstring>

using namespace llob;
using namespace llobtest;

TEST(byte_reader_reads_every_width_big_endian) {
  const uint8_t buf[8] = {0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF};
  ByteReader r(buf, 8);

  CHECK_EQ(r.size(), 8);
  CHECK_EQ(r.u8(0),   0x01);
  CHECK_EQ(r.u8(7),   0xEF);
  CHECK_EQ(r.be16(0), 0x0123);
  CHECK_EQ(r.be16(6), 0xCDEF);
  CHECK_EQ(r.be32(0), 0x01234567u);
  CHECK_EQ(r.be32(4), 0x89ABCDEFu);
  CHECK_EQ(r.be64(0), 0x0123456789ABCDEFull);
}

TEST(byte_reader_reads_at_a_nonzero_offset) {
  // Order ID offset 5'te duruyor, yani bu protokoldeki her geniş okuma unaligned.
  const uint8_t buf[9] = {0xFF, 0x11,0x22,0x33,0x44, 0x55,0x66,0x77,0x88};
  ByteReader r(buf, 9);

  CHECK_EQ(r.be32(1), 0x11223344u);
  CHECK_EQ(r.be32(5), 0x55667788u);
}

TEST(byte_reader_has_is_exact_at_the_boundary) {
  const uint8_t buf[4] = {0,0,0,0};
  ByteReader r(buf, 4);

  CHECK(r.has(0));
  CHECK(r.has(4));      // tam olarak bütün buffer hâlâ sığıyor
  CHECK(!r.has(5));     // bir byte fazlası reddedilmeli
}

TEST(byte_reader_round_trips_every_add_order_field) {
  // Builder ile reader birbirinden bağımsız yazıldı. Herhangi bir offset'te ya da
  // byte sırasında anlaşamazlarsa, bunu sadece bir round trip yakalar.
  Bytes m = add_order(10000000u, 77ull, 73616u, SIDE_SELL, 500ull, 104900);
  ByteReader r(&m[0], m.size());
  REQUIRE(r.has(45));

  CHECK_EQ(r.u8(0),    (uint8_t)'A');
  CHECK_EQ(r.be32(1),  10000000u);
  CHECK_EQ(r.be64(5),  77ull);
  CHECK_EQ(r.be32(13), 73616u);
  CHECK_EQ(r.u8(17),   (uint8_t)'S');
  CHECK_EQ(r.be64(22), 500ull);
  CHECK_EQ((Price)r.be32(30), 104900);
  CHECK_EQ(r.be16(34), 0u);
  CHECK_EQ(r.u8(36),   2);
}

TEST(byte_reader_round_trips_the_market_order_price_sentinel) {
  Bytes m = add_order(1u, 1ull, 73616u, SIDE_BUY, 1ull, PRICE_NOT_AVAILABLE);
  ByteReader r(&m[0], m.size());
  REQUIRE(r.has(45));

  CHECK_EQ(r.be32(30), 0x80000000u);                 // sadece bit 31
  CHECK_EQ((Price)r.be32(30), PRICE_NOT_AVAILABLE);  // ve geri dönerken signed
  CHECK((Price)r.be32(30) < 0);
}

TEST(byte_reader_round_trips_the_other_three_messages) {
  Bytes d = deleted(1u, 99ull, 73616u, SIDE_BUY);
  ByteReader rd(&d[0], d.size());
  REQUIRE(rd.has(18));
  CHECK_EQ(rd.u8(0), (uint8_t)'D');
  CHECK_EQ(rd.be64(5), 99ull);
  CHECK_EQ(rd.u8(17), (uint8_t)'B');

  Bytes e = executed(2u, 99ull, 73616u, SIDE_SELL, 250ull, 0xAABBCCDDull, 0u);
  ByteReader re(&e[0], e.size());
  REQUIRE(re.has(52));
  CHECK_EQ(re.u8(0), (uint8_t)'E');
  CHECK_EQ(re.be64(18), 250ull);                     // execute edilen quantity
  CHECK_EQ(re.be64(26), 0xAABBCCDDull);              // match id

  Bytes t = seconds(1727700000u);
  ByteReader rt(&t[0], t.size());
  REQUIRE(rt.has(5));
  CHECK_EQ(rt.u8(0), (uint8_t)'T');
  CHECK_EQ(rt.be32(1), 1727700000u);
}

TEST(byte_reader_raw_compares_alpha_fields_without_copying) {
  Bytes m = book_state(1u, 73616u, "P_SUREKLI_ISLEM");
  ByteReader r(&m[0], m.size());
  REQUIRE(r.has(29));

  // 20 byte, space ile pad'lenmiş, terminator yok: hot path state'i std::string
  // kurmadan böyle test ediyor - pad'lenmiş bir sabite karşı memcmp.
  const char want[20] = {'P','_','S','U','R','E','K','L','I','_',
                         'I','S','L','E','M',' ',' ',' ',' ',' '};
  CHECK(std::memcmp(r.raw(9), want, 20) == 0);

  const char other[20] = {'P','_','M','A','R','J','_','Y','A','Y',
                          'I','N','_','K','A','P','A','N','I','S'};
  CHECK(std::memcmp(r.raw(9), other, 20) != 0);
}

TEST(byte_reader_slice_rebases_offsets_to_the_message) {
  Bytes wire = serialize(make_packet("BIST01", 42, {
      add_order(10000000u, 77ull, 73616u, SIDE_SELL, 500ull, 104900),
      deleted  (10000000u, 88ull, 73616u, SIDE_BUY)
  }));
  ByteReader pkt(&wire[0], wire.size());
  REQUIRE(pkt.has(87));                 // 20 + (2+45) + (2+18)

  CHECK_EQ(pkt.be64(10), 42ull);        // sequence
  CHECK_EQ(pkt.be16(18), 2u);           // message count

  // Block'ları yürü. Her slice 0'dan başlıyor, yani message packet'in neresinde
  // olursa olsun spec offset'leri doğrudan geçerli.
  uint16_t len0 = pkt.be16(20);
  CHECK_EQ(len0, 45u);
  ByteReader m0 = pkt.slice(22, len0);
  CHECK_EQ(m0.u8(0), (uint8_t)'A');
  CHECK_EQ(m0.be64(5), 77ull);
  CHECK_EQ(m0.u8(17), (uint8_t)'S');

  uint16_t len1 = pkt.be16(67);         // 22 + 45
  CHECK_EQ(len1, 18u);
  ByteReader m1 = pkt.slice(69, len1);
  CHECK_EQ(m1.u8(0), (uint8_t)'D');
  CHECK_EQ(m1.be64(5), 88ull);          // aynı offset, farklı message
  CHECK_EQ(m1.u8(17), (uint8_t)'B');
}

TEST(byte_reader_slice_cannot_see_past_its_block) {
  Bytes wire = serialize(make_packet("BIST01", 1, {
      deleted(1u, 1ull, 73616u, SIDE_BUY),
      deleted(2u, 2ull, 73616u, SIDE_SELL)
  }));
  ByteReader pkt(&wire[0], wire.size());

  ByteReader first = pkt.slice(22, 18);
  CHECK_EQ(first.size(), 18);
  CHECK(first.has(18));
  CHECK(!first.has(19));   // bozuk bir length bir sonraki message'a sızmamalı
}
