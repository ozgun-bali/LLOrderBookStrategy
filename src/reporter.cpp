// Son çıktı. Her timed region'ın dışında çalışıyor.

#include "app/reporter.hpp"

#include <cstdio>
#include <fstream>

namespace llob {

namespace {

// Eksik bir değer sayı olarak değil kelime olarak basılıyor, ki kimse yer
// tutucuyu veri sanmasın. Book'un boş bir side'ının best price'ı hiç yok, ve
// hiç işlem görmemiş bir price'la açık duran bir position'ın dürüst bir P&L'i
// yok.
void printPrice(std::ostream& out, const char* name, bool have, Price p) {
    out << name << " = ";
    if (have) out << p; else out << "none";
    out << "\n";
}

} // namespace

void print_summary(std::ostream& out, const RunSummary& s) {
    out << "final_position          = " << s.position  << "\n";
    out << "cash_flow               = " << s.cash_flow << "\n";

    out << "final_pnl               = ";
    if (s.pnl_known) out << s.final_pnl;
    else             out << "unmarked";       // position açık, hiç işlem olmadı
    out << "\n";

    printPrice(out, "last_execution_price   ",
               s.have_last_execution, s.last_execution_price);
    printPrice(out, "end_of_day_mark_price  ", s.have_mark_price, s.mark_price);

    printPrice(out, "best_bid               ", s.have_best_bid, s.best_bid);
    out << "best_bid_quantity       = "
        << (s.have_best_bid ? s.best_bid_quantity : 0) << "\n";

    printPrice(out, "best_ask               ", s.have_best_ask, s.best_ask);
    out << "best_ask_quantity       = "
        << (s.have_best_ask ? s.best_ask_quantity : 0) << "\n";

    out << "processed_message_count = " << s.processed_message_count << "\n";
    out << "sequence_gap_count      = " << s.sequence_gap_count      << "\n";
    out << "invalid_message_count   = " << s.invalid_message_count   << "\n";
    out << "generated_order_count   = " << s.generated_order_count   << "\n";

    char hex[32];
    std::snprintf(hex, sizeof(hex), "0x%016llX", (unsigned long long)s.checksum);
    out << "checksum                = " << hex << "\n";

    // Yukarıdaki sayıları okumak için bağlam. Zorunlu listenin parçası değil.
    out << "\n";
    out << "packets                 = " << s.packets         << "\n";
    out << "itch_messages           = " << s.itch_messages   << "\n";
    out << "target_messages         = " << s.target_messages << "\n";
    out << "timestamp_groups        = " << s.groups          << "\n";
    out << "simulated_fills         = " << s.fills           << "\n";

    // Evaluation'ların nereye gittiği. Bu listeyi aşağı doğru okumak, günün
    // gerçekten hiçbir şey sunmadığını mı yoksa bir kuralın her şeyi mi geri
    // çevirdiğini söylüyor.
    out << "\n";
    out << "strategy_evaluations    = " << s.evaluations         << "\n";
    out << "  skip_state            = " << s.skip_state          << "\n";
    out << "  skip_sequence         = " << s.skip_sequence       << "\n";
    out << "  skip_one_sided        = " << s.skip_one_sided      << "\n";
    out << "  skip_spread           = " << s.skip_spread         << "\n";
    out << "  skip_not_this_group   = " << s.skip_not_this_group << "\n";
    out << "  skip_flip             = " << s.skip_flip           << "\n";
    out << "  skip_no_direction     = " << s.skip_no_direction   << "\n";
    out << "  skip_duplicate        = " << s.skip_duplicate      << "\n";
    out << "  skip_position_limit   = " << s.skip_position_limit << "\n";
}

bool write_order_csv(const char* path, const OrderLog& log) {
    std::ofstream f(path);
    if (!f) return false;

    f << "timestamp,side,price,quantity,reason\n";
    for (std::size_t i = 0; i < log.size(); ++i) {
        const OrderIntent& o = log[i];
        f << o.timestamp << ','
          << side_text(o.side) << ','
          << o.price << ','
          << o.quantity << ','
          << reason_text(o.reason) << '\n';
    }
    return f.good();
}

} // namespace llob
