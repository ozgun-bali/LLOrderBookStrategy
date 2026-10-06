// Config file parsing. Startup only, cold path.
//
// Format is one `key = value` per line. Blank lines and lines starting with '#'
// are ignored. Unknown keys, bad numbers and missing keys all fail loudly rather
// than defaulting, because a wrong capacity only shows up much later and looks
// like something else.

#include "core/config.hpp"

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace llob {

namespace {

std::string trim(const std::string& s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
    return s.substr(b, e - b);
}

// strtoll reports two different failures and both matter here: text that is not
// a number at all, and a number too large for the type.
bool parseInt(const std::string& s, long long& out) {
    if (s.empty()) return false;

    errno = 0;
    char* end = 0;
    const long long v = std::strtoll(s.c_str(), &end, 10);

    if (end == s.c_str()) return false;   // no digits were read
    if (*end != '\0')     return false;   // trailing junk, e.g. "100abc"
    if (errno == ERANGE)  return false;   // outside long long

    out = v;
    return true;
}

std::string atLine(std::size_t line, const std::string& msg) {
    std::ostringstream os;
    os << "config line " << line << ": " << msg;
    return os.str();
}

} // namespace

bool parse_config(const std::string& text, Config& out, std::string& error) {
    Config cfg;

    bool seenTarget = false, seenTick = false, seenQty = false;
    bool seenMin = false, seenMax = false;
    bool seenOrders = false, seenLevels = false;

    std::istringstream in(text);
    std::string line;
    std::size_t lineNo = 0;

    while (std::getline(in, line)) {
        ++lineNo;
        const std::string s = trim(line);
        if (s.empty() || s[0] == '#') continue;

        const std::size_t eq = s.find('=');
        if (eq == std::string::npos) {
            error = atLine(lineNo, "expected key = value");
            return false;
        }

        const std::string key = trim(s.substr(0, eq));
        const std::string val = trim(s.substr(eq + 1));

        long long v = 0;
        if (!parseInt(val, v)) {
            error = atLine(lineNo, "'" + val + "' is not a whole number");
            return false;
        }

        if (key == "target_order_book_id") {
            if (v < 0 || v > 0xFFFFFFFFLL) {
                error = atLine(lineNo, "target_order_book_id is out of range");
                return false;
            }
            cfg.target_order_book_id = (OrderBookId)v;
            seenTarget = true;

        } else if (key == "tick_size") {
            if (v <= 0 || v > 0x7FFFFFFFLL) {
                error = atLine(lineNo, "tick_size must be positive");
                return false;
            }
            cfg.tick_size = (Price)v;
            seenTick = true;

        } else if (key == "order_quantity") {
            if (v <= 0) {
                error = atLine(lineNo, "order_quantity must be positive");
                return false;
            }
            cfg.order_quantity = (Quantity)v;
            seenQty = true;

        } else if (key == "minimum_position") {
            cfg.minimum_position = (Position)v;     // may be negative, that is the point
            seenMin = true;

        } else if (key == "maximum_position") {
            cfg.maximum_position = (Position)v;
            seenMax = true;

        } else if (key == "max_active_orders") {
            if (v <= 0) {
                error = atLine(lineNo, "max_active_orders must be positive");
                return false;
            }
            cfg.max_active_orders = (std::size_t)v;
            seenOrders = true;

        } else if (key == "max_price_levels") {
            if (v <= 0) {
                error = atLine(lineNo, "max_price_levels must be positive");
                return false;
            }
            cfg.max_price_levels = (std::size_t)v;
            seenLevels = true;

        } else {
            error = atLine(lineNo, "unknown key '" + key + "'");
            return false;
        }
    }

    const char* missing = 0;
    if      (!seenTarget) missing = "target_order_book_id";
    else if (!seenTick)   missing = "tick_size";
    else if (!seenQty)    missing = "order_quantity";
    else if (!seenMin)    missing = "minimum_position";
    else if (!seenMax)    missing = "maximum_position";
    else if (!seenOrders) missing = "max_active_orders";
    else if (!seenLevels) missing = "max_price_levels";

    if (missing) {
        error = std::string("config: missing required key '") + missing + "'";
        return false;
    }

    if (cfg.minimum_position > cfg.maximum_position) {
        error = "config: minimum_position is above maximum_position";
        return false;
    }

    out = cfg;
    return true;
}

bool load_config(const char* path, Config& out, std::string& error) {
    std::ifstream f(path);
    if (!f) {
        error = std::string("config: cannot open '") + path + "'";
        return false;
    }

    std::ostringstream buf;
    buf << f.rdbuf();
    return parse_config(buf.str(), out, error);
}

} // namespace llob
