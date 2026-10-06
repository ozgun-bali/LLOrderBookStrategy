#ifndef LLOB_PROTOCOL_ITCH_MESSAGES_HPP
#define LLOB_PROTOCOL_ITCH_MESSAGES_HPP

// ITCH message field offsets and decoded views.
//
// All offsets verified against BISTECH ITCH Protocol Specification (BIST v2112).
// Numeric = unsigned big-endian. Price = SIGNED big-endian 4 bytes.
// Alpha = left justified, space padded, ISO 8859-1, NOT null terminated.
//
// Decoding assumes the caller already checked the length with ByteReader::has().
// The structs are stack locals that -O3 keeps in registers; nothing allocates.

#include "core/types.hpp"
#include "core/byte_reader.hpp"

#include <cstring>

namespace llob {

// ---------------------------------------------------------------- message types

static const uint8_t MSG_SECONDS         = 'T';
static const uint8_t MSG_BOOK_STATE      = 'O';
static const uint8_t MSG_ADD_ORDER       = 'A';
static const uint8_t MSG_ORDER_EXECUTED  = 'E';
static const uint8_t MSG_ORDER_DELETE    = 'D';

// Skipped, but named because the README has to explain what ignoring them costs:
// 'C' also reduces an order's quantity, 'Y' clears the whole book.
static const uint8_t MSG_EXECUTED_PRICE  = 'C';
static const uint8_t MSG_BOOK_FLUSH      = 'Y';

// ---------------------------------------------------------------- fixed lengths

static const std::size_t LEN_SECONDS        = 5;
static const std::size_t LEN_BOOK_STATE     = 29;
static const std::size_t LEN_ADD_ORDER      = 45;
static const std::size_t LEN_ORDER_EXECUTED = 52;
static const std::size_t LEN_ORDER_DELETE   = 18;

// ---------------------------------------------------------------- field offsets

// Every message starts the same way.
static const std::size_t OFF_MSG_TYPE = 0;
static const std::size_t OFF_NANOS    = 1;

// 'T' Seconds
static const std::size_t OFF_SECOND = 1;

// 'O' Order Book State — note the book id sits at 5 here, not 13
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

// ---------------------------------------------------------------- state names

// 20-byte Alpha fields, space padded. Compared with memcmp so the hot path
// never builds a std::string. The trailing NUL of each literal is not compared.
static const char STATE_NAME_SUREKLI_ISLEM[]      = "P_SUREKLI_ISLEM     ";
static const char STATE_NAME_MARJ_YAYIN_KAPANIS[] = "P_MARJ_YAYIN_KAPANIS";

inline BookState book_state_from_name(const uint8_t* p) {
    if (std::memcmp(p, STATE_NAME_SUREKLI_ISLEM, LEN_STATE_NAME) == 0)
        return STATE_SUREKLI_ISLEM;
    if (std::memcmp(p, STATE_NAME_MARJ_YAYIN_KAPANIS, LEN_STATE_NAME) == 0)
        return STATE_MARJ_YAYIN_KAPANIS;
    return STATE_OTHER;
}

// ---------------------------------------------------------------- side

// The wire can carry anything, so an unrecognised byte becomes SIDE_INVALID
// rather than silently indexing a per-side table as if it were a buy.
inline Side side_from_char(uint8_t c) {
    if (c == 'B') return SIDE_BUY;
    if (c == 'S') return SIDE_SELL;
    return SIDE_INVALID;
}

// ---------------------------------------------------------------- decoded views

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
    Quantity quantity;      // a delta to subtract, not a running total
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

// ---------------------------------------------------------------- decoders
//
// The order book id is deliberately not decoded here: the parser reads it
// straight off the buffer and drops non-target messages before any of this runs.

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

// Length a message of this type must have, or 0 when the type is not handled.
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
