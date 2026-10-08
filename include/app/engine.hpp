#ifndef LLOB_APP_ENGINE_HPP
#define LLOB_APP_ENGINE_HPP

// Pipeline'ı birbirine bağlıyor ve timestamp grouping'in sahibi.
//
//   datagram -> MoldUDP64 -> ItchParser -> Engine -> OrderBook
//                                             |
//                                             +-> GapStrategy, group başına bir kez
//
// Aynı ITCH timestamp'ini paylaşan message'lar, borsa açısından tek bir olay.
// Strategy'yi aralarında çalıştırmak ona hiç var olmamış book'lar gösterirdi:
// sample data'da en büyük group 3.919 message taşıyor ve book onun içinde
// defalarca cross olup düzeliyor.
//
// Bu yüzden strategy sadece kapanmış bir group üzerinde çalışıyor, ve bir group
// farklı bir timestamp taşıyan bir message geldiğinde kapanıyor - o message
// uygulanmadan ÖNCE, yoksa bitirdiği group'un içine sızardı. Son group'un
// arkasında böyle bir message yok; finalize() tam olarak bunun için var.

#include "core/types.hpp"
#include "book/order_book.hpp"
#include "protocol/itch_messages.hpp"

namespace llob {

struct EngineStats {
    uint64_t groups;            // tamamlanmış timestamp group'ları
    uint64_t messages;          // uygulanan target book message'ları
    uint64_t invariant_breaks;  // var olamayacak bir book'la biten group'lar

    EngineStats() : groups(0), messages(0), invariant_breaks(0) {}
};

template <typename Strategy>
class Engine {
public:
    // Position okunuyor, asla yazılmıyor: sahibi her kimse - bundan sonrası için
    // bir fill simulator - onu group'lar arasında güncelliyor.
    Engine(OrderBook& book, Strategy& strategy, const Position& position)
        : book_(book), strategy_(strategy), position_(position),
          current_ts_(0), have_ts_(false), sequence_ok_(true) {}

    const EngineStats& stats() const { return stats_; }
    bool sequence_ok() const { return sequence_ok_; }
    Timestamp current_timestamp() const { return current_ts_; }

    void on_sequence_reliable(bool reliable) { sequence_ok_ = reliable; }

    void on_add_order(Timestamp ts, const AddOrderMsg& m) {
        closeGroupIfTimestampChanged(ts);
        ++stats_.messages;
        book_.add_order(m);
    }

    void on_order_executed(Timestamp ts, const OrderExecutedMsg& m) {
        closeGroupIfTimestampChanged(ts);
        ++stats_.messages;
        book_.order_executed(m);
    }

    void on_order_delete(Timestamp ts, const OrderDeleteMsg& m) {
        closeGroupIfTimestampChanged(ts);
        ++stats_.messages;
        book_.order_delete(m);
    }

    // State değişikliği de diğer her message gibi bir timestamp taşıyor, yani o da
    // kendinden önceki group'u kapatıyor.
    void on_book_state(Timestamp ts, const BookStateMsg& m) {
        closeGroupIfTimestampChanged(ts);
        ++stats_.messages;
        book_.book_state(m);
    }

    // Input'un sonu. Son group hâlâ açık ve onu kapatacak bir şey gelmeyecek, o
    // yüzden burada değerlendiriliyor. Unutulması kolay, ve unutmak son group'un
    // taşıdığı signal'i sessizce düşürüyor.
    void finalize() {
        if (!have_ts_) return;
        closeGroup();
        have_ts_ = false;
    }

private:
    void closeGroupIfTimestampChanged(Timestamp ts) {
        if (have_ts_ && ts == current_ts_) return;   // hâlâ aynı olay
        if (have_ts_) closeGroup();
        current_ts_ = ts;
        have_ts_    = true;
    }

    void closeGroup() {
        // Her message'dan sonra değil burada kontrol ediliyor: bir group'un
        // içinde book meşru şekilde cross olabiliyor, çünkü eski bid'i silmek ve
        // yeni ask'i eklemek iki ayrı message.
        if (!book_.invariants_hold()) ++stats_.invariant_breaks;

        strategy_.evaluate(book_, sequence_ok_, position_, current_ts_);

        // Journal group'u ancak şimdi unutuyor - strategy kaydettiklerini
        // okuduktan sonra.
        book_.begin_group();
        ++stats_.groups;
    }

    OrderBook&      book_;
    Strategy&       strategy_;
    const Position& position_;

    Timestamp   current_ts_;
    bool        have_ts_;
    bool        sequence_ok_;
    EngineStats stats_;
};

} // namespace llob

#endif // LLOB_APP_ENGINE_HPP
