#ifndef LLOB_APP_LATENCY_HPP
#define LLOB_APP_LATENCY_HPP

// Latency samples, kept as a histogram.
//
// Percentiles need the distribution, and keeping every sample would mean
// megabytes per iteration and allocation while measuring. A fixed array of
// counters gives the same answer: one bucket per nanosecond up to four
// microseconds, plus an overflow bucket and the true maximum alongside.
//
// 4096 buckets is 32 KB, which stays in cache, and recording a sample is an
// index and an increment.
//
// Why percentiles at all. An average of fifty nanoseconds with one message in
// a thousand taking fifty microseconds still averages a hundred, and still
// looks fine. But the strategy only pays when it is first, and the slow cases
// cluster where the feed is busiest - the largest timestamp group on this feed
// holds 3,919 messages - which is exactly where the opportunities are. The
// mean hides that. The tail is the thing.

#include "core/types.hpp"

#include <chrono>
#include <cstddef>

namespace llob {

// __builtin_ia32_rdtsc rather than <x86intrin.h>: that header drags in
// <stdlib.h> through mm_malloc.h, and under -std=c++11 GCC 8 then fails to
// declare ::abs and most of cstdlib. The builtin needs no header at all.
#if defined(__GNUC__) && defined(__x86_64__)
#  define LLOB_HAVE_TSC 1
#endif

// steady_clock is monotonic and portable, and it times the whole pass. It is
// not good enough for individual datagrams: measured on this machine it only
// advances in steps of 99 ns, even though clock_getres claims 1 ns, so every
// sample would land on a multiple of 99 and the percentiles would be
// quantised into uselessness.
inline uint64_t now_ns() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

#ifdef LLOB_HAVE_TSC

// The CPU's own cycle counter. Two reads cost about one cycle and it resolves
// to a fraction of a nanosecond, which is what sampling a datagram needs.
//
// Two things make it usable here. The counter is invariant on any modern
// x86-64 part: it ticks at a fixed rate regardless of what the core clocks
// itself to. And the thread is pinned, so it cannot migrate to a core whose
// counter started at a different point.
inline uint64_t now_cycles() { return (uint64_t)__builtin_ia32_rdtsc(); }

// Cycles mean nothing until they are tied to real time, so the rate is
// measured once against steady_clock over a long enough window that its 99 ns
// step does not matter.
inline double calibrate_cycles_per_ns() {
    const uint64_t w0 = now_ns();
    const uint64_t c0 = now_cycles();
    while (now_ns() - w0 < 200000000ULL) { }      // 200 ms
    const uint64_t c1 = now_cycles();
    const uint64_t w1 = now_ns();

    const uint64_t elapsed = w1 - w0;
    if (elapsed == 0) return 1.0;
    return (double)(c1 - c0) / (double)elapsed;
}

#else

// Without a cycle counter the wall clock has to do, coarse as it is.
inline uint64_t now_cycles() { return now_ns(); }
inline double   calibrate_cycles_per_ns() { return 1.0; }

#endif

// What two back-to-back reads of the sampling clock cost, in cycles. Reported
// alongside the percentiles so they can be read knowing what the instrument
// itself contributes.
inline uint64_t measure_timer_overhead_cycles() {
    uint64_t best = (uint64_t)-1;
    for (int i = 0; i < 20000; ++i) {
        const uint64_t a = now_cycles();
        const uint64_t b = now_cycles();
        if (b - a < best) best = b - a;    // the minimum is the cleanest estimate
    }
    return best;
}

struct LatencyReport {
    uint64_t samples;
    uint64_t p50, p90, p99, p999, max;
    uint64_t mean;
    uint64_t overflow;       // samples past the histogram's range
    bool     saturated;      // a reported percentile fell in the overflow

    LatencyReport()
        : samples(0), p50(0), p90(0), p99(0), p999(0), max(0),
          mean(0), overflow(0), saturated(false) {}
};

class LatencyHistogram {
public:
    static const std::size_t BUCKETS = 4096;    // 1 ns each, 0 .. 4095 ns

    LatencyHistogram() { reset(); }

    void reset() {
        for (std::size_t i = 0; i < BUCKETS; ++i) buckets_[i] = 0;
        overflow_ = 0; count_ = 0; sum_ = 0; max_ = 0;
    }

    void record(uint64_t ns) {
        ++count_;
        sum_ += ns;
        if (ns > max_) max_ = ns;
        if (ns < BUCKETS) ++buckets_[ns]; else ++overflow_;
    }

    uint64_t samples() const { return count_; }

    LatencyReport report() const {
        LatencyReport r;
        r.samples  = count_;
        r.overflow = overflow_;
        r.max      = max_;
        if (count_ == 0) return r;

        r.mean = (uint64_t)(sum_ / count_);
        r.p50  = quantile(0.50,  r.saturated);
        r.p90  = quantile(0.90,  r.saturated);
        r.p99  = quantile(0.99,  r.saturated);
        r.p999 = quantile(0.999, r.saturated);
        return r;
    }

private:
    // Nearest rank: the smallest value that at least p of the samples are at
    // or below. No interpolation between buckets, so every number reported is
    // one that was actually observed.
    //
    // Walks the buckets until the running total reaches the target share. A
    // target that falls past the last bucket can only be answered with the
    // maximum, and that is flagged rather than passed off as exact.
    uint64_t quantile(double p, bool& saturated) const {
        const uint64_t target = (uint64_t)(p * (double)count_);
        uint64_t seen = 0;
        for (std::size_t i = 0; i < BUCKETS; ++i) {
            seen += buckets_[i];
            if (seen >= target) return (uint64_t)i;
        }
        saturated = true;
        return max_;
    }

    uint64_t buckets_[BUCKETS];
    uint64_t overflow_;
    uint64_t count_;
    uint64_t sum_;
    uint64_t max_;
};

} // namespace llob

#endif // LLOB_APP_LATENCY_HPP
