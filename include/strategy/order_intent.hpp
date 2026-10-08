#ifndef LLOB_STRATEGY_ORDER_INTENT_HPP
#define LLOB_STRATEGY_ORDER_INTENT_HPP

// Strategy'nin çıktısı, ve nereye gittiği.
//
// Strategy hiçbir zaman dosyaya yazmıyor. Bir intent'i sink'e veriyor, sink'in
// onunla ne yaptığı sink'in işi: gerçek run'da bir fill simulator alıyor,
// testlerde bir recorder topluyor, benchmark'ta ise sadece checksum'a katılıp
// bırakılıyor. Hot path'ten dosyaya yazmak signal başına bir system call demek
// olurdu ve ölçülmeye çalışılan latency'yi boğardı.
//
// Reason bir enum, text değil. CSV kolonunu formatlamak timed region'ın dışında
// yapılıyor; ortalıkta bir kelime taşımak hot path'te string kurmak anlamına
// gelirdi ki assignment bunu açıkça yasaklıyor.

#include "core/types.hpp"

namespace llob {

enum IntentReason {
    REASON_NONE = 0,
    REASON_GAP_WAS_ASK,   // kaybolan level bir ask'ti, o yüzden oraya satıyoruz
    REASON_GAP_WAS_BID    // bid'di, o yüzden alıyoruz
};

// Assignment'taki struct'la birebir, artı CSV'nin ihtiyaç duyduğu reason tag'i.
struct OrderIntent {
    Timestamp    timestamp;
    Side         side;
    Price        price;
    Quantity     quantity;
    IntentReason reason;

    OrderIntent()
        : timestamp(0), side(SIDE_INVALID), price(0), quantity(0),
          reason(REASON_NONE) {}
};

inline const char* reason_text(IntentReason r) {
    switch (r) {
        case REASON_GAP_WAS_ASK: return "gap_was_ask";
        case REASON_GAP_WAS_BID: return "gap_was_bid";
        default:                 return "none";
    }
}

inline const char* side_text(Side s) {
    return s == SIDE_BUY ? "BUY" : (s == SIDE_SELL ? "SELL" : "?");
}

// Sadece intent sayıyor, başka bir şey yapmıyor. Tek başına benchmark için
// faydalı: iş yapılmak zorunda ama output timed region'ın dışında kalmalı.
struct CountingSink {
    uint64_t count;
    CountingSink() : count(0) {}
    void on_order_intent(const OrderIntent&) { ++count; }
};

} // namespace llob

#endif // LLOB_STRATEGY_ORDER_INTENT_HPP
