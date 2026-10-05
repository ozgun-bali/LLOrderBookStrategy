// Strategy tests.
//
// Required: a transient gap inside one timestamp group must not signal, a real
// BUY gap, a real SELL gap, repeated signal suppression.
//
// Worth adding: a price flipping bid<->ask inside one group, a gap that existed
// before the group rather than being created by it, a gap while the book state
// is not P_SUREKLI_ISLEM, a gap while the sequence stream is unreliable, and a
// two tick gap (must not signal).

#include "test_framework.hpp"

// TODO
