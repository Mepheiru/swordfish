CC ?= gcc
CFLAGS ?= -Wall -Wextra -Werror -O2 -std=gnu11
LDFLAGS ?= 

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
BUILDDIR = build
TARGET = $(BUILDDIR)/swordfish
SRC = src/main.c src/args.c src/process.c src/hooks.c
HEADERS = src/args.h src/process.h src/main.h src/hooks.h
OBJ = $(SRC:src/%.c=$(BUILDDIR)/%.o)

# Progress tracking
COUNT = 0
TOTAL = $(shell echo $$(($(words $(SRC)) + 1)))  # +1 for linking

# Compute width for alignment (e.g., 1/12, 10/12)
WIDTH = $(shell echo $$((${TOTAL}<10?1:${TOTAL}<100?2:3)))

all: $(TARGET)

$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

# Compile each .c file into an object file with progress counter
$(BUILDDIR)/%.o: src/%.c $(HEADERS) | $(BUILDDIR)
	@$(eval COUNT=$(shell echo $$(($(COUNT)+1))))
	@printf "[%*d/%d] Compiling %s\n" $(WIDTH) $(COUNT) $(TOTAL) $<
	@$(CC) $(CFLAGS) -c $< -o $@

# Link object files
$(TARGET): $(OBJ)
	@$(eval COUNT=$(shell echo $$(($(COUNT)+1))))
	@printf "[%*d/%d] Linking %s\n" $(WIDTH) $(COUNT) $(TOTAL) $@
	@$(CC) $(OBJ) $(LDFLAGS) -o $@
	@echo "Cleaning up..."
	@rm -f $(OBJ)
	@echo "Build complete! Located in $(BUILDDIR)"

# Separate step for cleaning up object files
.PHONY: cleanup_objs
cleanup_objs:
	@echo "Cleaning up..."
	@rm -f $(OBJ)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/swordfish

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/swordfish

clean:
	rm -rf $(BUILDDIR)

.PHONY: all install uninstall clean test
