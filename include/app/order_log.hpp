#ifndef LLOB_APP_ORDER_LOG_HPP
#define LLOB_APP_ORDER_LOG_HPP

// Strategy'nin ürettiği her order intent'i saklıyor; CSV ve checksum için.
//
// CSV'yi order'lar çıktıkça yazmak hot path'e bir system call koyardı ve
// ölçülmeye çalışılan latency'yi boğardı, o yüzden satırlar en sonda, burada
// tutulanlardan formatlanıyor.
//
// Capacity baştan reserve ediliyor. Sample data'nın target book'u günde birkaç
// yüz intent üretiyor, yani reserve bunun kat kat üstünde ve pratikte push_back
// hiç reallocate etmiyor. Bir feed yine de aşarsa vector bir kez büyüyecek;
// taşmanın gizlenmek yerine sayılmasının sebebi bu.

#include "strategy/order_intent.hpp"

#include <vector>

namespace llob {

class OrderLog {
public:
    static const std::size_t DEFAULT_RESERVE = 1u << 16;   // 65,536

    explicit OrderLog(std::size_t reserve = DEFAULT_RESERVE)
        : reserved_(reserve), grew_(false) {
        intents_.reserve(reserve);
    }

    void add(const OrderIntent& intent) {
        if (intents_.size() == reserved_) grew_ = true;
        intents_.push_back(intent);
    }

    std::size_t size() const { return intents_.size(); }
    bool        grew() const { return grew_; }
    const OrderIntent& operator[](std::size_t i) const { return intents_[i]; }

private:
    std::vector<OrderIntent> intents_;
    std::size_t              reserved_;
    bool                     grew_;
};

} // namespace llob

#endif // LLOB_APP_ORDER_LOG_HPP
