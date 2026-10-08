#ifndef LLOB_SIM_POSITION_HPP
#define LLOB_SIM_POSITION_HPP

// Position ve cash flow, tam kuruş cinsinden.
//
// Her şeyi iki tane koşan sayı taşıyor. Position net kaç lot tutulduğu, kaça
// alındığına dair hiçbir hafızası yok; cash flow ise hesaptan geçen para, yani
// price'ların durduğu yer. İkisi birlikte her an book'u fiyatlıyor, ve hiçbiri
// tek tek işlemlerin saklanmasını gerektirmiyor.
//
//   BUY : cash_flow -= quantity * price;  position += quantity;
//   SELL: cash_flow += quantity * price;  position -= quantity;
//
// Buraya sadece kendi simüle edilmiş fill'lerimiz ulaşıyor. Public ITCH
// execution'ları diğer katılımcılara ait ve buraya asla dokunmamalı.
//
// Hiçbir yerde floating point yok. Her çarpım önce int64_t'ye genişletiliyor:
// Quantity unsigned, Price signed; ikisini doğrudan çarpmak price'ı unsigned'a
// promote eder ve negatif bir price'ı devasa bir pozitif sayıya çevirirdi.

#include "core/types.hpp"

#include <cassert>

namespace llob {

class PositionTracker {
public:
    PositionTracker()
        : quantity_(0), cash_flow_(0), final_pnl_(0),
          fills_(0), mark_price_(0), day_marked_(false),
          pnl_known_(false), have_mark_(false) {}

    // Engine tarafından bağlanıyor ki strategy, order'ı limitlere göre
    // boyutlandırırken canlı position'ı okuyabilsin.
    const Position& position_ref() const { return quantity_; }

    Position quantity()  const { return quantity_; }
    Cash     cash_flow() const { return cash_flow_; }
    uint64_t fills()     const { return fills_; }

    bool day_marked() const { return day_marked_; }
    bool pnl_known()  const { return pnl_known_; }

    // Açık position'ın hangi price'tan değerlendiği - raporun tutması için.
    bool  have_mark_price() const { return have_mark_; }
    Price mark_price()      const { return mark_price_; }
    Cash final_pnl()  const { assert(pnl_known_); return final_pnl_; }

    void apply_fill(Side side, Price price, Quantity qty) {
        assert(side == SIDE_BUY || side == SIDE_SELL);

        const int64_t notional = mul(qty, price);
        if (side == SIDE_BUY) {
            cash_flow_ = sub(cash_flow_, notional);
            quantity_ += (Position)qty;
        } else {
            cash_flow_ = add(cash_flow_, notional);
            quantity_ -= (Position)qty;
        }
        ++fills_;
    }

    // P_MARJ_YAYIN_KAPANIS geldiğinde alınıyor, dosya sonunda değil. Feed
    // kapanıştan sonra da akmaya devam ediyor - auction'lar, yeni execution'lar -
    // ve son işlem price'ı günün bittiği price'tan uzaklaşırdı.
    void mark_end_of_day(bool have_last_price, Price last_price) {
        if (day_marked_) return;        // sayılan, ilk kapanış
        day_marked_ = true;

        if (quantity_ == 0) {
            // Flat, yani değerlenecek bir şey yok: sonuç doğrudan cash flow.
            final_pnl_ = cash_flow_;
            pnl_known_ = true;
        } else if (have_last_price) {
            have_mark_ = true;
            mark_price_ = last_price;
            // İki taraf da zaten signed, yani short bir position negatif bir
            // mark veriyor ve işaret kendi kendini hallediyor.
            final_pnl_ = add(cash_flow_, mulSigned(quantity_, (int64_t)last_price));
            pnl_known_ = true;
        } else {
            // Hâlâ açık bir position var ve feed'in hiç işlem gördüğü bir price
            // yok. Basılacak dürüst bir sayı yok, o yüzden hiçbiri basılmıyor.
            pnl_known_ = false;
        }
    }

private:
    // Çarpmadan önce genişlet. Quantity unsigned, Price signed; ikisini olduğu
    // gibi çarpmak price'ı unsigned'a promote ederdi.
    static int64_t mul(Quantity q, Price p) {
        return mulSigned((int64_t)q, (int64_t)p);
    }

    // __builtin_*_overflow hem işlemi yapıyor hem de wrap edip etmediğini
    // bildiriyor. x86-64'te bu zaten istediğin instruction'ın kendisi artı onun
    // overflow flag'i; release build'de flag okunmadığı için compiler düz bir
    // imul ya da add üretiyor ve kontrolün maliyeti sıfır oluyor.
    //
    // İşlem bilerek assert'in DIŞINDA duruyor: assert'in içine konan ve yan
    // etkisi olan kod NDEBUG ile birlikte yok oluyor, ve burada sonucun kendisi
    // o yan etki.
    static int64_t mulSigned(int64_t a, int64_t b) {
        int64_t out = 0;
        const bool overflowed = __builtin_mul_overflow(a, b, &out);
        assert(!overflowed);
        (void)overflowed;
        return out;
    }

    static int64_t add(int64_t a, int64_t b) {
        int64_t out = 0;
        const bool overflowed = __builtin_add_overflow(a, b, &out);
        assert(!overflowed);
        (void)overflowed;
        return out;
    }

    static int64_t sub(int64_t a, int64_t b) {
        int64_t out = 0;
        const bool overflowed = __builtin_sub_overflow(a, b, &out);
        assert(!overflowed);
        (void)overflowed;
        return out;
    }

    Position quantity_;
    Cash     cash_flow_;
    Cash     final_pnl_;
    uint64_t fills_;
    Price    mark_price_;
    bool     day_marked_;
    bool     pnl_known_;
    bool     have_mark_;
};

} // namespace llob

#endif // LLOB_SIM_POSITION_HPP
