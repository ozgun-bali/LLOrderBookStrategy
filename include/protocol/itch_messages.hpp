#ifndef LLOB_PROTOCOL_ITCH_MESSAGES_HPP
#define LLOB_PROTOCOL_ITCH_MESSAGES_HPP

// ITCH message field offset'leri ve decode edilmiş view'ları.
//
// Bütün offset'ler BISTECH ITCH Protocol Specification (BIST v2112) ile
// doğrulandı.
// Numeric = unsigned big-endian. Price = SIGNED big-endian, 4 byte.
// Alpha = sola yaslı, space ile pad'lenmiş, ISO 8859-1, null terminator YOK.
//
// Decode işlemi, caller'ın uzunluğu ByteReader::has() ile zaten kontrol ettiğini
// varsayıyor. Struct'lar stack'te local; -O3 onları register'da tutuyor, hiçbiri
// allocate etmiyor.

#include "core/types.hpp"
#include "core/byte_reader.hpp"

#include <cstring>

namespace llob {

// ----------------------------------------------------------------- message type

static const uint8_t MSG_SECONDS         = 'T';
static const uint8_t MSG_BOOK_STATE      = 'O';
static const uint8_t MSG_ADD_ORDER       = 'A';
static const uint8_t MSG_ORDER_EXECUTED  = 'E';
static const uint8_t MSG_ORDER_DELETE    = 'D';

// Handle edilmiyor, ama isimlendirildi çünkü README'de bunları skip etmenin
// bedelini açıklamak gerekiyor: 'C' de bir order'ın quantity'sini azaltıyor,
// 'Y' ise bütün book'u temizliyor.
static const uint8_t MSG_EXECUTED_PRICE  = 'C';
static const uint8_t MSG_BOOK_FLUSH      = 'Y';

// -------------------------------------------------------------- fixed length'ler

static const std::size_t LEN_SECONDS        = 5;
static const std::size_t LEN_BOOK_STATE     = 29;
static const std::size_t LEN_ADD_ORDER      = 45;
static const std::size_t LEN_ORDER_EXECUTED = 52;
static const std::size_t LEN_ORDER_DELETE   = 18;

// ------------------------------------------------------------- field offset'leri

// Her message aynı şekilde başlıyor.
static const std::size_t OFF_MSG_TYPE = 0;
static const std::size_t OFF_NANOS    = 1;

// 'T' Seconds
static const std::size_t OFF_SECOND = 1;

// 'O' Order Book State — dikkat: book id burada 13'te değil, 5'te
static const std::size_t OFF_STATE_BOOK_ID = 5;
static const std::size_t OFF_STATE_NAME    = 9;
static const std::size_t LEN_STATE_NAME    = 20;

// 'A' Add Order
static const std::size_t OFF_ADD_ORDER_ID = 5;
static const std::size_t OFF_ADD_BOOK_ID  = 13;
static const std::size_t OFF_ADD_SIDE     = 17;
static const std::size_t OFF_ADD_QUANTITY = 22;
static const std::size_t OFF_ADD_PRICE    = 30;

// 'E' Order Executed
static const std::size_t OFF_EXE_ORDER_ID = 5;
static const std::size_t OFF_EXE_BOOK_ID  = 13;
static const std::size_t OFF_EXE_SIDE     = 17;
static const std::size_t OFF_EXE_QUANTITY = 18;

// 'D' Order Delete
static const std::size_t OFF_DEL_ORDER_ID = 5;
static const std::size_t OFF_DEL_BOOK_ID  = 13;
static const std::size_t OFF_DEL_SIDE     = 17;

// -------------------------------------------------------------------- state name

// 20 byte'lık Alpha field'lar, space ile pad'lenmiş. memcmp ile compare ediliyor
// ki hot path hiç std::string kurmasın. Literal'lerin sonundaki NUL compare'e
// girmiyor.
static const char STATE_NAME_SUREKLI_ISLEM[]      = "P_SUREKLI_ISLEM     ";
static const char STATE_NAME_MARJ_YAYIN_KAPANIS[] = "P_MARJ_YAYIN_KAPANIS";

inline BookState book_state_from_name(const uint8_t* p) {
    if (std::memcmp(p, STATE_NAME_SUREKLI_ISLEM, LEN_STATE_NAME) == 0)
        return STATE_SUREKLI_ISLEM;
    if (std::memcmp(p, STATE_NAME_MARJ_YAYIN_KAPANIS, LEN_STATE_NAME) == 0)
        return STATE_MARJ_YAYIN_KAPANIS;
    return STATE_OTHER;
}

// ------------------------------------------------------------------------- side

// Wire'dan her şey gelebilir, o yüzden tanınmayan bir byte sessizce buy gibi
// davranıp side başına tutulan table'ı index'lemek yerine SIDE_INVALID oluyor.
inline Side side_from_char(uint8_t c) {
    if (c == 'B') return SIDE_BUY;
    if (c == 'S') return SIDE_SELL;
    return SIDE_INVALID;
}

// ------------------------------------------------------- decode edilmiş view'lar

struct AddOrderMsg {
    uint32_t nanos;
    OrderId  order_id;
    Side     side;
    Quantity quantity;
    Price    price;
};

struct OrderExecutedMsg {
    uint32_t nanos;
    OrderId  order_id;
    Side     side;
    Quantity quantity;      // çıkarılacak delta, running total değil
};

struct OrderDeleteMsg {
    uint32_t nanos;
    OrderId  order_id;
    Side     side;
};

struct BookStateMsg {
    uint32_t  nanos;
    BookState state;
};

// ---------------------------------------------------------------------- decoder
//
// Order book id'si burada bilerek decode edilmiyor: parser onu doğrudan buffer'dan
// okuyup, target dışındaki message'ları buraya hiç gelmeden filtreliyor.

inline void decode_add_order(const ByteReader& r, AddOrderMsg& out) {
    assert(r.has(LEN_ADD_ORDER));
    out.nanos    = r.be32(OFF_NANOS);
    out.order_id = r.be64(OFF_ADD_ORDER_ID);
    out.side     = side_from_char(r.u8(OFF_ADD_SIDE));
    out.quantity = r.be64(OFF_ADD_QUANTITY);
    out.price    = (Price)r.be32(OFF_ADD_PRICE);
}

inline void decode_order_executed(const ByteReader& r, OrderExecutedMsg& out) {
    assert(r.has(LEN_ORDER_EXECUTED));
    out.nanos    = r.be32(OFF_NANOS);
    out.order_id = r.be64(OFF_EXE_ORDER_ID);
    out.side     = side_from_char(r.u8(OFF_EXE_SIDE));
    out.quantity = r.be64(OFF_EXE_QUANTITY);
}

inline void decode_order_delete(const ByteReader& r, OrderDeleteMsg& out) {
    assert(r.has(LEN_ORDER_DELETE));
    out.nanos    = r.be32(OFF_NANOS);
    out.order_id = r.be64(OFF_DEL_ORDER_ID);
    out.side     = side_from_char(r.u8(OFF_DEL_SIDE));
}

inline void decode_book_state(const ByteReader& r, BookStateMsg& out) {
    assert(r.has(LEN_BOOK_STATE));
    out.nanos = r.be32(OFF_NANOS);
    out.state = book_state_from_name(r.raw(OFF_STATE_NAME));
}

// Bu type'taki bir message'ın sahip olması gereken uzunluk; handle edilmiyorsa 0.
inline std::size_t expected_length(uint8_t type) {
    switch (type) {
        case MSG_SECONDS:        return LEN_SECONDS;
        case MSG_BOOK_STATE:     return LEN_BOOK_STATE;
        case MSG_ADD_ORDER:      return LEN_ADD_ORDER;
        case MSG_ORDER_EXECUTED: return LEN_ORDER_EXECUTED;
        case MSG_ORDER_DELETE:   return LEN_ORDER_DELETE;
        default:                 return 0;
    }
}

} // namespace llob

#endif // LLOB_PROTOCOL_ITCH_MESSAGES_HPP
