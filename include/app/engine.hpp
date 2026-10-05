#ifndef LLOB_APP_ENGINE_HPP
#define LLOB_APP_ENGINE_HPP

// Wires the pipeline and owns timestamp grouping.
//
//   datagram -> MoldUDP64 -> ITCH parser -> OrderBook
//                                        -> [group boundary] -> Strategy
//                                        -> FillSimulator -> Position
//
// Grouping: apply every message sharing a timestamp, and only when the
// timestamp changes treat the previous group as complete and evaluate the
// strategy once. The final group at end of file must also be evaluated -
// that is what finalize() is for.
//
// Entry point, per the assignment:
//   void process_datagram(const uint8_t* data, std::size_t length);
//
// This is the hot path. After construction it must not allocate.

#include "core/types.hpp"

#include <cstddef>

namespace llob {

// TODO

} // namespace llob

#endif // LLOB_APP_ENGINE_HPP
