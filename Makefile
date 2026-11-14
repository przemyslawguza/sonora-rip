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

# GUI (GTK 3) is built automatically when gtk+-3.0 is found; force off with GUI=no
GUI ?= auto
HAVE_GTK := $(shell $(PKG_CONFIG) --exists gtk+-3.0 && echo yes)
ifeq ($(GUI),no)
HAVE_GTK :=
endif
GTK_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk+-3.0 2>/dev/null)
GTK_LIBS   := $(shell $(PKG_CONFIG) --libs gtk+-3.0 2>/dev/null)

CORE = src/cdrip.c src/sacd.c src/deemph.c src/wav.c src/sink.c src/meta.c src/util.c
CORE_OBJ = $(CORE:.c=.o)
OBJ = src/main.o $(CORE_OBJ)
BINS = sonora-rip $(if $(HAVE_GTK),sonora-rip-gui)

all: $(BINS)
sonora-rip: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)
sonora-rip-gui: src/gui.o $(CORE_OBJ)
	$(CC) $(LDFLAGS) -o $@ src/gui.o $(CORE_OBJ) $(LDLIBS) $(GTK_LIBS) -pthread
src/gui.o: src/gui.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -c -o $@ $<

tests/test_deemph: tests/test_deemph.c src/deemph.c
	$(CC) $(CFLAGS) -o $@ $^ -lm
tests/test_util: tests/test_util.c src/util.c
	$(CC) $(CFLAGS) -o $@ $^
check: tests/test_deemph tests/test_util
	./tests/test_deemph
	./tests/test_util

install: $(BINS)
	install -D -m 0755 sonora-rip $(DESTDIR)$(PREFIX)/bin/sonora-rip
ifneq ($(HAVE_GTK),)
	install -D -m 0755 sonora-rip-gui $(DESTDIR)$(PREFIX)/bin/sonora-rip-gui
	install -D -m 0644 data/sonora-rip.desktop $(DESTDIR)$(PREFIX)/share/applications/sonora-rip.desktop
endif
	install -D -m 0644 docs/sonora-rip.1 $(DESTDIR)$(PREFIX)/share/man/man1/sonora-rip.1
clean:
	rm -f $(OBJ) src/gui.o sonora-rip sonora-rip-gui tests/test_deemph tests/test_util
.PHONY: all check install clean
