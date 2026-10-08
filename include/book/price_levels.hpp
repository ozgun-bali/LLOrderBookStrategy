#ifndef LLOB_BOOK_PRICE_LEVELS_HPP
#define LLOB_BOOK_PRICE_LEVELS_HPP

// İki side için price level toplamları, ve her side'ın best price'ı.
//
// Price'lar tam kuruş ve tick_size 1, yani bir level doğrudan price - base_price
// ile adresleniyor. Arama yok, karşılaştırma yok, yeniden sıralama yok: bir
// update tek bir çıkarma ve tek bir array yazması. Sorted bir vector best
// price'ı bedavaya index 0'a koyardı, ama buradaki baskın işlem
// order id -> price -> level, ve o her seferinde bir binary search olurdu;
// dahası insert ve erase index'leri kaydırdığı için bir order kendi level'ının
// nerede olduğunu cache'leyemezdi.
//
// İki side aynı index uzayı üzerinde ayrı array'ler. Level başına bir side tag'i
// tutan tek array belleği yarıya indirirdi, ama bir timestamp group'unun içinde
// bir price gerçekten aynı anda iki side'da olabiliyor - ask'i eklemeden önce
// bid'i silmemek geçerli bir olay sırası - ve tek bir tag bunu tutamaz.
//
// base_price görülen ilk price'tan türetiliyor, band'ın ortasına oturtularak.
// Boyut sample data'dan geldi: hedef book 17.060 ile 20.840 kuruş arasında
// hareket etti, yani 16.384 level'lık yere karşı 3.780 tick'lik bir band.
//
// Bir level boşaldığında best price'ı ileri taşıyan şey occupancy bitset'i.
// Level'ları tek tek yürümek çoğu zaman sorunsuz, arada bir ise felaket olurdu -
// ki bu tam olarak p99.9'u mahveden şeklin ta kendisi.

#include "core/types.hpp"
#include "book/group_journal.hpp"

#include <cassert>
#include <vector>

namespace llob {

struct Level {
    Quantity quantity;      // içindeki order'ların remaining quantity toplamı
    uint32_t order_count;   // burada kaç order bekliyor
};

class PriceLevels {
public:
    explicit PriceLevels(std::size_t max_levels)
        : levels_(), journal_(max_levels), words_((max_levels + 63) / 64),
          max_levels_(max_levels), base_price_(0), have_base_(false),
          out_of_band_(false) {
        assert(max_levels > 0);

        for (int s = 0; s < SIDE_COUNT; ++s) {
            levels_[s].assign(max_levels, zeroLevel());
            occupied_[s].assign(words_, 0);
            best_[s] = NO_LEVEL;
        }
    }

    // İlk price geldiğinde bir kez set ediliyor ve sonra asla oynatılmıyor.
    // Band'ın dışına çıkan bir feed rebase gerektirirdi; burada bu güvenli bir
    // hata olarak ele alınıyor.
    bool have_base()  const { return have_base_; }
    Price base_price() const { return base_price_; }

    // Band dışındaki ilk price'ta kalıcı olarak set oluyor. O noktadan sonra book
    // yanlış, o yüzden caller devam etmek yerine strategy'yi durduruyor.
    bool out_of_band() const { return out_of_band_; }

    bool  has_best(Side side) const { return best_[side] != NO_LEVEL; }
    Price best(Side side) const {
        assert(has_best(side));
        return toPrice(best_[side]);
    }
    Quantity best_quantity(Side side) const {
        assert(has_best(side));
        return levels_[side][best_[side]].quantity;
    }
    uint32_t best_order_count(Side side) const {
        assert(has_best(side));
        return levels_[side][best_[side]].order_count;
    }

    Quantity quantity_at(Side side, Price price) const {
        const std::size_t i = indexOf(price);
        return i == NO_LEVEL ? 0 : levels_[side][i].quantity;
    }
    uint32_t order_count_at(Side side, Price price) const {
        const std::size_t i = indexOf(price);
        return i == NO_LEVEL ? 0 : levels_[side][i].order_count;
    }
    bool occupied_at(Side side, Price price) const {
        const std::size_t i = indexOf(price);
        return i != NO_LEVEL && testBit(side, i);
    }

    // Çalışmakta olan timestamp group'unu bitiriyor. Journal'ın o group hakkında
    // tuttuğu her şey stale oluyor; hiçbir şey silinmiyor.
    void begin_group() { journal_.begin_group(); }

    bool changed_this_group(Price price) const {
        const std::size_t i = indexOf(price);
        return i != NO_LEVEL && journal_.changed_this_group(i);
    }
    bool flipped_this_group(Price price) const {
        const std::size_t i = indexOf(price);
        return i != NO_LEVEL && journal_.flipped(i);
    }
    Side side_before_group(Price price) const {
        const std::size_t i = indexOf(price);
        if (i == NO_LEVEL || !journal_.changed_this_group(i)) return SIDE_INVALID;
        return journal_.side_before(i);
    }

    // Bu level'a bir order katılıyor: quantity büyüyor ve level bir üye kazanıyor.
    bool add_order(Side side, Price price, Quantity qty) {
        if (!have_base_) setBase(price);

        const std::size_t i = indexOf(price);
        if (i == NO_LEVEL) { out_of_band_ = true; return false; }

        noteTouch(i, side);

        Level& lv = levels_[side][i];
        lv.quantity += qty;
        ++lv.order_count;

        if (lv.order_count == 1) {          // level yeni canlandı
            setBit(side, i);
            raiseBest(side, i);
        }
        return true;
    }

