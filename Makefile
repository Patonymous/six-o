# Make defines CXX=g++ by default; replace that built-in so unset means clang.
ifeq ($(origin CXX),default)
  CXX := clang
endif
CXXFLAGS ?= -std=c++23 -fPIC -Wall -Wextra -O2 -g
CPPFLAGS ?= -Iinclude
LDFLAGS_SO ?= -shared -ldl -lstdc++exp -lstdc++
LDFLAGS_TEST ?= -lstdc++exp -lstdc++ -rdynamic

# clang needs an explicit libstdc++ selection for <stacktrace> / -lstdc++exp.
ifneq ($(filter clang%,$(notdir $(CXX))),)
  CXXFLAGS += -stdlib=libstdc++
endif

BUILD_DIR := build
LIB := $(BUILD_DIR)/six-o.so
SMOKE := $(BUILD_DIR)/tests/smoke
WRAPPER := scripts/six-o

.PHONY: all test clean

all: $(LIB)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/tests:
	mkdir -p $(BUILD_DIR)/tests

$(LIB): src/preload.cpp include/sixo.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@ $(LDFLAGS_SO)

$(SMOKE): tests/smoke.cpp | $(BUILD_DIR)/tests
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@ $(LDFLAGS_TEST)

test: $(LIB) $(SMOKE)
	@set -euo pipefail; \
	tmp=$$(mktemp); \
	trap 'rm -f "$$tmp"' EXIT; \
	$(WRAPPER) --output "$$tmp" $(SMOKE) throw; \
	grep -q 'six-o: __cxa_throw' "$$tmp"; \
	grep -q 'sixo_smoke_throw_site' "$$tmp"; \
	: > "$$tmp"; \
	set +e; \
	$(WRAPPER) --output "$$tmp" $(SMOKE) abort; \
	status=$$?; \
	set -e; \
	test "$$status" -ne 0; \
	grep -q 'six-o: abort' "$$tmp"; \
	grep -q 'sixo_smoke_abort_site' "$$tmp"; \
	echo "OK: throw and abort smoke tests passed"

clean:
	rm -rf $(BUILD_DIR)
