#ifndef LLOB_SIM_FILL_SIMULATOR_HPP
#define LLOB_SIM_FILL_SIMULATOR_HPP

// Bir order intent'i fill'e çeviriyor.
//
// Modeli assignment sabitliyor: üretilen her order, üretildiği timestamp'in
// sonunda kendi price ve quantity'siyle tamamen fill olmuş sayılıyor. Strategy
// sadece kapanmış bir group üzerinde çalıştığı için, intent var olduğunda o son
// zaten gelmiş oluyor; yani fill anında gerçekleşiyor.
//
// Gerçek bir round trip'in içerdiği her şey atlanıyor - order'ın borsaya
// ulaşması, acknowledgement, book'ta beklemesi, parça parça eşleşmesi, cancel
// edilmesi. Hepsinin yerine üç satır duruyor.
//
// Buna rağmen ayrı bir component, ve assignment iki sebebini de söylüyor: bu
// ayrım puanlanıyor, ve gerçekçi bir modelin değiştireceği yer tam burası.
// Asynchronous acknowledgement, partial fill ve cancel destekleyen bir sürüm
// buraya gelir, ve ne strategy ne de position tracker değişir.
//
// Neyi GÖRMEDİĞİNE dikkat: public ITCH execution message'larını. Onlar diğer
// katılımcıların birbiriyle yaptığı işlemler. Buraya sadece kendi strategy'mizin
// intent'leri geliyor.

#include "core/types.hpp"
#include "sim/position.hpp"
#include "strategy/order_intent.hpp"

namespace llob {

class FillSimulator {
public:
    explicit FillSimulator(PositionTracker& position)
        : position_(position), fills_(0) {}

    uint64_t fills() const { return fills_; }

    void on_order_intent(const OrderIntent& intent) {
        position_.apply_fill(intent.side, intent.price, intent.quantity);
        ++fills_;
    }

private:
    PositionTracker& position_;
    uint64_t         fills_;
};

} // namespace llob

#endif // LLOB_SIM_FILL_SIMULATOR_HPP
