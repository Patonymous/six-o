CXXFLAGS ?= -std=c++23 -fPIC -Wall -Wextra -O2 -g -fno-omit-frame-pointer
CPPFLAGS ?= -Iinclude
LDFLAGS_SO ?= -shared -ldl
LDFLAGS_BIN ?=
LDFLAGS_TEST ?= -rdynamic

BUILD_DIR := build
LIB := $(BUILD_DIR)/six-o.so
BIN := $(BUILD_DIR)/six-o
SMOKE := $(BUILD_DIR)/tests/smoke

PRODUCER_SRCS := src/producer.cpp src/signals.cpp
CONSUMER_SRCS := src/consumer.cpp src/symbolize.cpp src/signals.cpp

.PHONY: all test clean

all: $(LIB) $(BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/tests:
	mkdir -p $(BUILD_DIR)/tests

$(LIB): $(PRODUCER_SRCS) include/protocol.hpp include/signals.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(PRODUCER_SRCS) -o $@ $(LDFLAGS_SO)

$(BIN): $(CONSUMER_SRCS) include/protocol.hpp include/signals.hpp include/symbolize.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(CONSUMER_SRCS) -o $@ $(LDFLAGS_BIN)

$(SMOKE): tests/smoke.cpp | $(BUILD_DIR)/tests
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@ $(LDFLAGS_TEST)

test: $(LIB) $(BIN) $(SMOKE)
	@set -euo pipefail; \
	tmp=$$(mktemp); \
	trap 'rm -f "$$tmp"' EXIT; \
	$(BIN) --output "$$tmp" $(SMOKE) throw; \
	grep -q 'six-o: __cxa_throw' "$$tmp"; \
	grep -q 'sixo_smoke_throw_site' "$$tmp"; \
	: > "$$tmp"; \
	set +e; \
	$(BIN) --output "$$tmp" $(SMOKE) abort; \
	status=$$?; \
	set -e; \
	test "$$status" -ne 0; \
	grep -q 'six-o: SIGABRT' "$$tmp"; \
	grep -q 'sixo_smoke_abort_site' "$$tmp"; \
	: > "$$tmp"; \
	set +e; \
	$(BIN) --output "$$tmp" $(SMOKE) segfault; \
	status=$$?; \
	set -e; \
	test "$$status" -ne 0; \
	grep -q 'six-o: SIGSEGV' "$$tmp"; \
	grep -q 'sixo_smoke_segfault_site' "$$tmp"; \
	echo "OK: throw, abort, and segfault smoke tests passed"

clean:
	rm -rf $(BUILD_DIR)
