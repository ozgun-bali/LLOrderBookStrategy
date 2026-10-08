#ifndef LLOB_APP_PIPELINE_HPP
#define LLOB_APP_PIPELINE_HPP

// The whole chain, assembled.
//
// Construction and the run are kept apart on purpose. Building the book and
// the pools allocates; the run must not. Separating them lets the benchmark
// reset the allocation counter in between and report what the hot path alone
// asked for.
//
// Everything is constructed fresh, so an iteration always starts from an empty
// book. Reusing one across iterations would make the second pass do different
// work from the first and the result would stop being reproducible.

#include "core/config.hpp"
#include "io/datagram_source.hpp"
#include "protocol/mold_udp64.hpp"
#include "protocol/itch_parser.hpp"
#include "app/engine.hpp"
#include "app/latency.hpp"
#include "app/order_log.hpp"
#include "app/stats.hpp"
#include "book/order_book.hpp"
#include "sim/fill_simulator.hpp"
#include "sim/position.hpp"
#include "strategy/gap_strategy.hpp"

namespace llob {

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

class Pipeline {
public:
    typedef GapStrategy<RunSink> Strategy;
    typedef Engine<Strategy>     Eng;
    typedef ItchParser<Eng>      Parser;
    typedef MoldSession<Parser>  Mold;

    Pipeline(const Config& cfg, OrderLog& log)
        : book_(cfg),
          position_(),
          fills_(position_),
          sink_(fills_, log),
          strategy_(cfg, sink_),
          engine_(book_, strategy_, position_.position_ref()),
          parser_(engine_, cfg.target_order_book_id),
          mold_(parser_),
          packets_(0), offset_(0), truncated_(false), closed_(false) {}

    // One pass over the data. With a histogram, every `sampleEvery`-th
    // datagram is timed: the clock costs about as much as a short datagram
    // takes, so timing all of them would measure the instrument.
    void run(const uint8_t* data, std::size_t size,
             LatencyHistogram* hist = 0, unsigned sampleEvery = 8,
             double nsPerCycle = 1.0) {
        DatagramSource source(data, size);

        const uint8_t* packet = 0;
        std::size_t    length = 0;
        unsigned       n = 0;

        while (source.next(packet, length)) {
            if (hist && ++n == sampleEvery) {
                n = 0;
                const uint64_t t0 = now_cycles();
                mold_.process_datagram(packet, length);
                const uint64_t cycles = now_cycles() - t0;
                hist->record((uint64_t)((double)cycles * nsPerCycle));
            } else {
                mold_.process_datagram(packet, length);
            }

            // The day is valued when the close arrives, not at end of file:
            // the feed keeps trading afterwards and the last price drifts.
            if (!closed_ && book_.day_closed()) {
                closed_ = true;
                position_.mark_end_of_day(book_.has_last_execution(),
                                          book_.last_execution_price());
            }
        }
        engine_.finalize();

        if (!closed_) {
            position_.mark_end_of_day(book_.has_last_execution(),
                                      book_.last_execution_price());
        }

        packets_   = source.packets();
        truncated_ = source.truncated();
        offset_    = source.offset();
    }

    bool        truncated() const { return truncated_; }
    std::size_t offset()    const { return offset_; }

    RunSummary summary(const OrderLog& log) const {
        RunSummary s;

        s.position  = position_.quantity();
        s.cash_flow = position_.cash_flow();
        s.pnl_known = position_.pnl_known();
        s.final_pnl = position_.pnl_known() ? position_.final_pnl() : 0;
        s.fills     = position_.fills();

        s.have_mark_price = position_.have_mark_price();
        s.mark_price      = position_.mark_price();

        s.have_last_execution  = book_.has_last_execution();
        s.last_execution_price = book_.last_execution_price();

        s.have_best_bid = book_.has_best(SIDE_BUY);
        if (s.have_best_bid) {
            s.best_bid          = book_.best(SIDE_BUY);
            s.best_bid_quantity = book_.best_quantity(SIDE_BUY);
        }
        s.have_best_ask = book_.has_best(SIDE_SELL);
        if (s.have_best_ask) {
            s.best_ask          = book_.best(SIDE_SELL);
            s.best_ask_quantity = book_.best_quantity(SIDE_SELL);
        }

        s.packets         = packets_;
        s.itch_messages   = parser_.stats().messages;
        s.target_messages = parser_.stats().target_messages;
        s.groups          = engine_.stats().groups;

        s.processed_message_count = engine_.stats().messages;
        s.sequence_gap_count      = mold_.stats().sequence_gaps;
        s.generated_order_count   = strategy_.stats().intents;
        s.invalid_message_count   =
            mold_.stats().invalid_packets + parser_.stats().invalid_messages;

        const StrategyStats& ss = strategy_.stats();
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
        return s;
    }

private:
    // Declaration order is construction order, and each one below needs the
    // one above it.
    OrderBook       book_;
    PositionTracker position_;
    FillSimulator   fills_;
    RunSink         sink_;
    Strategy        strategy_;
    Eng             engine_;
    Parser          parser_;
    Mold            mold_;

    uint64_t    packets_;
    std::size_t offset_;
    bool        truncated_;
    bool        closed_;
};

} // namespace llob

#endif // LLOB_APP_PIPELINE_HPP
