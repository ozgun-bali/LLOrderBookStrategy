#include "test_framework.hpp"
#include "message_builder.hpp"

using namespace llobtest;

// Field'ları big-endian geri okumak, her beklenen değer için elle hex hesaplamaktan
// iyi; ve writer bir gün onları little-endian yazarsa bu testler patlıyor.
static uint32_t be32_at(const Bytes& b, std::size_t off) {
  return ((uint32_t)b[off] << 24) | ((uint32_t)b[off + 1] << 16)
       | ((uint32_t)b[off + 2] << 8) |  (uint32_t)b[off + 3];
}

static uint64_t be64_at(const Bytes& b, std::size_t off) {
  uint64_t v = 0;
  for (std::size_t i = 0; i < 8; ++i) v = (v << 8) | b[off + i];
  return v;
}

static uint16_t be16_at(const Bytes& b, std::size_t off) {
  return (uint16_t)(((uint16_t)b[off] << 8) | b[off + 1]);
}

TEST(be32_writes_most_significant_byte_first) {
    Bytes b;
    put_be32(b, 0x12345678);
    CHECK_EQ(b.size(), 4);
    CHECK_EQ(b[0], 0x12);
    CHECK_EQ(b[1], 0x34);
    CHECK_EQ(b[2], 0x56);
    CHECK_EQ(b[3], 0x78);
}

TEST(deleted_builds_the_message_correctly){
  Bytes b = deleted(10000000,77,73616,SIDE_BUY);
  CHECK_EQ(b.size(),18);
  CHECK_EQ(b[0],0x44);
  CHECK_EQ(b[1],0x00);
  CHECK_EQ(b[2],0x98);
  CHECK_EQ(b[3],0x96);
  CHECK_EQ(b[4],0x80);
  CHECK_EQ(b[5],0x00);
  CHECK_EQ(b[6],0x00);
  CHECK_EQ(b[7],0x00);
  CHECK_EQ(b[8],0x00);
  CHECK_EQ(b[9],0x00);
  CHECK_EQ(b[10],0x00);
  CHECK_EQ(b[11],0x00);
  CHECK_EQ(b[12],0x4D);
  CHECK_EQ(b[13],0x00);
  CHECK_EQ(b[14],0x01);
  CHECK_EQ(b[15],0x1F);
  CHECK_EQ(b[16],0x90);
  CHECK_EQ(b[17],0x42);
  Bytes s = deleted(1, 1, 73616, SIDE_SELL);
  CHECK_EQ(s[17], 0x53);   // 'S'
  
}
TEST(seconds_work_as_intended) {
  Bytes b = seconds(1727700000);
  REQUIRE(b.size() == 5);

  uint32_t v = ((uint32_t)b[1] << 24) | ((uint32_t)b[2] << 16)
          | ((uint32_t)b[3] <<  8) |  (uint32_t)b[4];
          
  CHECK_EQ(v, 1727700000u);
  CHECK_EQ(b[0], 0x54);
  CHECK_EQ(b[1], 0x66);
  CHECK_EQ(b[2], 0xFA);
  CHECK_EQ(b[3], 0x9C);
  CHECK_EQ(b[4], 0x20);

}

TEST(book_state_lays_out_all_four_fields) {
  Bytes b = book_state(10000000u, 73616u, "P_SUREKLI_ISLEM");
  REQUIRE(b.size() == 29);

  CHECK_EQ(b[0], 0x4F);                                  // 'O'

  uint32_t nanos = ((uint32_t)b[1] << 24) | ((uint32_t)b[2] << 16)
                 | ((uint32_t)b[3] <<  8) |  (uint32_t)b[4];
  CHECK_EQ(nanos, 10000000u);

  uint32_t book  = ((uint32_t)b[5] << 24) | ((uint32_t)b[6] << 16)
                 | ((uint32_t)b[7] <<  8) |  (uint32_t)b[8];
  CHECK_EQ(book, 73616u);

  const char* want = "P_SUREKLI_ISLEM";                  // 15 karakter
  for (std::size_t i = 0; i < 15; ++i)
    CHECK_EQ(b[9 + i], (uint8_t)want[i]);

  for (std::size_t i = 24; i < 29; ++i)                  // 5 byte padding
    CHECK_EQ(b[i], 0x20);
}

