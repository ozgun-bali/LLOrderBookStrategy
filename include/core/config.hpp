#ifndef LLOB_CORE_CONFIG_HPP
#define LLOB_CORE_CONFIG_HPP

// Runtime configuration. Loaded once at startup - cold path, allocation is fine
// here. Every capacity the hot path relies on is fixed from this struct.
//
// Fields required by the assignment:
//   order_quantity, minimum_position, maximum_position, tick_size,
//   target_order_book_id, max_active_orders, max_price_levels
//
// Implementation lives in src/config.cpp.

#include "core/types.hpp"

namespace llob {

// TODO

} // namespace llob

#endif // LLOB_CORE_CONFIG_HPP
