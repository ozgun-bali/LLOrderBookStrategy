#ifndef LLOB_BOOK_ORDER_POOL_HPP
#define LLOB_BOOK_ORDER_POOL_HPP

// Book'ta bekleyen order'lar için fixed-capacity depolama, artı order id index'i.
//
// Capacity config.max_active_orders'tan geliyor ve bir kez allocate ediliyor.
// Dolduğunda INSERT_FULL dönüyor ki caller strategy'yi durdurabilsin; asla
// büyümüyor, asla üzerine yazmıyor, asla bellek bozmuyor.
//
// ITCH order id'leri sadece order book VE side bazında unique, o yüzden key
// (side, order_id). Slot'lar iki side arasında ortak - order order'dır - ama
// side başına bir index table var, ve her 'E' ve 'D' message'ı side'ı taşıdığı
// için hangi table'a bakılacağı hiçbir zaman belirsiz değil.
//
// Lookup, power-of-two boyutlu ve en fazla yarısı dolu bir table üzerinde open
// addressing + linear probing. Delete işlemi tombstone bırakmak yerine entry'leri
// geri kaydırıyor, çünkü order'lar sürekli siliniyor ve tombstone'lar zamanla her
// probe'u bir scan'e çevirirdi.

#include "core/types.hpp"

#include <cassert>
#include <vector>

namespace llob {

// Static member değil namespace scope: class içindeki bir `static const` sadece
// bir declaration, ve C++11'de bir şey ona reference bağladığı anda ayrı bir
// definition gerekiyor - header-only bir class'ın bunu koyacak yeri yok.
static const uint32_t NO_SLOT = 0xFFFFFFFFu;

struct Order {
    OrderId  id;
    Quantity remaining;
    Price    price;
    uint32_t next_free;   // sadece slot free list'teyken anlamlı
};

enum InsertResult {
    INSERT_OK,
    INSERT_DUPLICATE,   // bu id bu side'da zaten aktif
    INSERT_FULL         // capacity doldu; caller strategy'yi durdurmak zorunda
};

class OrderPool {
public:
    explicit OrderPool(std::size_t capacity)
        : slots_(capacity), free_head_(0), size_(0),
          mask_(tableSize(capacity) - 1),
          shift_(shiftFor(tableSize(capacity))) {
        assert(capacity > 0);
        assert(capacity < NO_SLOT);

        // Her slot'u free list'e zincirle, sonuncusu listeyi sonlandırıyor.
        for (std::size_t i = 0; i + 1 < capacity; ++i)
            slots_[i].next_free = (uint32_t)(i + 1);
        slots_[capacity - 1].next_free = NO_SLOT;

        for (int s = 0; s < SIDE_COUNT; ++s)
            buckets_[s].assign(mask_ + 1, NO_SLOT);
    }

    std::size_t size()     const { return size_; }
    std::size_t capacity() const { return slots_.size(); }
    bool        full()     const { return free_head_ == NO_SLOT; }

    InsertResult insert(Side side, OrderId id, Price price, Quantity qty) {
        assert(side == SIDE_BUY || side == SIDE_SELL);

        std::vector<uint32_t>& table = buckets_[side];
        std::size_t b = home(id);

        while (table[b] != NO_SLOT) {
            if (slots_[table[b]].id == id) return INSERT_DUPLICATE;
            b = (b + 1) & mask_;
        }
        if (free_head_ == NO_SLOT) return INSERT_FULL;

        const uint32_t slot = free_head_;
        free_head_ = slots_[slot].next_free;

        Order& o = slots_[slot];
        o.id        = id;
        o.remaining = qty;
        o.price     = price;
        o.next_free = NO_SLOT;

        table[b] = slot;
        ++size_;
        return INSERT_OK;
    }

    // Slot array'ine işaret ediyor, o array hiç yer değiştirmiyor, yani pointer bu
    // order silinene kadar geçerli kalıyor. Caller'lar `remaining`'i bunun üzerinden
    // değiştiriyor.
    Order* find(Side side, OrderId id) {
        const uint32_t slot = findSlot(side, id);
        return slot == NO_SLOT ? 0 : &slots_[slot];
    }

    const Order* find(Side side, OrderId id) const {
        const uint32_t slot = findSlot(side, id);
        return slot == NO_SLOT ? 0 : &slots_[slot];
    }

