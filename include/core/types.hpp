#ifndef LLOB_CORE_TYPES_HPP
#define LLOB_CORE_TYPES_HPP

// Shared scalar types and enums. No logic, no includes beyond <cstdint>.
// Pinning these early keeps fixed-point discipline: no float anywhere.

#include <cstdint>

namespace llob {

typedef int32_t  Price;        // kurus, SIGNED (ITCH price fields are signed)
typedef uint64_t Quantity;
typedef uint64_t OrderId;      // unique per (order book, side) only
typedef uint32_t OrderBookId;
typedef uint64_t Timestamp;    // seconds * 1e9 + nanoseconds, composed from 'T' + msg

enum Side {
    SIDE_BUY = 0,
    SIDE_SELL = 1,
    SIDE_COUNT = 2,
    SIDE_INVALID = 3
};

// ITCH data types: bit 31 set with all other bits zero means "no price",
// and in Add Order it means a market order.
static const Price PRICE_NOT_AVAILABLE = INT32_MIN;

enum BookState {
    STATE_UNKNOWN = 0,
    STATE_SUREKLI_ISLEM,       // P_SUREKLI_ISLEM  - strategy may run
    STATE_MARJ_YAYIN_KAPANIS,  // P_MARJ_YAYIN_KAPANIS - end of day, mark to market
    STATE_OTHER
};

} // namespace llob

#endif // LLOB_CORE_TYPES_HPP
