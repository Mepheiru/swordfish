# Makefile for Swordfish
# Version 10 Billion or something
# This has been reworked way too many times to count
#
# "make" for dev build
# "make clean" is obviously a thing
# "make rel" for release build (more optimizations)

CC ?= gcc
CFLAGS_DEV ?= -Wall -Wextra -g -std=gnu11
CFLAGS_REL ?= -Wall -Wextra -Werror -O3 -std=gnu11
LDFLAGS ?= 

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

DEV_BUILDDIR = build
REL_BUILDDIR = build/release

SRC = src/main.c src/args.c src/process.c src/hooks.c src/help.c
HEADERS = src/args.h src/process.h src/main.h src/hooks.h src/help.h

OBJ_DEV = $(SRC:src/%.c=$(DEV_BUILDDIR)/%.o)
OBJ_REL = $(SRC:src/%.c=$(REL_BUILDDIR)/%.o)

DOCS = docs/man/help.txt docs/man/general.txt docs/man/signals.txt docs/man/filter.txt docs/man/behavior.txt docs/man/misc.txt docs/man/args.txt

DOC_OBJ_DEV = $(DOCS:docs/%.txt=$(DEV_BUILDDIR)/docs_%.o)
DOC_OBJ_REL = $(DOCS:docs/%.txt=$(REL_BUILDDIR)/docs_%.o)

# Progress tracking
TOTAL = $(shell echo $$(($(words $(SRC)) + $(words $(DOCS)) + 2)))  # +2 for linking and docs
WIDTH = $(shell echo $$((${TOTAL}<10?1:${TOTAL}<100?2:3)))


.PHONY: dev rel clean install uninstall format


dev: $(DEV_BUILDDIR)/swordfish

$(DEV_BUILDDIR):
	@mkdir -p $(DEV_BUILDDIR)

$(DEV_BUILDDIR)/docs_%.o: docs/%.txt
	@mkdir -p $(@D)
	@$(eval COUNT=$(shell echo $$(($(COUNT)+1))))
	@printf "[%*d/%d] Embedding %s (dev)\n" $(WIDTH) $(COUNT) $(TOTAL) $<
	@ld -r -b binary $< -o $@

$(DEV_BUILDDIR)/%.o: src/%.c $(HEADERS) | $(DEV_BUILDDIR)
	@$(eval COUNT=$(shell echo $$(($(COUNT)+1))))
	@printf "[%*d/%d] Compiling %s (dev)\n" $(WIDTH) $(COUNT) $(TOTAL) $<
	@$(CC) $(CFLAGS_DEV) -c $< -o $@

$(DEV_BUILDDIR)/swordfish: $(OBJ_DEV) $(DOC_OBJ_DEV)
	@$(eval COUNT=$(shell echo $$(($(COUNT)+1))))
	@printf "[%*d/%d] Linking %s (dev)\n" $(WIDTH) $(COUNT) $(TOTAL) $@
	@$(CC) $(OBJ_DEV) $(DOC_OBJ_DEV) $(LDFLAGS) -o $@

	@$(eval COUNT=$(shell echo $$(($(COUNT)+1))))
	@printf "[%*d/%d] Generating docs (dev)\n" $(WIDTH) $(COUNT) $(TOTAL)
	@$(DEV_BUILDDIR)/swordfish --man docs/swordfish.1

	@echo "Dev build complete. Binary located in $(DEV_BUILDDIR)/"


rel: $(REL_BUILDDIR)/swordfish

$(REL_BUILDDIR):
	@mkdir -p $(REL_BUILDDIR)

$(REL_BUILDDIR)/docs_%.o: docs/%.txt
	@mkdir -p $(@D)
	@$(eval COUNT=$(shell echo $$(($(COUNT)+1))))
	@printf "[%*d/%d] Embedding %s (release)\n" $(WIDTH) $(COUNT) $(TOTAL) $<
	@ld -r -b binary $< -o $@
	
$(REL_BUILDDIR)/%.o: src/%.c $(HEADERS) | $(REL_BUILDDIR)
	@$(eval COUNT=$(shell echo $$(($(COUNT)+1))))
	@printf "[%*d/%d] Compiling %s (release)\n" $(WIDTH) $(COUNT) $(TOTAL) $<
	@$(CC) $(CFLAGS_REL) -c $< -o $@

$(REL_BUILDDIR)/swordfish: $(OBJ_REL) $(DOC_OBJ_REL)
	@$(eval COUNT=$(shell echo $$(($(COUNT)+1))))
	@printf "[%*d/%d] Linking %s (release)\n" $(WIDTH) $(COUNT) $(TOTAL) $@
	@$(CC) $(OBJ_REL) $(DOC_OBJ_REL) $(LDFLAGS) -o $@

	@$(eval COUNT=$(shell echo $$(($(COUNT)+1))))
	@printf "[%*d/%d] Generating docs (release)\n" $(WIDTH) $(COUNT) $(TOTAL)
	@$(REL_BUILDDIR)/swordfish --man docs/swordfish.1

	@echo "NOTE: Please increment package version in main.h"
	@echo "Release build complete. Binary located in $(REL_BUILDDIR)/"


install: rel
	install -Dm755 $(DEV_BUILDDIR)/swordfish $(DESTDIR)$(BINDIR)/swordfish


uninstall:
	rm -f $(DESTDIR)$(BINDIR)/swordfish


clean:
	rm -rf $(DEV_BUILDDIR) $(REL_BUILDDIR)

format:
	clang-format -i src/*.c