    // Bir side'daki aktif order'ları geziyor. Index table sadece dolu slot'lara
    // işaret ettiği için üzerinden geçmek her aktif order'ı tam bir kez ziyaret
    // ediyor. Sıra garantisi yok, ve bu kontrol için - hot path için değil.
    template <typename Fn>
    void for_each(Side side, Fn fn) const {
        assert(side == SIDE_BUY || side == SIDE_SELL);
        const std::vector<uint32_t>& table = buckets_[side];
        for (std::size_t b = 0; b < table.size(); ++b)
            if (table[b] != NO_SLOT) fn(slots_[table[b]]);
    }

    bool remove(Side side, OrderId id) {
        assert(side == SIDE_BUY || side == SIDE_SELL);

        std::vector<uint32_t>& table = buckets_[side];
        std::size_t b = home(id);

        while (table[b] != NO_SLOT) {
            if (slots_[table[b]].id == id) {
                const uint32_t slot = table[b];

                slots_[slot].next_free = free_head_;   // free list'e geri koy
                free_head_ = slot;
                --size_;

                closeGap(table, b);
                return true;
            }
            b = (b + 1) & mask_;
        }
        return false;
    }

private:
    // Multiply-shift: sabit 2^64 / altın oran. Çarpma bit'leri karıştırıyor ve en
    // iyi karışanlar üst bit'ler oluyor, shift de onları alıyor. id'ler sıralı
    // gittiği sürece düz `id % size` de iş görürdü, ama bu yöntem feed hangi deseni
    // üretirse üretsin probe'ları kısa tutuyor.
    std::size_t home(OrderId id) const {
        return (std::size_t)((id * 0x9E3779B97F4A7C15ULL) >> shift_) & mask_;
    }

    uint32_t findSlot(Side side, OrderId id) const {
        assert(side == SIDE_BUY || side == SIDE_SELL);

        const std::vector<uint32_t>& table = buckets_[side];
        std::size_t b = home(id);

        // Boş bir bucket aramayı bitiriyor: bu id insert edilmiş olsaydı, aynı yol
        // üzerindeki ilk boş bucket'a konmuş olurdu.
        while (table[b] != NO_SLOT) {
            if (slots_[table[b]].id == id) return table[b];
            b = (b + 1) & mask_;
        }
        return NO_SLOT;
    }

    // Backward-shift deletion. Bir bucket'ı doğrudan boşaltmak probe zincirini
    // keserdi: bu bucket'ın ötesine itilmiş bir entry'ye bir daha asla
    // ulaşılamazdı. O yüzden ileri doğru yürüyüp, home bucket'ı deliğin üzerinde
    // veya öncesinde olan her şeyi geri çekiyoruz.
    void closeGap(std::vector<uint32_t>& table, std::size_t hole) {
        std::size_t j = hole;
        table[hole] = NO_SLOT;

        for (;;) {
            j = (j + 1) & mask_;
            if (table[j] == NO_SLOT) return;

            const std::size_t k = home(slots_[table[j]].id);

            // Home'u (hole, j] aralığındaysa dokunma: geri çekmek onu kendi
            // başlangıç noktasının önüne koyardı.
            const bool inRange = (hole <= j) ? (hole < k && k <= j)
                                             : (hole < k || k <= j);
            if (inRange) continue;

            table[hole] = table[j];
            table[j]    = NO_SLOT;
            hole        = j;
        }
    }

    // İkinin kuvveti, capacity'nin en az iki katı: 0.5'in altındaki bir load
    // factor ortalama probe sayısını bire yakın tutuyor.
    static std::size_t tableSize(std::size_t capacity) {
        std::size_t n = 1;
        while (n < capacity * 2) n <<= 1;
        return n;
    }

    static unsigned shiftFor(std::size_t tableSize) {
        unsigned bits = 0;
        while ((std::size_t(1) << bits) < tableSize) ++bits;
        return 64 - bits;
    }

    std::vector<Order>    slots_;
    std::vector<uint32_t> buckets_[SIDE_COUNT];
    uint32_t              free_head_;
    std::size_t           size_;
    std::size_t           mask_;
    unsigned              shift_;
};

} // namespace llob

#endif // LLOB_BOOK_ORDER_POOL_HPP
