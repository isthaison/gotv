CC ?= cc
AR ?= ar
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2 -g
CFLAGS += -std=gnu11 -Wall -Wextra -Wno-unused-parameter
CPPFLAGS += -Iengine $(shell $(PKG_CONFIG) --cflags gio-2.0)
BUILD_DIR ?= build
# Shared file list + version (see mk/common.mk). Linux keeps a wildcard for
# CORE_SRC so new engine/*.c files never need a Makefile edit; VERSION
# defaults to the single source in ./VERSION.
include mk/common.mk
VERSION ?= $(GTV_VERSION)-1
# Macro/emoji tables for tests (and dev runs without install).
export GTV_DATA_DIR ?= $(abspath data)
CORE_SRC := $(wildcard engine/*.c)
CORE_OBJ := $(patsubst engine/%.c,$(BUILD_DIR)/engine/%.o,$(CORE_SRC))
CORE_LIB := $(BUILD_DIR)/libgotv.a
CORE_LIBS := $(shell $(PKG_CONFIG) --libs gio-2.0) -lm
BINS := $(BUILD_DIR)/ibus-engine-gotv $(BUILD_DIR)/ibus-setup-gotv $(BUILD_DIR)/gotv-demo

.PHONY: all build test vet install clean package help
all: build
build: $(BINS)
$(BUILD_DIR)/engine/%.o: engine/%.c engine/engine.h engine/internal.h
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@
$(CORE_LIB): $(CORE_OBJ)
	$(AR) rcs $@ $^
$(BUILD_DIR)/ibus-engine-gotv: linux/ibus/engine.c $(CORE_LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE_LIB) $(LDFLAGS) $(shell $(PKG_CONFIG) --cflags --libs ibus-1.0) $(CORE_LIBS) -o $@
$(BUILD_DIR)/ibus-setup-gotv: linux/setup/main.c linux/setup/data.c $(CORE_LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) linux/setup/main.c linux/setup/data.c $(CORE_LIB) $(LDFLAGS) $(shell $(PKG_CONFIG) --cflags --libs gtk+-3.0 ayatana-appindicator3-0.1) $(CORE_LIBS) -o $@
$(BUILD_DIR)/gotv-demo: tools/demo/main.c $(CORE_LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE_LIB) $(LDFLAGS) $(CORE_LIBS) -o $@
$(BUILD_DIR)/test-engine: tests/test_engine.c $(CORE_LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE_LIB) $(LDFLAGS) $(CORE_LIBS) -o $@
$(BUILD_DIR)/test-support: tests/test_support.c $(CORE_LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE_LIB) $(LDFLAGS) $(CORE_LIBS) -o $@
$(BUILD_DIR)/test-verdir: tests/test_verdir.c $(CORE_LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE_LIB) $(LDFLAGS) $(CORE_LIBS) -o $@
test: $(BUILD_DIR)/test-engine $(BUILD_DIR)/test-support $(BUILD_DIR)/test-verdir
	@for t in $(BUILD_DIR)/test-engine $(BUILD_DIR)/test-support $(BUILD_DIR)/test-verdir; do \
		if [ -x "$$t" ]; then "$$t"; else /lib64/ld-linux-x86-64.so.2 "$$t"; fi || exit 1; \
	done
vet:
	$(MAKE) build test CFLAGS='-O2 -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Werror' BUILD_DIR=$(BUILD_DIR)/strict
install: build
	sudo bash ./gtv.sh install
package: build
	bash ./gtv.sh package "$(VERSION)"
clean:
	bash ./gtv.sh clean
help:
	@echo 'make build | test | vet | install | package VERSION=... | clean | bump V=...'
	@echo '  (shell entry point: ./gtv.sh <build|test|vet|clean|install|package|bump|help>)'
.PHONY: bump
bump:
	bash ./gtv.sh bump "$(V)" "$(NOTES)"
-include $(CORE_OBJ:.o=.d)

$(BUILD_DIR)/test-setup: tests/test_setup.c linux/setup/main.c linux/setup/data.c $(CORE_LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_setup.c linux/setup/data.c $(CORE_LIB) $(LDFLAGS) $(shell $(PKG_CONFIG) --cflags --libs gtk+-3.0 ayatana-appindicator3-0.1) $(CORE_LIBS) -o $@
.PHONY: test-ui
test-ui: $(BUILD_DIR)/test-setup
	$(BUILD_DIR)/test-setup

$(BUILD_DIR)/test-ibus: tests/test_ibus.c linux/ibus/engine.c $(CORE_LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(CORE_LIB) $(LDFLAGS) $(shell $(PKG_CONFIG) --cflags --libs ibus-1.0) $(CORE_LIBS) -o $@
.PHONY: test-ibus
test-ibus: $(BUILD_DIR)/test-ibus
	$(BUILD_DIR)/test-ibus
