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
  // 0'dan 999'a her nanosaniyede bir örnek, yani cevaplar baştan belli.
  // Nearest rank: p, örneklerin en az %p kadarının altında veya eşit olduğu en
  // küçük değer. Bunların yarısı 499'un altında veya eşit, yani medyan o.
  LatencyHistogram h;
  for (uint64_t ns = 0; ns < 1000; ++ns) h.record(ns);

  const LatencyReport r = h.report();
  CHECK_EQ(r.samples, 1000ull);
  CHECK_EQ(r.p50, 499ull);
  CHECK_EQ(r.p90, 899ull);
  CHECK_EQ(r.p99, 989ull);
  CHECK_EQ(r.max, 999ull);
  CHECK_EQ(r.mean, 499ull);          // (0 + 999) / 2, aşağı yuvarlanmış
}

TEST(histogram_lets_a_rare_outlier_show_in_the_tail_only) {
  // Percentile'ın bütün olayı: binde biri bin kat uzun sürerken ortalamanın 60
  // çıkması ve iyi görünmesi.
  LatencyHistogram h;
  for (int i = 0; i < 999; ++i) h.record(50);
  h.record(50000);

  const LatencyReport r = h.report();
  CHECK_EQ(r.p50, 50ull);
  CHECK_EQ(r.p99, 50ull);            // p99'da hâlâ saklı
  CHECK_EQ(r.max, 50000ull);         // onu sadece maksimum ele veriyor
  CHECK_EQ(r.overflow, 1ull);        // histogram'ın menzilinin dışında
}

TEST(histogram_keeps_the_true_maximum_past_its_range) {
  LatencyHistogram h;
  h.record(10);
  h.record(999999);                  // son bucket'ın çok ötesinde

  const LatencyReport r = h.report();
  CHECK_EQ(r.max, 999999ull);        // ayrı takip ediliyor, kırpılmıyor
  CHECK_EQ(r.overflow, 1ull);
  CHECK_EQ(r.samples, 2ull);
}

TEST(histogram_flags_a_percentile_that_fell_past_its_range) {
  // Örneklerin çoğu overflow'a düşüyor, yani medyan bucket'lardan
  // cevaplanamıyor.
  LatencyHistogram h;
  h.record(1);
  for (int i = 0; i < 100; ++i) h.record(100000);

  const LatencyReport r = h.report();
  CHECK(r.saturated);                // tahmin edilmek yerine açıkça söyleniyor
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
  // Timed region'ın içinden kullanılıyor, yani oradaki her şey kadar sessiz
  // olmak zorunda.
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
  // steady_clock burada yaklaşık 99 ns'lik adımlarla ilerliyor, bu da her
  // datagram örneğini aynı birkaç değere kuantalardı. Percentile'ları anlamlı
  // kılan şey cycle sayacı.
  const double cyclesPerNs = calibrate_cycles_per_ns();
  CHECK(cyclesPerNs > 0.1);          // hiç olmazsa makul bir oran
  CHECK(cyclesPerNs < 100.0);

  const uint64_t overhead = measure_timer_overhead_cycles();
  CHECK(overhead < 1000);            // okuması ucuz
}

TEST(the_allocation_counter_sees_a_new) {
  alloc_reset();
  CHECK_EQ(alloc_count(), 0u);

  std::vector<int>* v = new std::vector<int>();
  v->reserve(64);
  CHECK(alloc_count() >= 2u);        // önce nesne, sonra buffer'ı
  CHECK(alloc_bytes() > 0u);
  delete v;
}

TEST(the_allocation_counter_stays_still_when_nothing_is_asked_for) {
  std::vector<int> v;
  v.reserve(1024);                   // baştan allocate et, pencerenin dışında

  alloc_reset();
  for (int i = 0; i < 1024; ++i) v.push_back(i);   // reserve'in içine sığıyor

  CHECK_EQ(alloc_count(), 0u);
}
