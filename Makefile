CC ?= cc
PKG_CONFIG ?= pkg-config
PREFIX ?= /usr
DESTDIR ?=
VERSION := 0.1.0
CPPFLAGS += $(shell $(PKG_CONFIG) --cflags gtk+-3.0)
CFLAGS ?= -Os
CFLAGS += -std=c11 -Wall -Wextra -Wformat=2 -Wshadow -Wstrict-prototypes -ffunction-sections -fdata-sections
LDLIBS += $(shell $(PKG_CONFIG) --libs gtk+-3.0) -lm
LDFLAGS += -Wl,--gc-sections -Wl,-z,relro,-z,now
APPS := calc notes tasks view clock
BINARIES := $(addprefix build/lime-,$(APPS))

.PHONY: all clean install uninstall test smoke dist
all: $(BINARIES)

build:
	mkdir -p $@

build/common.o: src/common.c src/common.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build/lime-calc: src/calc.c src/calc_engine.c src/calc_engine.h build/common.o
	$(CC) $(CPPFLAGS) $(CFLAGS) src/calc.c src/calc_engine.c build/common.o $(LDFLAGS) $(LDLIBS) -o $@

build/lime-%: src/%.c src/common.h build/common.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $< build/common.o $(LDFLAGS) $(LDLIBS) -o $@

build/test-calc: tests/test_calc.c src/calc_engine.c src/calc_engine.h | build
	$(CC) $(CFLAGS) -Isrc tests/test_calc.c src/calc_engine.c -lm -o $@

test: all build/test-calc
	./build/test-calc
	@for app in $(BINARIES); do $$app --version; done

smoke: all
	@for app in $(BINARIES); do echo "Smoke: $$app"; LIME_SMOKE_TEST=1 G_DEBUG=fatal-warnings $$app || exit 1; done

install: all
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/share/applications $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps $(DESTDIR)$(PREFIX)/share/licenses/lime-apps
	install -m755 $(BINARIES) $(DESTDIR)$(PREFIX)/bin/
	install -m644 data/*.desktop $(DESTDIR)$(PREFIX)/share/applications/
	install -m644 data/icons/*.svg $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/
	install -m644 LICENSE $(DESTDIR)$(PREFIX)/share/licenses/lime-apps/

uninstall:
	@for app in $(APPS); do rm -f $(DESTDIR)$(PREFIX)/bin/lime-$$app $(DESTDIR)$(PREFIX)/share/applications/lime-$$app.desktop; done
	@for icon in data/icons/*.svg; do rm -f $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/$$(basename $$icon); done
	rm -f $(DESTDIR)$(PREFIX)/share/licenses/lime-apps/LICENSE

dist:
	git archive --format=tar --prefix=lime-apps-$(VERSION)/ HEAD | gzip -n > lime-apps-$(VERSION).tar.gz

clean:
	rm -rf build
