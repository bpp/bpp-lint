CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -O2
LDFLAGS ?=
# libm is bundled with libc on macOS, but glibc requires an explicit -lm
# for sqrt / log etc. used in priors.c.
LDLIBS  ?= -lm

# EXTRA_CFLAGS / EXTRA_LDFLAGS are appended after the defaults. Use these
# for additive flags (e.g. `-arch x86_64` to cross-compile macOS Intel on
# an arm64 host) without having to restate the full CFLAGS.
CFLAGS  += $(EXTRA_CFLAGS)
LDFLAGS += $(EXTRA_LDFLAGS)

SRCDIR  := src
OBJDIR  := build
BIN     := bpp-lint

# The keyword table (src/keywords_gen.c) is generated from the canonical
# spec/bpp-syntax.json. It is committed, so the ordinary build needs no Python;
# regenerate with `make gen`, verify it is in sync with `make check-gen`.
SPEC    := spec/bpp-syntax.json
GEN     := spec/gen_keywords_c.py
GEN_C   := $(SRCDIR)/keywords_gen.c
PYTHON  ?= python3

SRCS := $(wildcard $(SRCDIR)/*.c)
OBJS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))

.PHONY: all clean test install gen check-gen

all: $(BIN)

# Regenerate the keyword table from the spec (run after editing spec/enrich.json
# or re-running spec/generate.py).
gen: $(GEN) $(SPEC)
	$(PYTHON) $(GEN)

# Verify the committed table matches what the spec would generate (CI guard).
check-gen: $(GEN) $(SPEC) | $(OBJDIR)
	@$(PYTHON) $(GEN) --out $(OBJDIR)/keywords_gen.check.c >/dev/null
	@if diff -q $(GEN_C) $(OBJDIR)/keywords_gen.check.c >/dev/null; then \
	  echo "keywords_gen.c is in sync with $(SPEC)"; \
	else \
	  echo "ERROR: $(GEN_C) is stale relative to $(SPEC); run 'make gen'"; \
	  diff -u $(GEN_C) $(OBJDIR)/keywords_gen.check.c; exit 1; \
	fi

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(BIN)

test: $(BIN) check-gen
	@./tests/run.sh

install: $(BIN)
	install -m 0755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)
