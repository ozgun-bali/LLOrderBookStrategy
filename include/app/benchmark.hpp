#ifndef LLOB_APP_BENCHMARK_HPP
#define LLOB_APP_BENCHMARK_HPP

// Aynı data'yı tekrar tekrar oynatıp ne kadar sürdüğünü raporluyor.
//
// Assignment'ın koyduğu kurallar, ve her birinin neden orada olduğu:
//
//   Input, hiçbir şey ölçülmeden önce bellekte. Okumayı ölçmek diski ölçmek
//   olurdu.
//
//   En az bir warm-up pass'ı ölçülmeden koşuyor. Soğuk bir ilk pass kararlı
//   haline göre kat kat yavaş olabiliyor, ve bunun hiçbiri kodun yaptığı bir şey
//   değil: yeni allocate edilmiş 584 MB'lık sayfa tek tek page fault'la içeri
//   giriyor, cache'ler ve branch predictor'lar boş başlıyor, ve çekirdek hâlâ
//   düşük frekansta olabiliyor.
//
//   Data çok kez tekrar oynatılıyor. Bir pass bir örnek demek, ve tek bir
//   scheduler kararı ya da bir interrupt onu kolayca onda bir oynatıyor.
//
//   Timed region'ın içinde hiç dosya ya da konsol çıktısı yok.
//
//   Programdan bir checksum çıkıyor. Hiçbir şeyin bağlı olmadığı bir değer
//   olmazsa -O3 bütün hesabın ölü olduğunu fark edip onu kaldırmakta haklı, ve
//   ölçüm hiçbir şeyin ölçümü olurdu.
//
// Her iterasyon yeni bir pipeline kuruyor, yani her pass boş bir book'la başlıyor
// ve birebir aynı işi yapıyor.

#include "app/alloc_counter.hpp"
#include "app/latency.hpp"
#include "app/pipeline.hpp"

#include <cstdio>
#include <fstream>
#include <string>

namespace llob {

struct BenchmarkResult {
    uint64_t iterations;

    double total_seconds;       // ölçülen bütün iterasyonlar boyunca
    double best_seconds;        // en temiz tek pass
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
    bool     checksum_stable;   // bütün iterasyonlar aynı fikirdeydi

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

// VmHWM, resident memory'nin high water mark'ı: sürecin gerçekten tuttuğu en
// yüksek miktar, yani raporlanmaya değer olan sayı.
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
        std::getline(f, key);   // satırın kalanını atla
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

    // Cycle sayacını, onunla bir şey ölçülmeden önce gerçek zamana bağla.
    const double cyclesPerNs = calibrate_cycles_per_ns();
    const double nsPerCycle  = (cyclesPerNs > 0) ? 1.0 / cyclesPerNs : 1.0;
    r.tsc_ghz           = cyclesPerNs;
    r.tsc_resolution_ns = nsPerCycle;
    r.timer_overhead_ns = (double)measure_timer_overhead_cycles() * nsPerCycle;

    // Warm-up. Ölçülmüyor, sayılmıyor, ve sonuçları atılıyor.
    {
        OrderLog log;
        Pipeline p(cfg, log);
        p.run(data, size);
    }

    LatencyHistogram hist;
    bool first = true;

    for (uint64_t i = 0; i < iterations; ++i) {
        OrderLog log;
        Pipeline p(cfg, log);          // allocate ediyor; bilerek reset'ten önce

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

        // Hot path ne istediyse, sonuncuyu değil en kötü durumu sakla.
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
            r.checksum_stable = false;   // aynı input artık aynı sonucu vermiyor
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
