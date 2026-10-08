#ifndef LLOB_APP_ORDER_LOG_HPP
#define LLOB_APP_ORDER_LOG_HPP

// Keeps every order intent the strategy produced, for the CSV and the checksum.
//
// Writing the CSV as orders appear would put a system call on the hot path and
// swamp the latency being measured, so the lines are formatted at the end from
// what is kept here.
//
// Capacity is reserved up front. The sample feed's target book produces a few
// hundred intents in a day, so the reserve is orders of magnitude clear of it
// and push_back never reallocates in practice. If a feed ever did exceed it the
// vector would grow once, which is why the overflow is counted rather than
// hidden.

#include "strategy/order_intent.hpp"

#include <vector>

namespace llob {

class OrderLog {
public:
    static const std::size_t DEFAULT_RESERVE = 1u << 16;   // 65,536

    explicit OrderLog(std::size_t reserve = DEFAULT_RESERVE)
        : reserved_(reserve), grew_(false) {
        intents_.reserve(reserve);
    }

    void add(const OrderIntent& intent) {
        if (intents_.size() == reserved_) grew_ = true;
        intents_.push_back(intent);
    }

    std::size_t size() const { return intents_.size(); }
    bool        grew() const { return grew_; }
    const OrderIntent& operator[](std::size_t i) const { return intents_[i]; }

private:
    std::vector<OrderIntent> intents_;
    std::size_t              reserved_;
    bool                     grew_;
};

} // namespace llob

#endif // LLOB_APP_ORDER_LOG_HPP
