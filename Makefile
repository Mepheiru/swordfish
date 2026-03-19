# Makefile for Swordfish
# Version 10 Billion or something +1
# This has been reworked way too many times to count
#
# "make" for dev build
# "make clean" is obviously a thing
# "make rel" for release build (more optimizations)
# "make test" to run the test suite

CC ?= gcc
CFLAGS_DEV ?= -Wall -Wextra -g -std=gnu11
CFLAGS_REL ?= -Wall -Wextra -Werror -O3 -std=gnu11
LDFLAGS ?= -lncurses

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

DEV_BUILDDIR  = build
REL_BUILDDIR  = build/release
TEST_BUILDDIR = build/test

SRC     = src/main.c src/args.c src/process.c src/hooks.c src/help.c src/tui.c src/fuzzy.c src/theme.c
HEADERS = src/args.h src/process.h src/main.h src/hooks.h src/help.h src/tui.h src/fuzzy.h src/theme.h

OBJ_DEV = $(SRC:src/%.c=$(DEV_BUILDDIR)/%.o)
OBJ_REL = $(SRC:src/%.c=$(REL_BUILDDIR)/%.o)

DOCS = docs/man/help.txt docs/man/general.txt docs/man/signals.txt docs/man/filter.txt docs/man/behavior.txt docs/man/misc.txt docs/man/args.txt docs/man/perf.txt

DOC_OBJ_DEV = $(DOCS:docs/%.txt=$(DEV_BUILDDIR)/docs_%.o)
DOC_OBJ_REL = $(DOCS:docs/%.txt=$(REL_BUILDDIR)/docs_%.o)

