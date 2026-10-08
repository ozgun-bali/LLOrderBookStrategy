#ifndef LLOB_APP_STATS_HPP
#define LLOB_APP_STATS_HPP

// Run'ın sonucu, ve üzerinden hesaplanan checksum.
//
// Checksum iki iş yapıyor. Determinizmi kanıtlıyor: aynı input ve aynı config
// her seferinde aynı değeri üretmek zorunda. Ve benchmark modunda hiçbir şey
// basılmadığı için optimizer'ı dürüst tutuyor - programdan çıkan bir değer
// olmazsa -O3 bütün hesabın ölü olduğuna karar verip silmekte serbest, ve ölçüm
// hiçbir şeyin ölçümü olurdu.
//
// FNV-1a ikisi için de yeterli. Sıraya duyarlı, ucuz, ve kriptografik olma
// iddiası yok.

#include "core/types.hpp"
#include "app/order_log.hpp"

namespace llob {

static const uint64_t FNV_OFFSET = 14695981039346656037ULL;
static const uint64_t FNV_PRIME  = 1099511628211ULL;

inline uint64_t fnv1a_byte(uint64_t hash, uint8_t b) {
    return (hash ^ (uint64_t)b) * FNV_PRIME;
}

inline uint64_t fnv1a_u64(uint64_t hash, uint64_t v) {
    for (int i = 7; i >= 0; --i)
        hash = fnv1a_byte(hash, (uint8_t)((v >> (i * 8)) & 0xFF));
    return hash;
}

// Assignment'ın programdan basmasını istediği her şey tek bir yerde toplanmış;
// böylece reporter tek argüman alıyor ve checksum'ın katlayacağı tek bir şey var.
struct RunSummary {
    Position position;
    Cash     cash_flow;
    bool     pnl_known;
    Cash     final_pnl;

    bool  have_last_execution;
    Price last_execution_price;

    // Günün hangi price'tan mark edildiği; P_MARJ_YAYIN_KAPANIS geldiğinde
    // yakalanıyor. Feed sonrasında işlem görmeye devam ettiği için bu genelde
    // dosyadaki son price değil, ve bu olmadan P&L tutmuyormuş gibi görünüyor.
    bool  have_mark_price;
    Price mark_price;

    bool     have_best_bid;
    Price    best_bid;
    Quantity best_bid_quantity;
    bool     have_best_ask;
    Price    best_ask;
    Quantity best_ask_quantity;

    uint64_t packets;
    uint64_t itch_messages;
    uint64_t target_messages;
    uint64_t processed_message_count;
    uint64_t sequence_gap_count;
    uint64_t invalid_message_count;
    uint64_t generated_order_count;
    uint64_t groups;
    uint64_t fills;

    uint64_t checksum;

    // Strategy'nin neden sessiz kaldığı. Zorunlu output'un parçası değil, ama
    // "hiç fırsat çıkmadı" ile "bir kural yanlış" arasındaki fark sadece burada
    // görünüyor.
    uint64_t evaluations;
    uint64_t skip_state;
    uint64_t skip_sequence;
    uint64_t skip_one_sided;
    uint64_t skip_spread;
    uint64_t skip_not_this_group;
    uint64_t skip_flip;
    uint64_t skip_no_direction;
    uint64_t skip_duplicate;
    uint64_t skip_position_limit;

    RunSummary()
        : position(0), cash_flow(0), pnl_known(false), final_pnl(0),
          have_last_execution(false), last_execution_price(0),
          have_mark_price(false), mark_price(0),
          have_best_bid(false), best_bid(0), best_bid_quantity(0),
          have_best_ask(false), best_ask(0), best_ask_quantity(0),
          packets(0), itch_messages(0), target_messages(0),
          processed_message_count(0), sequence_gap_count(0),
          invalid_message_count(0), generated_order_count(0),
          groups(0), fills(0), checksum(0),
          evaluations(0), skip_state(0), skip_sequence(0), skip_one_sided(0),
          skip_spread(0), skip_not_this_group(0), skip_flip(0),
          skip_no_direction(0), skip_duplicate(0), skip_position_limit(0) {}
};

// Önce order'ları üretildikleri sırayla katlıyor, sonra son durumu. Neyin karara
// bağlandığında ya da book'un nerede bittiğinde herhangi bir fark, değeri
// değiştiriyor.
inline uint64_t compute_checksum(const RunSummary& s, const OrderLog& log) {
    uint64_t h = FNV_OFFSET;

    for (std::size_t i = 0; i < log.size(); ++i) {
        const OrderIntent& o = log[i];
        h = fnv1a_u64(h, o.timestamp);
        h = fnv1a_u64(h, (uint64_t)o.side);
        h = fnv1a_u64(h, (uint64_t)(uint32_t)o.price);   // signed, keep the bits
        h = fnv1a_u64(h, o.quantity);
        h = fnv1a_u64(h, (uint64_t)o.reason);
    }

    h = fnv1a_u64(h, (uint64_t)s.position);
    h = fnv1a_u64(h, (uint64_t)s.cash_flow);
    h = fnv1a_u64(h, s.pnl_known ? (uint64_t)s.final_pnl : 0ULL);
    h = fnv1a_u64(h, (uint64_t)(uint32_t)s.last_execution_price);
    h = fnv1a_u64(h, s.processed_message_count);
    h = fnv1a_u64(h, s.sequence_gap_count);
    h = fnv1a_u64(h, s.invalid_message_count);
    h = fnv1a_u64(h, s.generated_order_count);
    return h;
}

} // namespace llob

#endif // LLOB_APP_STATS_HPP
