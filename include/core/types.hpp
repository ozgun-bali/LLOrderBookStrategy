#ifndef LLOB_CORE_TYPES_HPP
#define LLOB_CORE_TYPES_HPP

// Ortak scalar type'lar ve enum'lar. Logic yok, <cstdint> dışında include yok.
// Bunları en başta sabitlemek fixed-point disiplinini koruyor: hiçbir yerde
// float kullanılmıyor.

#include <cstdint>

namespace llob {

typedef int32_t  Price;        // kuruş, SIGNED (ITCH'te price field'ları signed)
typedef uint64_t Quantity;
typedef uint64_t OrderId;      // sadece (order book, side) çiftinde unique
typedef uint32_t OrderBookId;
typedef uint64_t Timestamp;    // second * 1e9 + nanosecond, 'T' + message'dan birleşiyor
typedef int64_t Position; // minimum position negatif olabilir -> unsigned olsa altına wrap eder
typedef int64_t Cash; // cash flow için -> tipik bir session'da milyarlarca kat headroom kalıyor
typedef uint32_t LevelIndex; // price level'ı takip etmek için kullanılan index


enum Side {
    SIDE_BUY = 0,
    SIDE_SELL = 1,
    SIDE_COUNT = 2,
    SIDE_INVALID = 3
};

// ITCH data type'ları: 31. bit set ve geri kalan her bit sıfırsa "price yok"
// demek. Add Order mesajında ayrıca market order anlamına geliyor.
static const Price PRICE_NOT_AVAILABLE = INT32_MIN;

enum BookState {
    STATE_UNKNOWN = 0,
    STATE_SUREKLI_ISLEM,       // P_SUREKLI_ISLEM - strateji çalışabilir
    STATE_MARJ_YAYIN_KAPANIS,  // P_MARJ_YAYIN_KAPANIS - gün sonu, mark to market
    STATE_OTHER
};

} // namespace llob

#endif // LLOB_CORE_TYPES_HPP
