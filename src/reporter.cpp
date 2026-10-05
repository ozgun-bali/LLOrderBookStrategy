// Final output. Runs outside every timed region.
//
// Deterministic summary block:
//   final_position, cash_flow, final_pnl, last_execution_price,
//   best_bid / best_bid_quantity, best_ask / best_ask_quantity,
//   processed_message_count, sequence_gap_count, invalid_message_count,
//   generated_order_count, checksum
//
// Plus the machine readable order log:
//   timestamp,side,price,quantity,reason

#include "app/stats.hpp"

// TODO
