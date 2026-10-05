#ifndef LLOB_APP_STATS_HPP
#define LLOB_APP_STATS_HPP

// Counters and the determinism checksum.
//
// Required counters: processed_message_count, sequence_gap_count,
// invalid_message_count, generated_order_count, plus packet count, total ITCH
// messages, target messages and order intents for the benchmark report.
//
// The checksum must fold in the result state so that identical input and config
// always produce the same value, and so the optimizer cannot delete the work in
// benchmark mode. Define it, keep it cheap, and document it in the README.

#include "core/types.hpp"

namespace llob {

// TODO

} // namespace llob

#endif // LLOB_APP_STATS_HPP