TEST(book_state_pads_nothing_when_the_name_fills_the_field) {
  Bytes b = book_state(1u, 73616u, "P_MARJ_YAYIN_KAPANIS");   // tam 20
  REQUIRE(b.size() == 29);

  const char* want = "P_MARJ_YAYIN_KAPANIS";
  for (std::size_t i = 0; i < 20; ++i)
    CHECK_EQ(b[9 + i], (uint8_t)want[i]);

  CHECK_EQ(b[28], (uint8_t)'S');    // son byte içerik, padding değil
}

TEST(executed_lays_out_all_fields_including_the_reserved_tail) {
  // id ile book bilerek birbirinden uzak: yerleri değişse yakalanır, sessizce geçmez
  Bytes b = executed(10000000u, 77ull, 73616u, SIDE_SELL,
                     500ull, 0xAABBCCDD11223344ull, 0u);
  REQUIRE(b.size() == 52);

  CHECK_EQ(b[0], 0x45);                            // 'E'
  CHECK_EQ(be32_at(b, 1), 10000000u);              // nanosecond
  CHECK_EQ(be64_at(b, 5), 77ull);                  // order id
  CHECK_EQ(be32_at(b, 13), 73616u);                // order book id
  CHECK_EQ(b[17], (uint8_t)'S');                   // side
  CHECK_EQ(be64_at(b, 18), 500ull);                // execute edilen quantity
  CHECK_EQ(be64_at(b, 26), 0xAABBCCDD11223344ull); // match id
  CHECK_EQ(be32_at(b, 34), 0u);                    // combo group id

  for (std::size_t i = 38; i < 52; ++i)            // iki reserved field, 7 + 7
    CHECK_EQ(b[i], 0x00);
}

TEST(executed_writes_the_buy_side_character) {
  Bytes b = executed(1u, 1ull, 73616u, SIDE_BUY, 1ull, 1ull, 0u);
  REQUIRE(b.size() == 52);
  CHECK_EQ(b[17], (uint8_t)'B');
}

TEST(add_order_lays_out_all_eleven_fields) {
  Bytes b = add_order(10000000u, 77ull, 73616u, SIDE_BUY, 500ull, 104900);
  REQUIRE(b.size() == 45);

  CHECK_EQ(b[0], 0x41);                           // 'A'
  CHECK_EQ(be32_at(b, 1), 10000000u);             // nanosecond
  CHECK_EQ(be64_at(b, 5), 77ull);                 // order id
  CHECK_EQ(be32_at(b, 13), 73616u);               // order book id
  CHECK_EQ(b[17], (uint8_t)'B');                  // side
  CHECK_EQ(be32_at(b, 18), RANKSEQ);              // parser bunu atlıyor: bilerek zehirli değer
  CHECK_EQ(be64_at(b, 22), 500ull);               // quantity
  CHECK_EQ((int32_t)be32_at(b, 30), 104900);      // price
  CHECK_EQ(be16_at(b, 34), 0u);                   // order attribute'ları, default
  CHECK_EQ(b[36], 2);                             // lot type, round lot
  CHECK_EQ(be64_at(b, 37), RANKINGTIME);          // parser bunu atlıyor: bilerek zehirli değer
}

