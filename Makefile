CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2
LDFLAGS ?= 

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
BUILDDIR = build
TARGET = $(BUILDDIR)/swordfish
SRC = src/main.c src/args.c src/process.c
HEADERS = src/args.h src/process.h
UNIT_TEST = tests/test_process

all: $(BUILDDIR) $(TARGET)

$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

$(TARGET): $(SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/swordfish

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/swordfish

clean:
	rm -rf $(BUILDDIR)

.PHONY: all install uninstall clean test