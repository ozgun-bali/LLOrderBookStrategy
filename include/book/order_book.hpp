#ifndef LLOB_BOOK_ORDER_BOOK_HPP
#define LLOB_BOOK_ORDER_BOOK_HPP

// Tek bir target instrument için order book.
//
// İki parçayı birleştiriyor: order'ları tutan ve onları (side, id) ile bulan
// OrderPool, ve price başına toplamlarla her side'ın best price'ını tutan
// PriceLevels. Bu class, her ITCH message'ının ne anlama geldiğini bilen kısım.
//
// Başka book'lara ait message'lar buraya hiç gelmiyor: parser onları decode
// etmeden eliyor. Aşağıdaki her şey message'ın bu book'a ait olduğunu varsayıyor.

#include "core/config.hpp"
#include "core/types.hpp"
#include "book/order_pool.hpp"
#include "book/price_levels.hpp"
#include "protocol/itch_messages.hpp"

namespace llob {

struct BookStats {
    uint64_t adds;
    uint64_t executions;
    uint64_t partial_executions;
    uint64_t deletes;
    uint64_t state_changes;

    uint64_t duplicate_order_ids;   // o side'da zaten aktif olan bir id
    uint64_t unknown_order_ids;     // hiç görmediğimiz bir order'a execute ya da delete
    uint64_t market_orders;         // price sentinel: oturacağı bir level yok
    uint64_t oversized_executions;  // kalandan fazla execute edilen quantity

    BookStats()
        : adds(0), executions(0), partial_executions(0), deletes(0),
          state_changes(0), duplicate_order_ids(0), unknown_order_ids(0),
          market_orders(0), oversized_executions(0) {}
};

class OrderBook {
public:
    explicit OrderBook(const Config& cfg)
        : orders_(cfg.max_active_orders), levels_(cfg.max_price_levels),
          state_(STATE_UNKNOWN), last_exec_price_(0),
          have_last_exec_(false), healthy_(true), day_closed_(false) {}

    // Pool dolduğunda ya da bir price band dışına düştüğünde false oluyor. O
    // noktadan sonra book yanlış, yani strategy onun üzerinden işlem yapmak yerine
    // durmak zorunda.
    bool healthy() const { return healthy_; }

    BookState state()      const { return state_; }
    bool      day_closed() const { return day_closed_; }

    bool  has_best(Side s)      const { return levels_.has_best(s); }
    Price best(Side s)          const { return levels_.best(s); }
    Quantity best_quantity(Side s) const { return levels_.best_quantity(s); }

    bool  has_last_execution()  const { return have_last_exec_; }
    Price last_execution_price() const { return last_exec_price_; }

    Quantity quantity_at(Side s, Price p) const { return levels_.quantity_at(s, p); }
    uint32_t order_count_at(Side s, Price p) const { return levels_.order_count_at(s, p); }
    bool     occupied_at(Side s, Price p) const { return levels_.occupied_at(s, p); }

    std::size_t      live_orders() const { return orders_.size(); }
    const BookStats& stats()       const { return stats_; }

    // Timestamp group sınırı. Engine bunu önceki group değerlendirildikten sonra
    // çağırıyor; journal'ın o group için kaydettiği her şey stale oluyor.
    void begin_group() { levels_.begin_group(); }

    // Mevcut group'un bir price'a ne yaptığı - strategy'nin 5. ve 6. kuralları için.
    bool changed_this_group(Price p) const { return levels_.changed_this_group(p); }
    bool flipped_this_group(Price p) const { return levels_.flipped_this_group(p); }
    Side side_before_group(Price p) const { return levels_.side_before_group(p); }

