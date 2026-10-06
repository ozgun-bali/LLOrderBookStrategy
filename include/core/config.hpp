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

struct Config {
    OrderBookId target_order_book_id;
    Price       tick_size;

    Quantity    order_quantity;
    Position    minimum_position;
    Position    maximum_position;

    std::size_t max_active_orders;
    std::size_t max_price_levels;

    Config()
        : target_order_book_id(0), tick_size(0), order_quantity(0),
          minimum_position(0), maximum_position(0),
          max_active_orders(0), max_price_levels(0) {}
};

// Returns false and fills `error` with a message naming the line and the problem.
// No exceptions: the rest of the project treats failures as return values.
bool load_config(const char* path, Config& out, std::string& error);

// Same checks without the file, so tests can feed text directly.
bool parse_config(const std::string& text, Config& out, std::string& error);

} // namespace llob

#endif // LLOB_CORE_CONFIG_HPP
