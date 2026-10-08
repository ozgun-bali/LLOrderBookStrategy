// CLI entry point.
//
//   ./orderbook --input sample.bin --config config.txt [--orders orders.csv]
//
// The input file is read into memory once, before anything that will later be
// timed. Everything downstream works on views into that one buffer: no copy is
// made of a packet, a message or a field.

#include "core/config.hpp"
#include "io/datagram_source.hpp"
#include "protocol/mold_udp64.hpp"
#include "protocol/itch_parser.hpp"
#include "app/engine.hpp"
#include "app/order_log.hpp"
#include "app/reporter.hpp"
#include "app/stats.hpp"
#include "book/order_book.hpp"
#include "sim/fill_simulator.hpp"
#include "sim/position.hpp"
#include "strategy/gap_strategy.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace llob;

namespace {

// An intent has to reach two places: the fill model, which turns it into a
// position, and the log, which keeps it for the CSV and the checksum. Rather
// than give either of them the other's job, the strategy writes here.
struct RunSink {
    FillSimulator& fills;
    OrderLog&      log;

    RunSink(FillSimulator& f, OrderLog& l) : fills(f), log(l) {}

    void on_order_intent(const OrderIntent& intent) {
        fills.on_order_intent(intent);
        log.add(intent);
    }
};

typedef GapStrategy<RunSink>            Strategy;
typedef Engine<Strategy>                Eng;
typedef ItchParser<Eng>                 Parser;
typedef MoldSession<Parser>             Mold;

bool readWholeFile(const char* path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;

    const std::streamoff size = f.tellg();
    if (size < 0) return false;
    f.seekg(0, std::ios::beg);

    out.resize((std::size_t)size);
    if (out.empty()) return true;
    return (bool)f.read((char*)&out[0], size);
}

void usage() {
    std::fprintf(stderr,
        "usage: orderbook --input <file> --config <file> [--orders <file.csv>]\n");
}

// One full pass over the data. Everything is constructed here so a benchmark
// can call it repeatedly from a clean state.
RunSummary runOnce(const Config& cfg,
                   const uint8_t* data, std::size_t size,
                   OrderLog& log) {
    OrderBook       book(cfg);
    PositionTracker position;
    FillSimulator   fills(position);
    RunSink         sink(fills, log);
    Strategy        strategy(cfg, sink);
    Eng             engine(book, strategy, position.position_ref());
    Parser          parser(engine, cfg.target_order_book_id);
    Mold            mold(parser);

    DatagramSource  source(data, size);

    const uint8_t* packet = 0;
    std::size_t    length = 0;
    bool           closed = false;          // end-of-day seen

    while (source.next(packet, length)) {
        mold.process_datagram(packet, length);

        // The day's value is taken when P_MARJ_YAYIN_KAPANIS arrives, not at
        // end of file: the feed keeps trading afterwards and the last price
        // would drift away from the one the day closed on.
        if (!closed && book.day_closed()) {
            closed = true;
            position.mark_end_of_day(book.has_last_execution(),
                                     book.last_execution_price());
        }
    }
    engine.finalize();

    // A feed that never reaches its close still has to report something.
    if (!closed) {
        position.mark_end_of_day(book.has_last_execution(),
                                 book.last_execution_price());
    }

    RunSummary s;
    s.position   = position.quantity();
    s.cash_flow  = position.cash_flow();
    s.pnl_known  = position.pnl_known();
    s.final_pnl  = position.pnl_known() ? position.final_pnl() : 0;
    s.fills      = position.fills();

    s.have_mark_price      = position.have_mark_price();
    s.mark_price           = position.mark_price();

    s.have_last_execution  = book.has_last_execution();
    s.last_execution_price = book.last_execution_price();

    s.have_best_bid = book.has_best(SIDE_BUY);
    if (s.have_best_bid) {
        s.best_bid          = book.best(SIDE_BUY);
        s.best_bid_quantity = book.best_quantity(SIDE_BUY);
    }
    s.have_best_ask = book.has_best(SIDE_SELL);
    if (s.have_best_ask) {
        s.best_ask          = book.best(SIDE_SELL);
        s.best_ask_quantity = book.best_quantity(SIDE_SELL);
    }

    s.packets         = source.packets();
    s.itch_messages   = parser.stats().messages;
    s.target_messages = parser.stats().target_messages;
    s.groups          = engine.stats().groups;

    // Every message the book was actually given, which is the target book's
    // share of the feed.
    s.processed_message_count = engine.stats().messages;
    s.sequence_gap_count      = mold.stats().sequence_gaps;
    s.generated_order_count   = strategy.stats().intents;

    // Malformed at either layer: a packet that did not frame, and a message
    // whose length or side byte did not hold up.
    s.invalid_message_count =
        mold.stats().invalid_packets + parser.stats().invalid_messages;

    const StrategyStats& ss = strategy.stats();
    s.evaluations         = ss.evaluations;
    s.skip_state          = ss.skip_state;
    s.skip_sequence       = ss.skip_sequence;
    s.skip_one_sided      = ss.skip_one_sided;
    s.skip_spread         = ss.skip_spread;
    s.skip_not_this_group = ss.skip_not_this_group;
    s.skip_flip           = ss.skip_flip;
    s.skip_no_direction   = ss.skip_no_direction;
    s.skip_duplicate      = ss.skip_duplicate;
    s.skip_position_limit = ss.skip_position_limit;

    s.checksum = compute_checksum(s, log);

    if (source.truncated())
        std::fprintf(stderr, "warning: input ends mid-packet at byte %zu\n",
                     source.offset());
    if (log.grew())
        std::fprintf(stderr, "warning: order log outgrew its reserve\n");

    return s;
}

} // namespace

int main(int argc, char** argv) {
    const char* inputPath  = 0;
    const char* configPath = 0;
    const char* ordersPath = "orders.csv";

    for (int i = 1; i < argc; ++i) {
        const bool hasNext = (i + 1 < argc);
        if      (!std::strcmp(argv[i], "--input")  && hasNext) inputPath  = argv[++i];
        else if (!std::strcmp(argv[i], "--config") && hasNext) configPath = argv[++i];
        else if (!std::strcmp(argv[i], "--orders") && hasNext) ordersPath = argv[++i];
        else { usage(); return 2; }
    }
    if (!inputPath || !configPath) { usage(); return 2; }

    Config      cfg;
    std::string error;
    if (!load_config(configPath, cfg, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }

    std::vector<uint8_t> data;
    if (!readWholeFile(inputPath, data)) {
        std::fprintf(stderr, "cannot read '%s'\n", inputPath);
        return 1;
    }
    if (data.empty()) {
        std::fprintf(stderr, "'%s' is empty\n", inputPath);
        return 1;
    }

    OrderLog log;
    const RunSummary s = runOnce(cfg, &data[0], data.size(), log);

    print_summary(std::cout, s);

    if (!write_order_csv(ordersPath, log)) {
        std::fprintf(stderr, "cannot write '%s'\n", ordersPath);
        return 1;
    }
    return 0;
}
