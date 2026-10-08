// Protokol seviyesi testler, uçtan uca.
//
// Assignment'ın saydığı senaryolar: kesik header, geçersiz message length,
// sequence gap, heartbeat, end of session, hedef dışı order book. Bunların her
// biri zaten test_mold_udp64.cpp ve test_itch_parser.cpp içinde doğrudan
// MoldSession ve ItchParser'a karşı kontrol ediliyor. Burada kontrol edilen şey,
// davranışın bütün zincirden sağ çıkıp raporlanan sayaçlara ulaşması - çünkü
// değerlendirmeyi yapan kişi onu orada arayacak.

#include "test_framework.hpp"
#include "feed_fixture.hpp"

using namespace llob;
using namespace llobtest;

TEST(protocol_a_truncated_header_stops_the_walk_and_is_reported) {
  // Bir dosyada packet'in etrafında zarf yok, yani kısa bir header bir sonraki
  // sınırın artık hesaplanamayacağı ve sonrasındaki her şeyin kaybolduğu anlamına
  // geliyor. Run bunu sessizce erken durmak yerine söylemek zorunda.
  Feed f;
  open_the_book(f);
  f.append(Bytes(10, 0x00));                  // yarım header, sonrası yok

  Run r(base_config(), f.wire);

  CHECK(r.truncated);
  CHECK_EQ(r.s.packets, 1ull);                // sağlam olan işlendi
  CHECK_EQ(r.s.itch_messages, 5ull);
  CHECK(r.s.have_best_bid);
  CHECK_EQ(r.s.best_bid, 1049);               // öncesindeki iş hâlâ ayakta
  CHECK_EQ(r.offset, f.wire.size() - 10);     // ve nerede durduğunu söylüyor
}

TEST(protocol_a_message_shorter_than_its_type_is_counted_and_skipped) {
  // Length'i, type'ın gerektirdiğiyle uyuşmayan bir block. Packet kendi içinde
  // tutarlı, yani framing sorunsuz; onu reddetmesi gereken taraf parser.
  Feed f;
  open_the_book(f);

  Bytes stub = add_order(2, 9, TARGET_BOOK, SIDE_SELL, 400, 1050);
  stub.resize(20);                            // 'A' bundan fazlasını istiyor
  f.packet({ stub });

  Run r(base_config(), f.wire);

  CHECK(!r.truncated);
  CHECK_EQ(r.s.packets, 2ull);
  CHECK_EQ(r.s.invalid_message_count, 1ull);
  CHECK_EQ(r.s.best_ask, 1050);               // bozuk message hiçbir şeyi değiştirmedi
  CHECK_EQ(r.s.best_ask_quantity, 600ull);
}

TEST(protocol_a_sequence_gap_is_counted_and_silences_the_strategy) {
  // Dört message hiç gelmedi, yani book'a artık güvenilemez. Ardından gelen gap
  // normalde signal verirdi; vermemeli.
  Feed f;
  open_the_book(f);
  f.lose(4);
  f.packet({ deleted(2, 2, TARGET_BOOK, SIDE_SELL) });

  Run r(base_config(), f.wire);

  CHECK_EQ(r.s.sequence_gap_count, 1ull);
  CHECK_EQ(r.s.generated_order_count, 0ull);
  CHECK(r.s.skip_sequence >= 1ull);
  CHECK_EQ(r.s.position, 0);
}

TEST(protocol_a_heartbeat_carries_nothing_and_breaks_nothing) {
  // Hiç block'u olmayan, beklenen bir sonraki sequence number'ı taşıyan bir
  // packet. Ne message sayılmalı, ne de bir sequence gap gibi görünmeli.
  Feed f;
  open_the_book(f);
  f.heartbeat();
  f.packet({ deleted(2, 2, TARGET_BOOK, SIDE_SELL) });

  Run r(base_config(), f.wire);

  CHECK_EQ(r.s.packets, 3ull);                // bu bir packet
  CHECK_EQ(r.s.itch_messages, 6ull);          // ama message değil
  CHECK_EQ(r.s.sequence_gap_count, 0ull);
  CHECK_EQ(r.s.generated_order_count, 1ull);  // stream hiç kopmadı
}

TEST(protocol_end_of_session_is_a_bare_header_and_frames_correctly) {
  // Count field'ındaki 0xFFFF bir count değil. Onu count olarak okumak,
  // yürüyüşün olmayan block'ları aramasına ve dosyanın geri kalanını
  // kaybetmesine yol açardı.
  Feed f;
  open_the_book(f);
  f.end_of_session();

  Run r(base_config(), f.wire);

  CHECK(!r.truncated);
  CHECK_EQ(r.s.packets, 2ull);
  CHECK_EQ(r.s.itch_messages, 5ull);
  CHECK_EQ(r.offset, f.wire.size());          // tam olarak son byte'a oturdu
}

TEST(protocol_another_order_book_never_reaches_the_engine) {
  // Gerçek feed'in %98'i başka book'lara ait. Type byte'ı, length kontrolü ve
  // tek bir 32 bit karşılaştırmanın ardından eleniyorlar, ve üstteki hiçbir
  // katman onları görmüyor.
  Feed f;
  open_the_book(f);
  f.packet({ add_order(2, 50, OTHER_BOOK, SIDE_BUY,  900, 1049),
             add_order(2, 51, OTHER_BOOK, SIDE_SELL, 900, 1050),
             deleted  (2, 50, OTHER_BOOK, SIDE_BUY) });

  Run r(base_config(), f.wire);

  CHECK_EQ(r.s.itch_messages, 8ull);          // hepsi okundu
  CHECK_EQ(r.s.target_messages, 4ull);        // state + üç add
  CHECK_EQ(r.s.processed_message_count, 4ull);
  CHECK_EQ(r.s.invalid_message_count, 0ull);  // bizim olmaması, bozuk olması demek değil
  CHECK_EQ(r.s.best_bid_quantity, 1200ull);   // bizim book'a dokunulmadı
  CHECK_EQ(r.s.best_ask, 1050);
}

TEST(protocol_the_second_marker_separates_identical_nanos) {
  // Nanosecond'lar her saniye baştan başlıyor, yani iki farklı saniyedeki aynı
  // nanos değeri iki farklı an demek. Onları birlikte gruplamak iki group'u tek
  // group'a indirir ve bir signal'i gizlerdi.
  Feed f;
  open_the_book(f);
  f.packet({ seconds(FEED_SECOND + 1),
             deleted(1, 2, TARGET_BOOK, SIDE_SELL) });   // yine nanos 1

  Run r(base_config(), f.wire);

  CHECK_EQ(r.s.groups, 2ull);
  CHECK_EQ(r.s.generated_order_count, 1ull);
  CHECK_EQ(r.log[0].timestamp,
           (Timestamp)(FEED_SECOND + 1) * 1000000000ull + 1ull);
}
