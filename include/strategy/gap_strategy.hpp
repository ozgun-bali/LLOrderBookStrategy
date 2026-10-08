#ifndef LLOB_STRATEGY_GAP_STRATEGY_HPP
#define LLOB_STRATEGY_GAP_STRATEGY_HPP

// Tek tick'lik gap tespiti, tamamlanmış her timestamp group'u için bir kez
// değerlendiriliyor.
//
// Bahis şu: bir level kazara kaybolmuş - biri cancel etmiş, ya da küçük bir
// order onu süpürmüş - ama piyasa hâlâ o price üzerinde anlaşıyor. Deliğe gir,
// best quote ol, spread'i topla. Bu sadece ilk olursan para ediyor; projenin
// tamamının latency etrafında kurulmasının sebebi bu.
//
// Aşağıdaki her koşul bu bahsi korumak için var:
//
//   1. P_SUREKLI_ISLEM        iki taraflı canlı bir book sadece continuous
//                             trading'de var
//   2. sequence stream sağlam kaçırılmış message'lar üzerine kurulmuş bir book
//                             kurgudan ibaret
//   3. iki side'da da level   yoksa ölçülecek bir spread yok
//   4. tam iki tick           bir boş adım; üç ve üstü yeni açılmış bir delik
//                             değil, sığ bir piyasa demek
//   5. bu group'un açtığı gap fırsat olayda, durumda değil. Bir süredir açık
//                             duran bir gap'i zaten herkes alabilirdi
//   6. o price'ta flip yok    price bid'den ask'e geçip sonra boşaldıysa piyasa
//                             o level'ın içinden geçmiş demektir. Orayı
//                             doldurmak likidite yerine koymak değil, hareketin
//                             önüne atlamak olur
//   7. gap başına tek order   yoksa aynı delik her group'ta tetiklenirdi
//
// Sink üzerinden template olmasının sebebi alttaki katmanlarla aynı: çağrı
// inline oluyor, ve testler arkalarında bir fill simulator olmadan intent
// kaydedebiliyor.

#include "core/config.hpp"
#include "core/types.hpp"
#include "book/order_book.hpp"
#include "strategy/order_intent.hpp"

namespace llob {

struct StrategyStats {
    uint64_t evaluations;
    uint64_t intents;

    // Neden hiçbir şey gönderilmediği. Ayrı ayrı tutuluyor ki rapor sadece kaç
    // order çıktığını değil, günün gerçekte nasıl geçtiğini de söyleyebilsin.
    uint64_t skip_state;
    uint64_t skip_sequence;
    uint64_t skip_one_sided;
    uint64_t skip_spread;
    uint64_t skip_not_this_group;
    uint64_t skip_flip;
    uint64_t skip_no_direction;
    uint64_t skip_duplicate;
    uint64_t skip_position_limit;

    StrategyStats()
        : evaluations(0), intents(0), skip_state(0), skip_sequence(0),
          skip_one_sided(0), skip_spread(0), skip_not_this_group(0),
          skip_flip(0), skip_no_direction(0), skip_duplicate(0),
          skip_position_limit(0) {}
};

template <typename Sink>
class GapStrategy {
public:
    GapStrategy(const Config& cfg, Sink& sink)
        : cfg_(cfg), sink_(sink), last_price_(0), last_side_(SIDE_INVALID),
          have_last_(false) {}

    const StrategyStats& stats() const { return stats_; }

    // Tamamlanmış her timestamp group'u için bir kez; bir group'un message'ları
    // arasında asla çağrılmıyor.
    void evaluate(const OrderBook& book, bool sequence_ok,
                  Position position, Timestamp ts) {
        ++stats_.evaluations;

        forgetIfGapClosed(book);

        if (book.state() != STATE_SUREKLI_ISLEM || book.day_closed()) {
            ++stats_.skip_state; return;
        }
        if (!sequence_ok || !book.healthy()) {
            ++stats_.skip_sequence; return;
        }
        if (!book.has_best(SIDE_BUY) || !book.has_best(SIDE_SELL)) {
            ++stats_.skip_one_sided; return;
        }

        const Price bid = book.best(SIDE_BUY);
        const Price ask = book.best(SIDE_SELL);
        if (ask - bid != 2 * cfg_.tick_size) {
            ++stats_.skip_spread; return;
        }

        const Price gap = bid + cfg_.tick_size;

        // Kural 5. Dokunulmamış olması, level'ın group'un onu bulduğu halde
        // durduğu anlamına geliyor; yani delik zaten oradaydı ve artık kimsenin
        // fırsatı değil.
        if (!book.changed_this_group(gap)) {
            ++stats_.skip_not_this_group; return;
        }
        // Kural 6.
        if (book.flipped_this_group(gap)) {
            ++stats_.skip_flip; return;
        }

        // Boşalan side'ı doldur: bir ask kayboldu, o yüzden satış koyuyoruz.
        const Side was = book.side_before_group(gap);
        Side side;
        IntentReason reason;
        if (was == SIDE_SELL)      { side = SIDE_SELL; reason = REASON_GAP_WAS_ASK; }
        else if (was == SIDE_BUY)  { side = SIDE_BUY;  reason = REASON_GAP_WAS_BID; }
        else { ++stats_.skip_no_direction; return; }   // ya boştu ya da cross'tu

        // Kural 7.
        if (have_last_ && last_price_ == gap && last_side_ == side) {
            ++stats_.skip_duplicate; return;
        }

        const Quantity qty = sendQuantity(side, position);
        if (qty == 0) { ++stats_.skip_position_limit; return; }

        OrderIntent intent;
        intent.timestamp = ts;
        intent.side      = side;
        intent.price     = gap;
        intent.quantity  = qty;
        intent.reason    = reason;

        ++stats_.intents;
        last_price_ = gap;
        last_side_  = side;
        have_last_  = true;

        sink_.on_order_intent(intent);
    }

private:
    // Hatırlanan gap, gerçekten kapanana kadar aynı fırsat sayılıyor. O price'ta
    // yeniden bir şey beklemeye başladığında, sonraki bir açılış yeni bir olay
    // olur ve baştan signal verebilir.
    void forgetIfGapClosed(const OrderBook& book) {
        if (!have_last_) return;
        if (book.occupied_at(SIDE_BUY, last_price_) ||
            book.occupied_at(SIDE_SELL, last_price_)) {
            have_last_ = false;
        }
    }

    // Position limit'e kalan yer, config'deki boyutla sınırlanmış. Her şey
    // signed: minimum_position normalde negatif, ve buradaki herhangi bir adımın
    // unsigned olması short limitini "limit yok"a çevirirdi.
    Quantity sendQuantity(Side side, Position position) const {
        const int64_t room = (side == SIDE_BUY)
            ? (int64_t)cfg_.maximum_position - (int64_t)position
            : (int64_t)position - (int64_t)cfg_.minimum_position;

        if (room <= 0) return 0;

        const int64_t want = (int64_t)cfg_.order_quantity;
        return (Quantity)(room < want ? room : want);
    }

    const Config& cfg_;
    Sink&         sink_;

    Price         last_price_;
    Side          last_side_;
    bool          have_last_;
    StrategyStats stats_;
};

} // namespace llob

#endif // LLOB_STRATEGY_GAP_STRATEGY_HPP
