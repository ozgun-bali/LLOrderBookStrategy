#ifndef LLOB_APP_REPORTER_HPP
#define LLOB_APP_REPORTER_HPP

// Son çıktı. Buradaki her şey timed region'dan sonra çalışıyor, asla içinde değil.

#include "app/order_log.hpp"
#include "app/stats.hpp"

#include <ostream>

namespace llob {

// Assignment'ın istediği field'lar, satır başına bir tane, sabit bir sırada -
// böylece iki run diff ile karşılaştırılabiliyor.
void print_summary(std::ostream& out, const RunSummary& s);

// timestamp,side,price,quantity,reason - makine okur, satır başına bir order.
bool write_order_csv(const char* path, const OrderLog& log);

} // namespace llob

#endif // LLOB_APP_REPORTER_HPP
