#ifndef LLOB_SIM_POSITION_HPP
#define LLOB_SIM_POSITION_HPP

// Position and cash flow, integer only.
//
//   BUY  : cash_flow -= quantity * price;  position += quantity;
//   SELL : cash_flow += quantity * price;  position -= quantity;
//
// At P_MARJ_YAYIN_KAPANIS:
//   final_pnl = cash_flow + position * last_market_execution_price
//
// No floating point anywhere. quantity * price overflows 32 bits easily -
// widen before multiplying and document the chosen width and its headroom.
// last_market_execution_price is updated only from the target book.
// Policy when no execution price exists must be documented in the README.

#include "core/types.hpp"

namespace llob {

// TODO

} // namespace llob

#endif // LLOB_SIM_POSITION_HPP
