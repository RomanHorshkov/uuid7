CC       ?= gcc
AR       ?= ar
ARFLAGS  ?= rcs

INCDIR    := include
SRCDIR    := src
BUILDDIR  := build
LIB       := $(BUILDDIR)/libuuid7.a

# Pick up all C sources under src/
SRCS := $(wildcard $(SRCDIR)/*.c)
OBJS := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))

CFLAGS   ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS ?= -I$(INCDIR) -D_GNU_SOURCE

.PHONY: all clean help

all: $(LIB) ## Build static library at build/libuuid7.a

$(LIB): $(OBJS)
	@echo "[ar] $@"
	$(AR) $(ARFLAGS) $@ $^
	@# keep build/ clean: remove intermediate objects so only .a remains
	@rm -f $(OBJS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILDDIR):
	@mkdir -p $@

clean: ## Remove build directory completely
	rm -rf $(BUILDDIR)

help: ## Show targets
	@echo "Targets:"
	@echo "  all    Build static library at $(LIB)"
	@echo "  clean  Remove build directory"
