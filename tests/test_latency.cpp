#include "test_framework.hpp"
#include "app/latency.hpp"
#include "app/alloc_counter.hpp"

#include <vector>

using namespace llob;

TEST(histogram_starts_empty) {
  LatencyHistogram h;
  const LatencyReport r = h.report();
  CHECK_EQ(r.samples, 0ull);
  CHECK_EQ(r.max, 0ull);
  CHECK(!r.saturated);
}

TEST(histogram_finds_the_percentiles_of_a_flat_spread) {
  // One sample at every nanosecond from 0 to 999, so the answers are known.
  // Nearest rank: p is the smallest value that at least p% of the samples are
  // at or below. Half of these are at or below 499, so that is the median.
  LatencyHistogram h;
  for (uint64_t ns = 0; ns < 1000; ++ns) h.record(ns);

  const LatencyReport r = h.report();
  CHECK_EQ(r.samples, 1000ull);
  CHECK_EQ(r.p50, 499ull);
  CHECK_EQ(r.p90, 899ull);
  CHECK_EQ(r.p99, 989ull);
  CHECK_EQ(r.max, 999ull);
  CHECK_EQ(r.mean, 499ull);          // (0 + 999) / 2, rounded down
}

TEST(histogram_lets_a_rare_outlier_show_in_the_tail_only) {
  // The point of percentiles: a mean of 60 looks fine while one in a thousand
  // takes a thousand times longer.
  LatencyHistogram h;
  for (int i = 0; i < 999; ++i) h.record(50);
  h.record(50000);

  const LatencyReport r = h.report();
  CHECK_EQ(r.p50, 50ull);
  CHECK_EQ(r.p99, 50ull);            // still hidden at p99
  CHECK_EQ(r.max, 50000ull);         // only the maximum tells on it
  CHECK_EQ(r.overflow, 1ull);        // past the histogram's range
}

TEST(histogram_keeps_the_true_maximum_past_its_range) {
  LatencyHistogram h;
  h.record(10);
  h.record(999999);                  // far beyond the last bucket

  const LatencyReport r = h.report();
  CHECK_EQ(r.max, 999999ull);        // tracked separately, not clamped
  CHECK_EQ(r.overflow, 1ull);
  CHECK_EQ(r.samples, 2ull);
}

TEST(histogram_flags_a_percentile_that_fell_past_its_range) {
  // Most samples overflow, so the median cannot be answered from the buckets.
  LatencyHistogram h;
  h.record(1);
  for (int i = 0; i < 100; ++i) h.record(100000);

  const LatencyReport r = h.report();
  CHECK(r.saturated);                // said out loud rather than guessed at
}

TEST(histogram_reset_clears_everything) {
  LatencyHistogram h;
  for (int i = 0; i < 100; ++i) h.record(42);
  h.reset();

  const LatencyReport r = h.report();
  CHECK_EQ(r.samples, 0ull);
  CHECK_EQ(r.max, 0ull);
  CHECK_EQ(r.overflow, 0ull);
}

TEST(histogram_does_not_allocate_while_recording) {
  // It is used from inside the timed region, so it has to be as quiet as
  // everything else there.
  LatencyHistogram h;
  alloc_reset();
  for (uint64_t i = 0; i < 100000; ++i) h.record(i % 4096);

  CHECK_EQ(alloc_count(), 0u);
}

TEST(the_sampling_clock_moves_forward) {
  const uint64_t a = now_cycles();
  uint64_t b = a;
  for (int i = 0; i < 1000 && b == a; ++i) b = now_cycles();
  CHECK(b > a);
}

TEST(the_sampling_clock_resolves_better_than_the_wall_clock) {
  // steady_clock advances in steps of about 99 ns here, which would quantise
  // every datagram sample onto the same handful of values. The cycle counter
  // is what makes the percentiles mean anything.
  const double cyclesPerNs = calibrate_cycles_per_ns();
  CHECK(cyclesPerNs > 0.1);          // a plausible rate at all
  CHECK(cyclesPerNs < 100.0);

  const uint64_t overhead = measure_timer_overhead_cycles();
  CHECK(overhead < 1000);            // reading it is cheap
}

TEST(the_allocation_counter_sees_a_new) {
  alloc_reset();
  CHECK_EQ(alloc_count(), 0u);

  std::vector<int>* v = new std::vector<int>();
  v->reserve(64);
  CHECK(alloc_count() >= 2u);        // the object, then its buffer
  CHECK(alloc_bytes() > 0u);
  delete v;
}

TEST(the_allocation_counter_stays_still_when_nothing_is_asked_for) {
  std::vector<int> v;
  v.reserve(1024);                   // allocate up front, outside the window

  alloc_reset();
  for (int i = 0; i < 1024; ++i) v.push_back(i);   // fits in the reserve

  CHECK_EQ(alloc_count(), 0u);
}
