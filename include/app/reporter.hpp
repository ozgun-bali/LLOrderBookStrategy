#ifndef LLOB_APP_REPORTER_HPP
#define LLOB_APP_REPORTER_HPP

// Final output. Everything here runs after the timed region, never inside it.

#include "app/order_log.hpp"
#include "app/stats.hpp"

#include <ostream>

namespace llob {

// The fields the assignment asks for, one per line, in a fixed order so two
// runs can be compared with diff.
void print_summary(std::ostream& out, const RunSummary& s);

// timestamp,side,price,quantity,reason - machine readable, one order per line.
bool write_order_csv(const char* path, const OrderLog& log);

} // namespace llob

#endif // LLOB_APP_REPORTER_HPP
