#ifndef LLOB_TESTS_FEED_FIXTURE_HPP
#define LLOB_TESTS_FEED_FIXTURE_HPP

// Sentetik bir feed'i kurulmuş programın içinden geçiriyor.
//
// Component başına yazılan testler her katmanı kendi arayüzüne karşı kontrol
// ediyor. Buradakiler ise bir uçtan byte veriyor, diğer uçtan raporlanan özeti
// okuyor; arada gerçek olan her katman var: DatagramSource, MoldSession,
// ItchParser, Engine, OrderBook, GapStrategy, FillSimulator, PositionTracker.
//
// Bu, component testlerinden farklı bir soru. Bir katman kendi başına doğru olup
// yine de yanlış bağlanmış olabilir - yanlış nesneden okunan bir sayaç, hiç
// ulaşılmayan bir handler, yanlış sayıya uygulanan bir limit. Bunu sadece bütün
// zincirden geçen bir run yakalıyor, ve assignment'ın saydığı senaryolar tam
// olarak bu seviyede kontrol edilmeye değer olanlar.

#include "message_builder.hpp"

#include "app/pipeline.hpp"
#include "core/config.hpp"

namespace llobtest {

using namespace llob;

const OrderBookId TARGET_BOOK = 73616u;
const OrderBookId OTHER_BOOK  = 99999u;
const uint32_t    FEED_SECOND = 1000u;

// Timestamp'ler second * 1e9 + nanos olarak compose ediliyor, yani `nanos`
// anında yazılmış bir message sonunda bunun altında grupleniyor.
inline Timestamp at(uint32_t nanos) {
    return (Timestamp)FEED_SECOND * 1000000000ull + nanos;
}

inline Config base_config() {
    Config c;
    c.target_order_book_id = TARGET_BOOK;
    c.tick_size            = 1;
    c.order_quantity       = 100;
    c.minimum_position     = -1000;
    c.maximum_position     = 1000;
    c.max_active_orders    = 256;
    c.max_price_levels     = 1024;
    return c;
}

inline Bytes state_open(uint32_t nanos) {
    return book_state(nanos, TARGET_BOOK, STATE_NAME_SUREKLI_ISLEM);
}
inline Bytes state_close(uint32_t nanos) {
    return book_state(nanos, TARGET_BOOK, STATE_NAME_MARJ_YAYIN_KAPANIS);
}

// Continuous trading; bid 1049, ask 1050, ask 1051, hepsi tek bir group'ta.
// 1050'deki ask'i silmek geriye bid 1049'a karşı ask 1051 bırakıyor: 1050'de
// tek tick'lik bir delik, yani strategy'nin aradığı şey.
inline void open_the_book(Feed& f) {
    f.packet({ seconds(FEED_SECOND),
               state_open(1),
               add_order(1, 1, TARGET_BOOK, SIDE_BUY,  1200, 1049),
               add_order(1, 2, TARGET_BOOK, SIDE_SELL,  600, 1050),
               add_order(1, 3, TARGET_BOOK, SIDE_SELL,  800, 1051) });
}

// Ayna görüntüsü: bid 1048, bid 1049, ask 1050. 1049'daki bid'i silmek
// 1049'da bid olan bir delik bırakıyor, yani doldurma işlemi bir alış.
inline void open_the_book_for_a_bid_gap(Feed& f) {
    f.packet({ seconds(FEED_SECOND),
               state_open(1),
               add_order(1, 1, TARGET_BOOK, SIDE_BUY,   500, 1048),
               add_order(1, 2, TARGET_BOOK, SIDE_BUY,   600, 1049),
               add_order(1, 3, TARGET_BOOK, SIDE_SELL,  800, 1050) });
}

// Tek bir tam run. Bittikten sonra log'u ve özeti tutuyor; pipeline'ın kendisi
// o noktada yok olmuş oluyor, yani kimse book'u okumaya devam edemiyor.
struct Run {
    Config     cfg;
    OrderLog   log;
    RunSummary s;
    bool       truncated;
    std::size_t offset;

    Run(const Config& c, const Bytes& wire)
        : cfg(c), log(), s(), truncated(false), offset(0) {
        assert(!wire.empty());
        Pipeline p(cfg, log);
        p.run(&wire[0], wire.size());
        s         = p.summary(log);
        truncated = p.truncated();
        offset    = p.offset();
    }
};

} // namespace llobtest

#endif // LLOB_TESTS_FEED_FIXTURE_HPP
