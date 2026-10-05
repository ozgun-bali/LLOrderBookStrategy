#ifndef LLOB_SIM_FILL_SIMULATOR_HPP
#define LLOB_SIM_FILL_SIMULATOR_HPP

// Turns an OrderIntent into a fill.
//
// For this assignment every generated order is assumed fully filled at its own
// price and quantity at the end of the timestamp it was generated in.
//
// Public ITCH execution messages are NOT our fills - they belong to other
// participants and must never feed the position.
//
// Kept separate from the strategy on purpose: the assignment calls out that
// this model should later support async ack, partial fill and cancel.

#include "core/types.hpp"

namespace llob {

// TODO

} // namespace llob

#endif // LLOB_SIM_FILL_SIMULATOR_HPP
