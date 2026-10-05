#ifndef LLOB_STRATEGY_ORDER_INTENT_HPP
#define LLOB_STRATEGY_ORDER_INTENT_HPP

// The strategy's output type and the sink it writes to.
//
//   struct OrderIntent {
//       uint64_t timestamp;
//       Side     side;
//       int32_t  price;
//       uint64_t quantity;
//   };
//
// The sink is an interface so the fill model can be swapped later for async
// acknowledgement, partial fills and cancels without touching the strategy.
// The CSV line (timestamp,side,price,quantity,reason) is written outside the
// timed region - the reason tag travels with the intent, it is not formatted
// on the hot path.

#include "core/types.hpp"

namespace llob {

// TODO

} // namespace llob

#endif // LLOB_STRATEGY_ORDER_INTENT_HPP