    void add_order(const AddOrderMsg& m) {
        ++stats_.adds;

        // Market order'ın price'ı yok, yani hiçbir level'da beklemiyor. Sentinel
        // değeri geçirmek band'ın çok dışını index'lemeye yol açardı.
        if (m.price == PRICE_NOT_AVAILABLE) { ++stats_.market_orders; return; }

        const InsertResult r = orders_.insert(m.side, m.order_id, m.price, m.quantity);
        if (r == INSERT_DUPLICATE) {
            // Zaten bekleyen order'ı koru, yenisini yok say: elimizdeki book, daha
            // önceki message'ların tarif ettiği book.
            ++stats_.duplicate_order_ids;
            return;
        }
        if (r == INSERT_FULL) { healthy_ = false; return; }

        if (!levels_.add_order(m.side, m.price, m.quantity)) {
            orders_.remove(m.side, m.order_id);   // geri al, ikisi senkron kalsın
            healthy_ = false;
        }
    }

    void order_executed(const OrderExecutedMsg& m) {
        ++stats_.executions;

        Order* o = orders_.find(m.side, m.order_id);
        if (!o) { ++stats_.unknown_order_ids; return; }

        // Price'ı hiçbir şey silinmeden önce oku: message kendi price'ını
        // taşımıyor, ve last_execution_price buradan geliyor.
        const Price    price = o->price;
        const Quantity left  = o->remaining;

        last_exec_price_ = price;
        have_last_exec_  = true;

        Quantity filled = m.quantity;
        if (filled > left) { ++stats_.oversized_executions; filled = left; }

        if (filled < left) {
            o->remaining -= filled;
            levels_.take_quantity(m.side, price, filled);
            ++stats_.partial_executions;
            return;
        }

        // Tamamen fill oldu. Tam bir fill'in ardından Order Delete gelmiyor, yani
        // onu book'tan düşüren tek şey buradaki silme.
        levels_.remove_order(m.side, price, left);
        orders_.remove(m.side, m.order_id);
    }

    void order_delete(const OrderDeleteMsg& m) {
        ++stats_.deletes;

        Order* o = orders_.find(m.side, m.order_id);
        if (!o) { ++stats_.unknown_order_ids; return; }

        levels_.remove_order(m.side, o->price, o->remaining);
        orders_.remove(m.side, m.order_id);
    }

    void book_state(const BookStateMsg& m) {
        ++stats_.state_changes;
        state_ = m.state;
        if (m.state == STATE_MARJ_YAYIN_KAPANIS) day_closed_ = true;
    }

    // Ucuz kontroller; her message için değil, tamamlanmış bir timestamp group'u
    // için düşünülmüş: bir group'un içinde book meşru şekilde cross olabiliyor,
    // çünkü eski bid'i silmek ve yeni ask'i eklemek iki ayrı message.
    bool invariants_hold() const {
        if (levels_.out_of_band()) return false;
        if (has_best(SIDE_BUY) && has_best(SIDE_SELL))
            if (best(SIDE_BUY) >= best(SIDE_SELL)) return false;
        return true;
    }

    // Her aktif order'ı gezip toplamları level toplamlarıyla karşılaştırıyor.
    // Order sayısıyla doğru orantılı, o yüzden sadece testlerde.
    bool verify_level_sums() const {
        for (int s = 0; s < SIDE_COUNT; ++s) {
            const Side side = (Side)s;
            SumCheck check(levels_, side);
            orders_.for_each(side, check);
            if (!check.ok) return false;
        }
        return true;
    }

private:
    // Lambda yerine functor: C++11 lambda'ları sorunsuz, ama bu şekilde capture
    // açıkça görünüyor ve hata sebebini genişletmek kolay kalıyor.
    struct SumCheck {
        const PriceLevels& levels;
        Side side;
        bool ok;
        SumCheck(const PriceLevels& l, Side s) : levels(l), side(s), ok(true) {}

        void operator()(const Order& o) {
            if (!levels.occupied_at(side, o.price)) ok = false;
            if (levels.quantity_at(side, o.price) < o.remaining) ok = false;
        }
    };

    OrderPool   orders_;
    PriceLevels levels_;
    BookState   state_;
    Price       last_exec_price_;
    bool        have_last_exec_;
    bool        healthy_;
    bool        day_closed_;
    BookStats   stats_;
};

} // namespace llob

#endif // LLOB_BOOK_ORDER_BOOK_HPP
