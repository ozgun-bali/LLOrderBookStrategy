#ifndef LLOB_TESTS_FEED_FIXTURE_HPP
#define LLOB_TESTS_FEED_FIXTURE_HPP

// Runs a synthetic feed through the assembled program.
//
// The per-component tests check each layer against its own interface. These run
// bytes in at one end and read the reported summary out at the other, with every
// real layer in between: DatagramSource, MoldSession, ItchParser, Engine,
// OrderBook, GapStrategy, FillSimulator, PositionTracker.
//
// That is a different question from the component tests. A layer can be correct
// on its own and still be wired up wrongly - a counter read from the wrong
// object, a handler never reached, a limit applied to the wrong number. Only a
// run through the whole chain catches that, and the scenarios the assignment
// names are exactly the ones worth checking at this level.

#include "message_builder.hpp"

#include "app/pipeline.hpp"
#include "core/config.hpp"

namespace llobtest {

using namespace llob;

const OrderBookId TARGET_BOOK = 73616u;
const OrderBookId OTHER_BOOK  = 99999u;
const uint32_t    FEED_SECOND = 1000u;

// Timestamps are composed as second * 1e9 + nanos, so this is what a message
// written at `nanos` ends up grouped under.
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

// Continuous trading, bid 1049, ask 1050, ask 1051, all in one group.
// Deleting the ask at 1050 then leaves bid 1049 against ask 1051: a one-tick
// hole at 1050, which is what the strategy looks for.
inline void open_the_book(Feed& f) {
    f.packet({ seconds(FEED_SECOND),
               state_open(1),
               add_order(1, 1, TARGET_BOOK, SIDE_BUY,  1200, 1049),
               add_order(1, 2, TARGET_BOOK, SIDE_SELL,  600, 1050),
               add_order(1, 3, TARGET_BOOK, SIDE_SELL,  800, 1051) });
}

// The mirror image: bid 1048, bid 1049, ask 1050. Deleting the bid at 1049
// leaves a hole at 1049 that was a bid, so the refill is a buy.
inline void open_the_book_for_a_bid_gap(Feed& f) {
    f.packet({ seconds(FEED_SECOND),
               state_open(1),
               add_order(1, 1, TARGET_BOOK, SIDE_BUY,   500, 1048),
               add_order(1, 2, TARGET_BOOK, SIDE_BUY,   600, 1049),
               add_order(1, 3, TARGET_BOOK, SIDE_SELL,  800, 1050) });
}

// One complete run. Holds the log and the summary after it finishes; the
// pipeline itself is gone by then, so nothing can keep reading the book.
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