THEMES = $(wildcard themes/*.swt)

THEME_OBJ_DEV = $(THEMES:themes/%.swt=$(DEV_BUILDDIR)/themes_%.o)
THEME_OBJ_REL = $(THEMES:themes/%.swt=$(REL_BUILDDIR)/themes_%.o)

TEST_SRC     = tests/main.c tests/test_fuzzy.c tests/test_args.c
TEST_OBJ     = $(TEST_SRC:tests/%.c=$(TEST_BUILDDIR)/%.o)
# all dev objects except main.o — the test runner supplies its own main
TEST_OBJ_SRC = $(filter-out $(DEV_BUILDDIR)/main.o, $(OBJ_DEV))

# Progress tracking — +2 for linking and doc generation, +1 for rivulet.png
TOTAL = $(shell echo $$(($(words $(SRC)) + $(words $(DOCS)) + $(words $(THEMES)) + 1 + 2)))
WIDTH = $(shell echo $$((${TOTAL}<10?1:${TOTAL}<100?2:3)))


.PHONY: dev rel test clean install uninstall format


dev: $(DEV_BUILDDIR)/swordfish

$(DEV_BUILDDIR):
	@mkdir -p $(DEV_BUILDDIR)

$(DEV_BUILDDIR)/docs_%.o: docs/%.txt
	@mkdir -p $(@D)
	@$(eval COUNT=$(shell echo $$(($(COUNT)+1))))
	@printf "[%*d/%d] Embedding %s (dev)\n" $(WIDTH) $(COUNT) $(TOTAL) $<
	@ld -r -b binary $< -o $@

$(DEV_BUILDDIR)/themes_%.o: themes/%.swt | $(DEV_BUILDDIR)
	@mkdir -p $(@D)
	@$(eval COUNT=$(shell echo $$(($(COUNT)+1))))
	@printf "[%*d/%d] Embedding %s (dev)\n" $(WIDTH) $(COUNT) $(TOTAL) $<
	@ld -r -b binary $< -o $@

$(DEV_BUILDDIR)/rivulet.o: assets/rivulet.png | $(DEV_BUILDDIR)
	@$(eval COUNT=$(shell echo $$(($(COUNT)+1))))
	@printf "[%*d/%d] Embedding %s (dev)\n" $(WIDTH) $(COUNT) $(TOTAL) $<
	@ld -r -b binary $< -o $@

$(DEV_BUILDDIR)/%.o: src/%.c $(HEADERS) | $(DEV_BUILDDIR)
	@$(eval COUNT=$(shell echo $$(($(COUNT)+1))))
	@printf "[%*d/%d] Compiling %s (dev)\n" $(WIDTH) $(COUNT) $(TOTAL) $<
	@$(CC) $(CFLAGS_DEV) -c $< -o $@

$(DEV_BUILDDIR)/swordfish: $(OBJ_DEV) $(DOC_OBJ_DEV) $(THEME_OBJ_DEV) $(DEV_BUILDDIR)/rivulet.o
	@$(eval COUNT=$(shell echo $$(($(COUNT)+1))))
	@printf "[%*d/%d] Linking %s (dev)\n" $(WIDTH) $(COUNT) $(TOTAL) $@
	@$(CC) $(OBJ_DEV) $(DOC_OBJ_DEV) $(THEME_OBJ_DEV) $(DEV_BUILDDIR)/rivulet.o $(LDFLAGS) -o $@
	@$(eval COUNT=$(shell echo $$(($(COUNT)+1))))
	@printf "[%*d/%d] Generating docs (dev)\n" $(WIDTH) $(COUNT) $(TOTAL)
	@$(DEV_BUILDDIR)/swordfish --man docs/swordfish.1
	@echo "Dev build complete. Binary located in $(DEV_BUILDDIR)/"


rel: test $(REL_BUILDDIR)/swordfish

$(REL_BUILDDIR):
	@mkdir -p $(REL_BUILDDIR)

$(REL_BUILDDIR)/docs_%.o: docs/%.txt
	@mkdir -p $(@D)
	@$(eval COUNT=$(shell echo $$(($(COUNT)+1))))
	@printf "[%*d/%d] Embedding %s (release)\n" $(WIDTH) $(COUNT) $(TOTAL) $<
	@ld -r -b binary $< -o $@

$(REL_BUILDDIR)/themes_%.o: themes/%.swt | $(REL_BUILDDIR)
	@mkdir -p $(@D)
	@$(eval COUNT=$(shell echo $$(($(COUNT)+1))))
	@printf "[%*d/%d] Embedding %s (release)\n" $(WIDTH) $(COUNT) $(TOTAL) $<
	@ld -r -b binary $< -o $@

$(REL_BUILDDIR)/rivulet.o: assets/rivulet.png | $(REL_BUILDDIR)
	@$(eval COUNT=$(shell echo $$(($(COUNT)+1))))
	@printf "[%*d/%d] Embedding %s (release)\n" $(WIDTH) $(COUNT) $(TOTAL) $<
	@ld -r -b binary $< -o $@

$(REL_BUILDDIR)/%.o: src/%.c $(HEADERS) | $(REL_BUILDDIR)
	@$(eval COUNT=$(shell echo $$(($(COUNT)+1))))
	@printf "[%*d/%d] Compiling %s (release)\n" $(WIDTH) $(COUNT) $(TOTAL) $<
	@$(CC) $(CFLAGS_REL) -c $< -o $@

$(REL_BUILDDIR)/swordfish: $(OBJ_REL) $(DOC_OBJ_REL) $(THEME_OBJ_REL) $(REL_BUILDDIR)/rivulet.o
	@$(eval COUNT=$(shell echo $$(($(COUNT)+1))))
	@printf "[%*d/%d] Linking %s (release)\n" $(WIDTH) $(COUNT) $(TOTAL) $@
	@$(CC) $(OBJ_REL) $(DOC_OBJ_REL) $(THEME_OBJ_REL) $(REL_BUILDDIR)/rivulet.o $(LDFLAGS) -o $@
	@$(eval COUNT=$(shell echo $$(($(COUNT)+1))))
	@printf "[%*d/%d] Generating docs (release)\n" $(WIDTH) $(COUNT) $(TOTAL)
	@$(REL_BUILDDIR)/swordfish --man docs/swordfish.1
	@echo "NOTE: Please increment package version in main.h"
	@echo "Release build complete. Binary located in $(REL_BUILDDIR)/"


test: $(OBJ_DEV) $(DOC_OBJ_DEV) $(THEME_OBJ_DEV) $(TEST_BUILDDIR)/test_runner
	@$(TEST_BUILDDIR)/test_runner

$(TEST_BUILDDIR):
	@mkdir -p $(TEST_BUILDDIR)

$(TEST_BUILDDIR)/%.o: tests/%.c $(HEADERS) tests/test.h | $(TEST_BUILDDIR)
	@printf "[test] Compiling %s\n" $<
	@$(CC) $(CFLAGS_DEV) -Isrc -Itests -c $< -o $@

$(TEST_BUILDDIR)/test_runner: $(TEST_OBJ) $(TEST_OBJ_SRC) $(DOC_OBJ_DEV) $(THEME_OBJ_DEV)
	@printf "[test] Linking test runner\n"
	@$(CC) $(TEST_OBJ) $(TEST_OBJ_SRC) $(DOC_OBJ_DEV) $(THEME_OBJ_DEV) $(LDFLAGS) -o $@


install: rel
	install -Dm755 $(REL_BUILDDIR)/swordfish $(DESTDIR)$(BINDIR)/swordfish

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/swordfish

clean:
	rm -rf $(DEV_BUILDDIR) $(REL_BUILDDIR) $(TEST_BUILDDIR)

format:
	clang-format -i src/*.c
