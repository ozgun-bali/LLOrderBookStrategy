// Risk and P&L tests.
//
// Required: quantity reduction near the limit, no order generated at the limit,
// BUY cash flow, SELL cash flow, end of day mark to market.
//
// Worth adding: no order after the end of day state, mark to market when no
// execution price was ever seen, and a quantity * price product large enough to
// overflow 32 bits.

#include "test_framework.hpp"

// TODO
