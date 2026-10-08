#ifndef LLOB_APP_LATENCY_HPP
#define LLOB_APP_LATENCY_HPP

// Latency örnekleri, histogram olarak tutuluyor.
//
// Percentile hesaplamak için dağılım gerekiyor, ve her örneği saklamak iterasyon
// başına megabyte'lar ve ölçüm sırasında allocation demek olurdu. Sabit bir
// sayaç array'i aynı cevabı veriyor: dört mikrosaniyeye kadar nanosaniye başına
// bir bucket, artı bir overflow bucket'ı ve yanında gerçek maksimum.
//
// 4096 bucket 32 KB ediyor, yani cache'te kalıyor, ve bir örnek kaydetmek bir
// index ile bir increment'ten ibaret.
//
// Percentile'a neden gerek var. Ortalaması elli nanosaniye olan bir sistemde
// binde bir message elli mikrosaniye sürse ortalama yine yüz çıkar ve yine iyi
// görünür. Ama strategy sadece ilk olduğunda para ediyor, ve yavaş durumlar
// feed'in en yoğun olduğu yerde kümeleniyor - bu feed'deki en büyük timestamp
// group'u 3.919 message taşıyor - ki orası tam olarak fırsatların olduğu yer.
// Ortalama bunu saklıyor. Asıl mesele tail.

#include "core/types.hpp"

#include <chrono>
#include <cstddef>

namespace llob {

// <x86intrin.h> yerine __builtin_ia32_rdtsc: o header mm_malloc.h üzerinden
// <stdlib.h>'yi içeri çekiyor, ve -std=c++11 altında GCC 8 sonrasında ::abs ile
// cstdlib'in çoğunu declare edemiyor. Builtin'in hiç header'a ihtiyacı yok.
#if defined(__GNUC__) && defined(__x86_64__)
#  define LLOB_HAVE_TSC 1
#endif

// steady_clock monotonic ve portable, ve bütün pass'ı o ölçüyor. Tek tek
// datagram'lar için yeterli değil: bu makinede ölçüldü, clock_getres 1 ns iddia
// etmesine rağmen sadece 99 ns'lik adımlarla ilerliyor; yani her örnek 99'un bir
// katına düşer ve percentile'lar işe yaramaz hale kuantalanırdı.
inline uint64_t now_ns() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

#ifdef LLOB_HAVE_TSC

// CPU'nun kendi cycle sayacı. İki okumanın maliyeti yaklaşık bir cycle ve
// çözünürlüğü bir nanosaniyenin kesri - bir datagram'ı örneklemek için gereken
// tam olarak bu.
//
// Burada kullanılabilir olmasını iki şey sağlıyor. Sayaç modern her x86-64
// parçasında invariant: çekirdek kendini kaç GHz'e ayarlarsa ayarlasın sabit
// hızda tıklıyor. Ve thread pin'li, yani sayacı farklı bir noktadan başlamış bir
// çekirdeğe göç edemiyor.
inline uint64_t now_cycles() { return (uint64_t)__builtin_ia32_rdtsc(); }

// Cycle'lar gerçek zamana bağlanmadan hiçbir şey ifade etmiyor, o yüzden oran
// steady_clock'a karşı bir kez ölçülüyor - 99 ns'lik adımının önemsiz kalacağı
// kadar uzun bir pencerede.
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

// Cycle sayacı yoksa, kaba da olsa duvar saatiyle idare etmek gerekiyor.
inline uint64_t now_cycles() { return now_ns(); }
inline double   calibrate_cycles_per_ns() { return 1.0; }

#endif

// Örnekleme saatinin arka arkaya iki okumasının cycle cinsinden maliyeti.
// Percentile'ların yanında raporlanıyor ki okuyan, ölçü aletinin kendisinin ne
// kattığını bilerek baksın.
inline uint64_t measure_timer_overhead_cycles() {
    uint64_t best = (uint64_t)-1;
    for (int i = 0; i < 20000; ++i) {
        const uint64_t a = now_cycles();
        const uint64_t b = now_cycles();
        if (b - a < best) best = b - a;    // en temiz tahmin minimum olanı
    }
    return best;
}

struct LatencyReport {
    uint64_t samples;
    uint64_t p50, p90, p99, p999, max;
    uint64_t mean;
    uint64_t overflow;       // histogram'ın menzilini aşan örnekler
    bool     saturated;      // raporlanan bir percentile overflow'a düştü

    LatencyReport()
        : samples(0), p50(0), p90(0), p99(0), p999(0), max(0),
          mean(0), overflow(0), saturated(false) {}
};

class LatencyHistogram {
public:
    static const std::size_t BUCKETS = 4096;    // her biri 1 ns, 0 .. 4095 ns

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
    // Nearest rank: örneklerin en az p kadarının altında veya eşit olduğu en
    // küçük değer. Bucket'lar arasında interpolasyon yok, yani raporlanan her
    // sayı gerçekten gözlenmiş bir değer.
    //
    // Koşan toplam hedef paya ulaşana kadar bucket'ları yürüyor. Son bucket'ın
    // ötesine düşen bir hedefe ancak maksimumla cevap verilebilir, ve bu kesin
    // bir sonuç gibi sunulmak yerine flag'leniyor.
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
