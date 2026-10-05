// Config file parsing. Startup only, cold path.
//
// Simple key = value lines, '#' comments. Unknown keys and malformed values
// should fail loudly here rather than silently defaulting - a wrong capacity
// surfaces as a runtime error much later.

#include "core/config.hpp"

// TODO
