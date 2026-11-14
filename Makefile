# Portable build: native or cross (e.g. make CC=aarch64-linux-gnu-gcc PKG_CONFIG=aarch64-linux-gnu-pkg-config)
VERSION  ?= 0.1.0
PREFIX   ?= /usr
DESTDIR  ?=
CC       ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS   ?= -O2 -g
CFLAGS   += -Wall -Wextra -std=gnu11 -DVERSION=\"$(VERSION)\" \
            $(shell $(PKG_CONFIG) --cflags libcdio libcdio_paranoia libcdio_cdda flac libcddb 2>/dev/null)
LDLIBS   += $(shell $(PKG_CONFIG) --libs libcdio_paranoia libcdio_cdda libcdio flac libcddb 2>/dev/null) -lm

SRC = src/main.c src/cdrip.c src/sacd.c src/deemph.c src/wav.c src/sink.c src/meta.c src/util.c
OBJ = $(SRC:.c=.o)

all: sonora-rip
sonora-rip: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

tests/test_deemph: tests/test_deemph.c src/deemph.c
	$(CC) $(CFLAGS) -o $@ $^ -lm
tests/test_util: tests/test_util.c src/util.c
	$(CC) $(CFLAGS) -o $@ $^
check: tests/test_deemph tests/test_util
	./tests/test_deemph
	./tests/test_util

install: sonora-rip
	install -D -m 0755 sonora-rip $(DESTDIR)$(PREFIX)/bin/sonora-rip
	install -D -m 0644 docs/sonora-rip.1 $(DESTDIR)$(PREFIX)/share/man/man1/sonora-rip.1
clean:
	rm -f $(OBJ) sonora-rip tests/test_deemph tests/test_util
.PHONY: all check install clean
