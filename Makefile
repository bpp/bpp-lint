CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -O2
LDFLAGS ?=

# EXTRA_CFLAGS / EXTRA_LDFLAGS are appended after the defaults. Use these
# for additive flags (e.g. `-arch x86_64` to cross-compile macOS Intel on
# an arm64 host) without having to restate the full CFLAGS.
CFLAGS  += $(EXTRA_CFLAGS)
LDFLAGS += $(EXTRA_LDFLAGS)

SRCDIR  := src
OBJDIR  := build
BIN     := bpp-lint

SRCS := $(wildcard $(SRCDIR)/*.c)
OBJS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))

.PHONY: all clean test install

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(BIN)

test: $(BIN)
	@./tests/run.sh

install: $(BIN)
	install -m 0755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)