    // Order'ın bir kısmı fill oldu. Hâlâ burada bekliyor, yani count değişmiyor.
    void take_quantity(Side side, Price price, Quantity qty) {
        const std::size_t i = indexOf(price);
        if (i == NO_LEVEL) { out_of_band_ = true; return; }

        noteTouch(i, side);

        Level& lv = levels_[side][i];
        assert(lv.quantity >= qty);
        lv.quantity -= qty;
    }

    // Bir order ayrıldı: tamamen fill oldu ya da silindi. Level boşalmış olabilir.
    void remove_order(Side side, Price price, Quantity qty) {
        const std::size_t i = indexOf(price);
        if (i == NO_LEVEL) { out_of_band_ = true; return; }

        noteTouch(i, side);

        Level& lv = levels_[side][i];
        assert(lv.quantity >= qty);
        assert(lv.order_count > 0);

        lv.quantity -= qty;
        --lv.order_count;

        // Boşluk quantity'ye değil count'a bakarak belirleniyor: bir undisclosed
        // order hâlâ burada beklerken görünür quantity'si sıfır olabiliyor.
        if (lv.order_count == 0) {
            lv.quantity = 0;
            clearBit(side, i);
            if (best_[side] == i) carryBestForward(side);
        }
    }

private:
    static const std::size_t NO_LEVEL = (std::size_t)-1;

    static Level zeroLevel() { Level l; l.quantity = 0; l.order_count = 0; return l; }

    // Her değişiklikten önce, level hâlâ eski halini tutarken çağırılıyor. Price,
    // side ve değişiklik öncesi doluluk bilgisinin aynı anda elde olduğu tek an
    // burası - journal'ın bir üst katmanda değil burada yaşamasının sebebi bu.
    void noteTouch(std::size_t i, Side side) {
        journal_.note(i, side, testBit(SIDE_BUY, i), testBit(SIDE_SELL, i));
    }

    void setBase(Price price) {
        // Band'ı ilk price'ın ortasına oturt ki book iki yöne de hareket edebilsin.
        base_price_ = price - (Price)(max_levels_ / 2);
        have_base_  = true;
    }

    std::size_t indexOf(Price price) const {
        if (!have_base_) return NO_LEVEL;
        const int64_t i = (int64_t)price - (int64_t)base_price_;
        if (i < 0 || (uint64_t)i >= (uint64_t)max_levels_) return NO_LEVEL;
        return (std::size_t)i;
    }

    Price toPrice(std::size_t index) const {
        return (Price)((int64_t)base_price_ + (int64_t)index);
    }

    bool testBit(Side side, std::size_t i) const {
        return (occupied_[side][i >> 6] >> (i & 63)) & 1ULL;
    }
    void setBit(Side side, std::size_t i) {
        occupied_[side][i >> 6] |= (1ULL << (i & 63));
    }
    void clearBit(Side side, std::size_t i) {
        occupied_[side][i >> 6] &= ~(1ULL << (i & 63));
    }

    // Yeni bir level best'i sadece iyileştirebilir, asla kötüleştiremez; o yüzden
    // tek bir karşılaştırma yetiyor. Buy en yüksek price'ı, sell en düşüğü istiyor.
    void raiseBest(Side side, std::size_t i) {
        if (best_[side] == NO_LEVEL) { best_[side] = i; return; }
        if (side == SIDE_BUY) { if (i > best_[side]) best_[side] = i; }
        else                  { if (i < best_[side]) best_[side] = i; }
    }

    // Best level boşaldı. Bir sonraki, o side'ın kötüleştiği yönde: buy için
    // aşağı, sell için yukarı.
    void carryBestForward(Side side) {
        const std::size_t from = best_[side];
        best_[side] = (side == SIDE_BUY) ? scanDown(side, from) : scanUp(side, from);
    }

    // `from`'un kesinlikle üstündeki en düşük set bit. Word başına 64 level test
    // ediliyor, ve __builtin_ctzll bir word'ü tek instruction'da ilk set bit'ine
    // çeviriyor.
    std::size_t scanUp(Side side, std::size_t from) const {
        std::size_t i = from + 1;
        if (i >= max_levels_) return NO_LEVEL;

        std::size_t w = i >> 6;
        uint64_t    v = occupied_[side][w] & (~0ULL << (i & 63));

        while (true) {
            if (v) return (w << 6) + (std::size_t)__builtin_ctzll(v);
            if (++w >= words_) return NO_LEVEL;
            v = occupied_[side][w];
        }
    }

    // `from`'un kesinlikle altındaki en yüksek set bit. __builtin_clzll baştaki
    // sıfırları sayıyor, yani 63 eksi o değer en üstteki set bit'in konumu.
    std::size_t scanDown(Side side, std::size_t from) const {
        if (from == 0) return NO_LEVEL;
        std::size_t i = from - 1;

        std::size_t w = i >> 6;
        const unsigned keep = (unsigned)(i & 63);
        uint64_t v = occupied_[side][w];
        if (keep != 63) v &= ~(~0ULL << (keep + 1));

        while (true) {
            if (v) return (w << 6) + (63 - (std::size_t)__builtin_clzll(v));
            if (w == 0) return NO_LEVEL;
            v = occupied_[side][--w];
        }
    }

    std::vector<Level>    levels_[SIDE_COUNT];
    GroupJournal          journal_;
    std::vector<uint64_t> occupied_[SIDE_COUNT];
    std::size_t           best_[SIDE_COUNT];
    std::size_t           words_;
    std::size_t           max_levels_;
    Price                 base_price_;
    bool                  have_base_;
    bool                  out_of_band_;
};

} // namespace llob

#endif // LLOB_BOOK_PRICE_LEVELS_HPP
