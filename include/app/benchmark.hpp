#ifndef LLOB_APP_BENCHMARK_HPP
#define LLOB_APP_BENCHMARK_HPP

// Replays the same data repeatedly and reports how long it took.
//
// The rules the assignment sets, and why each one is there:
//
//   The input is in memory before anything is timed. Timing the read would
//   measure the disk.
//
//   At least one warm-up pass runs unmeasured. A cold first pass can be
//   several times slower than the steady state, and none of it is the code's
//   doing: 584 MB of freshly allocated pages fault in one at a time, caches
//   and branch predictors start empty, and the core may still be clocked down.
//
//   The data is replayed many times. One pass is one sample, and a scheduler
//   decision or an interrupt moves it by a tenth easily.
//
//   No file or console output happens inside the timed region.
//
//   A checksum leaves the program. Without a value anything depends on, -O3
//   is entitled to notice the whole computation is dead and remove it, and the
//   measurement would be of nothing at all.
//
// Each iteration builds a fresh pipeline, so every pass starts from an empty
// book and does identical work.

#include "app/alloc_counter.hpp"
#include "app/latency.hpp"
#include "app/pipeline.hpp"

#include <cstdio>
#include <fstream>
#include <string>

namespace llob {

struct BenchmarkResult {
    uint64_t iterations;

    double total_seconds;       // across all measured iterations
    double best_seconds;        // the cleanest single pass
    double worst_seconds;
    double messages_per_second; // from the best pass
    double ns_per_message;

    uint64_t messages_per_iteration;
    uint64_t packets_per_iteration;
    uint64_t target_messages;
    uint64_t order_intents;

    LatencyReport latency;
    double        timer_overhead_ns;
    double        tsc_ghz;
    double        tsc_resolution_ns;
    unsigned      sample_every;

    uint64_t hot_path_allocations;
    uint64_t hot_path_bytes;
    uint64_t peak_memory_kb;

    uint64_t checksum;
    bool     checksum_stable;   // every iteration agreed

    BenchmarkResult()
        : iterations(0), total_seconds(0), best_seconds(0), worst_seconds(0),
          messages_per_second(0), ns_per_message(0),
          messages_per_iteration(0), packets_per_iteration(0),
          target_messages(0), order_intents(0),
          timer_overhead_ns(0), tsc_ghz(0), tsc_resolution_ns(0),
          sample_every(0),
          hot_path_allocations(0), hot_path_bytes(0), peak_memory_kb(0),
          checksum(0), checksum_stable(true) {}
};

// VmHWM is the high water mark of resident memory: the most the process ever
// actually held, which is the number worth reporting.
inline uint64_t peak_memory_kb() {
    std::ifstream f("/proc/self/status");
    if (!f) return 0;
    std::string key;
    while (f >> key) {
        if (key == "VmHWM:") {
            uint64_t kb = 0;
            if (f >> kb) return kb;
            return 0;
        }
        std::getline(f, key);   // skip the rest of the line
    }
    return 0;
}

inline BenchmarkResult run_benchmark(const Config& cfg,
                                     const uint8_t* data, std::size_t size,
                                     uint64_t iterations,
                                     unsigned sampleEvery = 8) {
    BenchmarkResult r;
    r.iterations   = iterations;
    r.sample_every = sampleEvery;

    // Tie the cycle counter to real time before anything is measured with it.
    const double cyclesPerNs = calibrate_cycles_per_ns();
    const double nsPerCycle  = (cyclesPerNs > 0) ? 1.0 / cyclesPerNs : 1.0;
    r.tsc_ghz           = cyclesPerNs;
    r.tsc_resolution_ns = nsPerCycle;
    r.timer_overhead_ns = (double)measure_timer_overhead_cycles() * nsPerCycle;

    // Warm-up. Not timed, not counted, and its results are thrown away.
    {
        OrderLog log;
        Pipeline p(cfg, log);
        p.run(data, size);
    }

    LatencyHistogram hist;
    bool first = true;

    for (uint64_t i = 0; i < iterations; ++i) {
        OrderLog log;
        Pipeline p(cfg, log);          // allocates; deliberately before the reset

        alloc_reset();
        const uint64_t t0 = now_ns();
        p.run(data, size, &hist, sampleEvery, nsPerCycle);
        const uint64_t t1 = now_ns();

        const uint64_t allocs = alloc_count();
        const uint64_t bytes  = alloc_bytes();

        const double seconds = (double)(t1 - t0) / 1e9;
        r.total_seconds += seconds;
        if (first || seconds < r.best_seconds)  r.best_seconds  = seconds;
        if (first || seconds > r.worst_seconds) r.worst_seconds = seconds;

        // Whatever the hot path did ask for, keep the worst case rather than
        // the last one.
        if (allocs > r.hot_path_allocations) r.hot_path_allocations = allocs;
        if (bytes  > r.hot_path_bytes)       r.hot_path_bytes       = bytes;

        const RunSummary s = p.summary(log);
        if (first) {
            r.checksum               = s.checksum;
            r.messages_per_iteration = s.itch_messages;
            r.packets_per_iteration  = s.packets;
            r.target_messages        = s.target_messages;
            r.order_intents          = s.generated_order_count;
        } else if (s.checksum != r.checksum) {
            r.checksum_stable = false;   // the same input stopped agreeing
        }
        first = false;
    }

    r.latency        = hist.report();
    r.peak_memory_kb = peak_memory_kb();

    if (r.best_seconds > 0) {
        r.messages_per_second = (double)r.messages_per_iteration / r.best_seconds;
        r.ns_per_message      = r.best_seconds * 1e9 / (double)r.messages_per_iteration;
    }
    return r;
}

} // namespace llob

#endif // LLOB_APP_BENCHMARK_HPP
