// Order book tests.
//
// Required: single and multiple add, partial execution, full execution, delete,
// removal of the best level, duplicate order id, unknown order id.
//
// Worth adding: the same order id active on both sides at once (legal in ITCH),
// a fully filled order that never receives a Delete, market order price
// sentinel, zero quantity (undisclosed) add, and capacity overflow producing a
// safe error rather than corruption.

#include "test_framework.hpp"

// TODO
