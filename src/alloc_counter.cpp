// Replaces the global allocation operators so every heap request is counted.
//
// These are the standard replaceable functions: defining them here makes the
// linker use ours instead of the library's, for the whole program and every
// library it pulls in. There is no way for an allocation to go around them.
//
// The counting itself is one increment of a plain size_t. What the override
// costs beyond that is nothing: libstdc++'s own operator new is a malloc call
// with a throw on failure, which is exactly what is below.

#include "app/alloc_counter.hpp"

#include <cstdlib>
#include <new>

namespace llob {
std::size_t g_alloc_count = 0;
std::size_t g_alloc_bytes = 0;
}

void* operator new(std::size_t n) {
    ++llob::g_alloc_count;
    llob::g_alloc_bytes += n;

    // malloc(0) is allowed to return null, which here would look like failure.
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}

void* operator new[](std::size_t n) { return operator new(n); }

void* operator new(std::size_t n, const std::nothrow_t&) throw() {
    ++llob::g_alloc_count;
    llob::g_alloc_bytes += n;
    return std::malloc(n ? n : 1);
}

void* operator new[](std::size_t n, const std::nothrow_t& t) throw() {
    return operator new(n, t);
}

// Freeing is not counted: the question is how often the program asks for
// memory on the hot path, and a free is not an ask.
void operator delete(void* p) throw() { std::free(p); }
void operator delete[](void* p) throw() { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) throw() { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) throw() { std::free(p); }