TEST(add_order_writes_the_market_order_price_sentinel) {
  Bytes b = add_order(1u, 1ull, 73616u, SIDE_SELL, 100ull, PRICE_NOT_AVAILABLE);
  REQUIRE(b.size() == 45);

  // bit 31 set, diğer her bit sıfır: "price yok", ve bir add'de market order demek
  CHECK_EQ(b[30], 0x80);
  CHECK_EQ(b[31], 0x00);
  CHECK_EQ(b[32], 0x00);
  CHECK_EQ(b[33], 0x00);
  CHECK_EQ((int32_t)be32_at(b, 30), PRICE_NOT_AVAILABLE);
}

TEST(add_order_carries_undisclosed_quantity_and_custom_attributes) {
  Bytes b = add_order(1u, 1ull, 73616u, SIDE_BUY, 0ull, 104900, 8192, 2);
  REQUIRE(b.size() == 45);

  CHECK_EQ(be64_at(b, 22), 0ull);    // spec: 0, undisclosed quantity demek
  CHECK_EQ(be16_at(b, 34), 8192u);   // spec: 8192, bait/implied order demek
}

TEST(serialize_frames_two_messages_into_one_packet) {
  Bytes wire = serialize(make_packet("BIST01", 42, {
      add_order(10000000u, 77ull, 73616u, SIDE_BUY, 500ull, 104900),
      deleted  (10000000u, 77ull, 73616u, SIDE_BUY)
  }));

  // 20 header + (2 + 45) + (2 + 18)
  REQUIRE(wire.size() == 87);

  const char* sess = "BIST01";
  for (std::size_t i = 0; i < 6; ++i) CHECK_EQ(wire[i], (uint8_t)sess[i]);
  for (std::size_t i = 6; i < 10; ++i) CHECK_EQ(wire[i], 0x20);   // pad'lenmiş

  CHECK_EQ(be64_at(wire, 10), 42ull);     // İLK message'ın sequence'i
  CHECK_EQ(be16_at(wire, 18), 2u);        // message count, make_packet dolduruyor

  CHECK_EQ(be16_at(wire, 20), 45u);       // ilk block length'i
  CHECK_EQ(wire[22], (uint8_t)'A');       // ilk message hemen ardından başlıyor

  CHECK_EQ(be16_at(wire, 67), 18u);       // ikinci block length'i: 22 + 45 = 67
  CHECK_EQ(wire[69], (uint8_t)'D');
}

TEST(serialize_writes_a_heartbeat_as_a_bare_header) {
  Bytes wire = serialize(make_packet("BIST01", 7, {}));
  REQUIRE(wire.size() == 20);             // sadece header, block yok
  CHECK_EQ(be16_at(wire, 18), 0u);        // heartbeat'i işaretleyen şey count 0
}

TEST(end_of_session_is_a_count_of_ffff) {
  MoldUDP64Packet p = make_packet("BIST01", 7, {});
  p.header.mesCount = 0xFFFF;
  Bytes wire = serialize(p);

  REQUIRE(wire.size() == 20);
  CHECK_EQ(be16_at(wire, 18), 0xFFFFu);
}

TEST(corruptors_break_exactly_one_thing_each) {
  Bytes good = serialize(make_packet("BIST01", 1, {
      deleted(1u, 1ull, 73616u, SIDE_BUY)
  }));
  REQUIRE(good.size() == 40);             // 20 + 2 + 18

  Bytes shortHeader = good;
  truncate_to(shortHeader, 15);
  CHECK_EQ(shortHeader.size(), 15);       // header bir field'ın ortasından kesilmiş

  Bytes wrongCount = good;
  set_message_count(wrongCount, 99);
  CHECK_EQ(be16_at(wrongCount, 18), 99u); // 99 block iddia ediyor, 1 taşıyor
  CHECK_EQ(wrongCount.size(), good.size());

  Bytes wrongLen = good;
  set_block_length(wrongLen, 0, 9999);
  CHECK_EQ(be16_at(wrongLen, 20), 9999u); // block packet'in dışına taşıyor
  CHECK_EQ(wrongLen.size(), good.size());
  CHECK_EQ(wrongLen[22], (uint8_t)'D');   // payload'ın kendisine dokunulmadı
}