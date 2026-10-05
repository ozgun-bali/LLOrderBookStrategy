# Statech - Low Latency Order Book and Strategy
#
#   make            release build  -> ./orderbook
#   make debug      debug build    -> ./orderbook-debug
#   make test       build tests    -> ./run_tests
#   make check      build and run tests
#   make clean

CXX       ?= g++
STD        = -std=c++11
WARN       = -Wall -Wextra -Wpedantic
INC        = -Iinclude

RELFLAGS   = -O3 -DNDEBUG
DBGFLAGS   = -O0 -g

SRCDIR     = src
TESTDIR    = tests
BUILDDIR   = build

BIN        = orderbook
DBGBIN     = orderbook-debug
TESTBIN    = run_tests

SRCS       = $(wildcard $(SRCDIR)/*.cpp)
LIBSRCS    = $(filter-out $(SRCDIR)/main.cpp,$(SRCS))
TESTSRCS   = $(wildcard $(TESTDIR)/*.cpp)

RELOBJS    = $(SRCS:$(SRCDIR)/%.cpp=$(BUILDDIR)/release/%.o)
DBGOBJS    = $(SRCS:$(SRCDIR)/%.cpp=$(BUILDDIR)/debug/%.o)
TESTOBJS   = $(TESTSRCS:$(TESTDIR)/%.cpp=$(BUILDDIR)/test/%.o) \
             $(LIBSRCS:$(SRCDIR)/%.cpp=$(BUILDDIR)/test/lib_%.o)

DEPS       = $(RELOBJS:.o=.d) $(DBGOBJS:.o=.d) $(TESTOBJS:.o=.d)

.PHONY: all debug test check clean

all: $(BIN)

$(BIN): $(RELOBJS)
	$(CXX) $(STD) $(RELFLAGS) $^ -o $@

$(DBGBIN): $(DBGOBJS)
	$(CXX) $(STD) $(DBGFLAGS) $^ -o $@

$(TESTBIN): $(TESTOBJS)
	$(CXX) $(STD) $(DBGFLAGS) $^ -o $@

debug: $(DBGBIN)

test: $(TESTBIN)

check: $(TESTBIN)
	./$(TESTBIN)

$(BUILDDIR)/release/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(STD) $(WARN) $(INC) $(RELFLAGS) -MMD -MP -c $< -o $@

$(BUILDDIR)/debug/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(STD) $(WARN) $(INC) $(DBGFLAGS) -MMD -MP -c $< -o $@

$(BUILDDIR)/test/lib_%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(STD) $(WARN) $(INC) $(DBGFLAGS) -MMD -MP -c $< -o $@

$(BUILDDIR)/test/%.o: $(TESTDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(STD) $(WARN) $(INC) -I$(TESTDIR) $(DBGFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -rf $(BUILDDIR) $(BIN) $(DBGBIN) $(TESTBIN)

-include $(DEPS)
