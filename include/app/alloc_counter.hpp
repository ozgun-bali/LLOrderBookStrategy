#ifndef LLOB_APP_ALLOC_COUNTER_HPP
#define LLOB_APP_ALLOC_COUNTER_HPP

// Counts every heap allocation the program makes.
//
// The global operator new is replaced in alloc_counter.cpp, so this sees all
// of them: every new, every vector that grows, every string that is built.
// Nothing can hide from it.
//
// The point is to turn a claim into a measurement. "No allocation on the hot
// path" is an assertion; a counter that reads zero after the run is evidence,
// and evidence is what the assignment asks to be reported.
//
// It matters because allocation is the single worst source of unpredictable
// latency. malloc may find a free block and return in twenty nanoseconds, or
// walk its free lists, or go to the kernel for more pages and take
// microseconds. The average hides it; the tail does not.

#include <cstddef>

namespace llob {

// Single threaded, so a plain counter is enough and costs an increment.
extern std::size_t g_alloc_count;
extern std::size_t g_alloc_bytes;

inline void alloc_reset() { g_alloc_count = 0; g_alloc_bytes = 0; }
inline std::size_t alloc_count() { return g_alloc_count; }
inline std::size_t alloc_bytes() { return g_alloc_bytes; }

} // namespace llob

#endif // LLOB_APP_ALLOC_COUNTER_HPP
