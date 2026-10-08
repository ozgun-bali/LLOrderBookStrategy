#ifndef LLOB_PROTOCOL_ITCH_PARSER_HPP
#define LLOB_PROTOCOL_ITCH_PARSER_HPP

// ITCH message dispatch.
//
// MoldUDP64 ile order book arasında duruyor: MoldSession block'ları bu class'a
// teslim ediyor, bu class da decode edilmiş message'ları yukarı veriyor.
//
// current_second_ burada tutuluyor, 'T' Seconds message'larıyla güncelleniyor.
// 'T' takip ediliyor ama book'a hiç uygulanmıyor: diğer her message'daki
// nanosecond field'ı son 'T'ye göreli, dolayısıyla second sınırını geçerken
// sıralı kalan tek değer compose edilmiş 64 bit timestamp.
//
// Dispatch sırası throughput açısından önemli: önce type byte'ına switch, sonra
// length kontrolü, sonra Order Book ID okunup target değilse çıkış. Feed'deki
// message'ların çoğu başka book'lara ait ve mümkün olduğunca ucuza elenmeli.
//
// Handle edilenler: 'O' 'A' 'E' 'D', artı zaman tutmak için 'T'. Geri kalanı
// skip ediliyor. Gerçek data'da önem taşıyacak ama skip edilen type'lar
// (README'de yazılı):
//   'C' Order Executed with Price - bu da order quantity'sini azaltıyor
//   'Y' Order Book Flush          - bütün book'u temizliyor
//
// Handler üzerinden template olmasının sebebi MoldSession ile aynı: çağrı inline
// oluyor, ve testler order book yerine bir recorder koyabiliyor.
// Handler'ın dört method'a ihtiyacı var:
//   void on_add_order     (Timestamp, const AddOrderMsg&);
//   void on_order_executed(Timestamp, const OrderExecutedMsg&);
//   void on_order_delete  (Timestamp, const OrderDeleteMsg&);
//   void on_book_state    (Timestamp, const BookStateMsg&);

#include "core/types.hpp"
#include "core/byte_reader.hpp"
#include "protocol/itch_messages.hpp"

namespace llob {

static const uint64_t NANOS_PER_SECOND = 1000000000ULL;

struct ItchStats {
    uint64_t messages;          // görülen her ITCH message'ı, book fark etmeksizin
    uint64_t target_messages;   // config'deki order book'a ait message'lar
    uint64_t invalid_messages;  // yanlış length, ya da B/S olmayan bir side byte'ı
    uint64_t skipped_types;     // bu build'in handle etmediği type'lar, 'C' ve 'Y' dahil
    uint64_t seconds_messages;  // zaman tutmak için tüketilen 'T' marker'ları
    uint64_t before_first_second;

    ItchStats()
        : messages(0), target_messages(0), invalid_messages(0),
          skipped_types(0), seconds_messages(0), before_first_second(0) {}
};

template <typename Handler>
class ItchParser {
public:
    ItchParser(Handler& handler, OrderBookId target)
        : handler_(handler), target_(target),
          current_second_(0), have_second_(false) {}

    const ItchStats& stats() const { return stats_; }

    // Message'lar son 'T'den beri geçen nanosecond'ı taşıyor. 'T' gelmemişse mutlak
    // zaman bilinmiyor; feed normalde 'T' ile açıldığı için bu sadece capture
    // stream'in ortasından başladığında önem kazanıyor.
    bool have_second() const { return have_second_; }

    // Doğrudan pass through. Parser'ın sequence sağlığı hakkında bir görüşü yok,
    // ama altındaki strategy, framing layer'ı bir sequence gap fark ettiği anda
    // bunu duymak zorunda.
    void on_sequence_reliable(bool reliable) {
        handler_.on_sequence_reliable(reliable);
    }

    void on_message(const ByteReader& m) {
        if (!m.has(1)) { ++stats_.invalid_messages; return; }
        ++stats_.messages;

        const uint8_t type = m.u8(OFF_MSG_TYPE);

        const std::size_t want = expected_length(type);
        if (want == 0) { ++stats_.skipped_types; return; }
        if (m.size() != want) { ++stats_.invalid_messages; return; }

        if (type == MSG_SECONDS) {
            current_second_ = m.be32(OFF_SECOND);
            have_second_    = true;
            ++stats_.seconds_messages;
            return;                      // sadece zaman tutuyor, book'a hiç ulaşmıyor
        }

        // Erken filtre. Bu satırın altındaki her şey sadece target book için çalışıyor.
        if (m.be32(bookIdOffset(type)) != target_) return;
        ++stats_.target_messages;

        if (!have_second_) ++stats_.before_first_second;
        const Timestamp ts = composeTimestamp(m.be32(OFF_NANOS));

        switch (type) {
            case MSG_ADD_ORDER: {
                AddOrderMsg a;
                decode_add_order(m, a);
                if (a.side == SIDE_INVALID) { ++stats_.invalid_messages; return; }
                handler_.on_add_order(ts, a);
                break;
            }
            case MSG_ORDER_EXECUTED: {
                OrderExecutedMsg e;
                decode_order_executed(m, e);
                if (e.side == SIDE_INVALID) { ++stats_.invalid_messages; return; }
                handler_.on_order_executed(ts, e);
                break;
            }
            case MSG_ORDER_DELETE: {
                OrderDeleteMsg d;
                decode_order_delete(m, d);
                if (d.side == SIDE_INVALID) { ++stats_.invalid_messages; return; }
                handler_.on_order_delete(ts, d);
                break;
            }
            case MSG_BOOK_STATE: {
                BookStateMsg s;
                decode_book_state(m, s);
                handler_.on_book_state(ts, s);
                break;
            }
            default:
                break;   // unreachable: expected_length geri kalanını zaten eledi
        }
    }

private:
    // 'O' book id'yi 5'te taşıyor, order message'ları 13'te.
    static std::size_t bookIdOffset(uint8_t type) {
        return (type == MSG_BOOK_STATE) ? OFF_STATE_BOOK_ID : OFF_ADD_BOOK_ID;
    }

    Timestamp composeTimestamp(uint32_t nanos) const {
        return (Timestamp)current_second_ * NANOS_PER_SECOND + nanos;
    }

    Handler&    handler_;
    OrderBookId target_;
    uint32_t    current_second_;
    bool        have_second_;
    ItchStats   stats_;
};

} // namespace llob

#endif // LLOB_PROTOCOL_ITCH_PARSER_HPP
