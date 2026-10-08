#ifndef LLOB_CORE_CONFIG_HPP
#define LLOB_CORE_CONFIG_HPP

// Runtime configuration. Loaded once at startup - cold path, so std::string and
// file I/O are fine here. Every capacity the hot path relies on is fixed from
// this struct and never grows afterwards.
//
// Every key is required. A missing or misspelled key is an error, not a silent
// default: a wrong capacity would otherwise surface much later as a confusing
// runtime failure.

#include "core/types.hpp"

#include <string>

namespace llob {

// A capacity given as -1 means the configuration imposes no limit. A fixed
// capacity design still has to allocate something, so these are what it picks:
// sized from the sample feed, whose target book peaked at 4,528 live orders
// across a 3,780 tick band, with more than ten times that as headroom.
static const std::size_t DEFAULT_MAX_ACTIVE_ORDERS = 1u << 16;   // 65,536
static const std::size_t DEFAULT_MAX_PRICE_LEVELS  = 1u << 14;   // 16,384

struct Config {
    OrderBookId target_order_book_id;
    Price       tick_size;

    Quantity    order_quantity;
    Position    minimum_position;
    Position    maximum_position;

    std::size_t max_active_orders;
    std::size_t max_price_levels;

    // False when the config said -1. The capacity is still finite, but
    // exceeding it is a configuration-independent failure rather than the
    // configured limit being reached.
    bool orders_limited;
    bool levels_limited;

    Config()
        : target_order_book_id(0), tick_size(0), order_quantity(0),
          minimum_position(0), maximum_position(0),
          max_active_orders(DEFAULT_MAX_ACTIVE_ORDERS),
          max_price_levels(DEFAULT_MAX_PRICE_LEVELS),
          orders_limited(true), levels_limited(true) {}
};

// Returns false and fills `error` with a message naming the line and the problem.
// No exceptions: the rest of the project treats failures as return values.
bool load_config(const char* path, Config& out, std::string& error);

// Same checks without the file, so tests can feed text directly.
bool parse_config(const std::string& text, Config& out, std::string& error);

} // namespace llob

#endif // LLOB_CORE_CONFIG_HPP
