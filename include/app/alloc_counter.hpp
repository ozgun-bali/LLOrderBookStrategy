#ifndef LLOB_APP_ALLOC_COUNTER_HPP
#define LLOB_APP_ALLOC_COUNTER_HPP

// Programın yaptığı her heap allocation'ı sayıyor.
//
// Global operator new, alloc_counter.cpp içinde değiştirildiği için bu sayaç
// hepsini görüyor: her new, büyüyen her vector, kurulan her string. Hiçbiri
// bundan saklanamıyor.
//
// Amaç bir iddiayı bir ölçüme çevirmek. "Hot path'te allocation yok" bir iddia;
// run'dan sonra sıfır okuyan bir sayaç ise kanıt, ve assignment'ın raporlanmasını
// istediği şey kanıt.
//
// Önemli olmasının sebebi, allocation'ın öngörülemez latency'nin tek başına en
// kötü kaynağı olması. malloc boş bir blok bulup yirmi nanosaniyede dönebilir,
// free list'lerini gezebilir, ya da yeni sayfa için kernel'e gidip mikrosaniyeler
// harcayabilir. Ortalama bunu saklıyor; tail saklamıyor.

#include <cstddef>

namespace llob {

// Tek thread, yani düz bir sayaç yeterli ve maliyeti bir increment.
extern std::size_t g_alloc_count;
extern std::size_t g_alloc_bytes;

inline void alloc_reset() { g_alloc_count = 0; g_alloc_bytes = 0; }
inline std::size_t alloc_count() { return g_alloc_count; }
inline std::size_t alloc_bytes() { return g_alloc_bytes; }

} // namespace llob

#endif // LLOB_APP_ALLOC_COUNTER_HPP
