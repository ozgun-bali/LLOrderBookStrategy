#ifndef LLOB_BOOK_GROUP_JOURNAL_HPP
#define LLOB_BOOK_GROUP_JOURNAL_HPP

// Her price level'ın, içinde bulunduğumuz timestamp group'u başladığında nasıl
// göründüğü.
//
// Strategy tamamlanmış bir group üzerinde çalışıyor ve bitmiş book'un
// cevaplayamadığı üç soruyu cevaplamak zorunda: bu price group'tan önce dolu
// muydu, hangi side'daydı, ve group sırasında side değiştirdi mi. Bunu ancak
// group çalışırken tutulan bir kayıt söyleyebilir.
//
// Group başına book'un snapshot'ını almak söz konusu değil - sample data'da 197
// bin group'a karşı 16 bin level var. Onun yerine sadece gerçekten dokunulan
// price'lar kaydediliyor, ve asıl önemli olan ilk dokunuş: o anda bu group'ta
// level'ı henüz hiçbir şey değiştirmemiş, yani içindeki şey group öncesi durum.
//
// İşin diğer yarısı temizlik. Array'leri group'lar arasında silmek snapshot
// almak kadar pahalı olurdu, o yüzden hiçbir şey silinmiyor: her entry kendisinin
// hangi group için yazıldığını taşıyor, ve stale bir entry sadece numarası artık
// tutmayan entry demek. Bir group'u bitirmek ++current_ etmekten ibaret.

#include "core/types.hpp"

#include <cassert>
#include <vector>

namespace llob {

class GroupJournal {
public:
    explicit GroupJournal(std::size_t max_levels)
        : generation_(max_levels, 0), before_(max_levels, 0),
          touched_(max_levels, 0), current_(1) {
        // Entry'ler generation 0'dan başlıyor ve ilk group 1, yani hiçbir şey
        // olmadan önce her slot "dokunulmamış" okunuyor.
        assert(max_levels > 0);
    }

    uint32_t group() const { return current_; }

    // Çalışmakta olan group'u bitirip bir sonrakini başlatıyor. Eski group için
    // yazılmış her entry sırf aritmetik sayesinde stale oluyor.
    void begin_group() { ++current_; }

    // PriceLevels bir level'ı değiştirmeden hemen önce, level hâlâ eski halini
    // tutarken çağırıyor. `side` birazdan değişecek olan side; iki flag ise bu
    // price'ın şu andaki doluluk durumu, her side için ayrı.
    void note(std::size_t index, Side side, bool bid_occupied, bool ask_occupied) {
        assert(index < generation_.size());
        assert(side == SIDE_BUY || side == SIDE_SELL);

        if (generation_[index] != current_) {
            generation_[index] = current_;
            before_[index]     = (uint8_t)sideOf(bid_occupied, ask_occupied);
            touched_[index]    = 0;          // eski group'tan kalan stale mask
        }
        touched_[index] |= (uint8_t)(1u << side);
    }

    bool changed_this_group(std::size_t index) const {
        assert(index < generation_.size());
        return generation_[index] == current_;
    }

    // Group başladığında bu price hangi side'daydı. SIDE_INVALID boştu demek,
    // yani oradan bir şey kaybolmadı ve alınacak bir yön yok. Sadece
    // changed_this_group() true olduğunda anlamlı.
    Side side_before(std::size_t index) const {
        assert(changed_this_group(index));
        return (Side)before_[index];
    }

    // Group sırasında iki side da değişmiş. Cross olmayan bir book'ta bu, price'ın
    // bir side'dan diğerine geçtiği anlamına geliyor - yani level'dan likidite
    // kaybolması değil, price'ın o level'ın içinden geçip gitmesi.
    bool flipped(std::size_t index) const {
        assert(index < generation_.size());
        if (generation_[index] != current_) return false;
        return touched_[index] == (BID_BIT | ASK_BIT);
    }

    uint8_t touched_mask(std::size_t index) const {
        assert(index < generation_.size());
        return generation_[index] == current_ ? touched_[index] : (uint8_t)0;
    }

private:
    static const uint8_t BID_BIT = 1u << SIDE_BUY;    // 0b01
    static const uint8_t ASK_BIT = 1u << SIDE_SELL;   // 0b10

    // Bir price'ın aynı anda iki side'da olması book'un cross olduğu anlamına
    // geliyor, ve bu bir group sınırını geçmemeli. Buradan yön okunmasın diye
    // "bilinmiyor" sayılıyor.
    static Side sideOf(bool bid, bool ask) {
        if (bid && ask) return SIDE_INVALID;
        if (bid)        return SIDE_BUY;
        if (ask)        return SIDE_SELL;
        return SIDE_INVALID;
    }

    std::vector<uint32_t> generation_;
    std::vector<uint8_t>  before_;
    std::vector<uint8_t>  touched_;
    uint32_t              current_;
};

} // namespace llob

#endif // LLOB_BOOK_GROUP_JOURNAL_HPP
