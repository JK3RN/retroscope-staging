CC ?= cc
PKG_CONFIG ?= pkg-config
PKGS := sdl2 >= 2.0.18, libpulse
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
BUILD ?= build

# Fail early with an actionable message instead of a wall of compiler errors.
ifeq ($(filter clean uninstall,$(MAKECMDGOALS)),)
ifneq ($(shell $(PKG_CONFIG) --exists '$(PKGS)' 2>/dev/null && echo ok),ok)
$(error Missing build dependencies: $(PKG_CONFIG) cannot find '$(PKGS)'. \
Install a C11 compiler, make, pkg-config and the SDL2 (>= 2.0.18) and libpulse development packages; see README.md)
endif
endif

CPPFLAGS += $(shell $(PKG_CONFIG) --cflags sdl2 libpulse)
# Project flags stay separate so a CFLAGS given on the command line (as packagers do)
# does not drop the language standard or the warnings.
CFLAGS ?= -O2 -g
RS_CFLAGS := -std=c11 -Wall -Wextra -Wpedantic
LDLIBS += $(shell $(PKG_CONFIG) --libs sdl2 libpulse) -lm
SANITIZE := -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined
HEADLESS := SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software
TESTS := scope instrument modes
SOURCES := src/retroscope.c src/instrument.h src/scope.h

.PHONY: all clean check sanitize audio-smoke showcase install uninstall
all: retroscope
retroscope: $(SOURCES)
	$(CC) $(CPPFLAGS) $(RS_CFLAGS) $(CFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD):
	mkdir -p $@
$(BUILD)/test_%: tests/test_%.c $(SOURCES) | $(BUILD)
	$(CC) $(CPPFLAGS) $(RS_CFLAGS) $(CFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

check: retroscope $(TESTS:%=$(BUILD)/test_%)
	./retroscope --version
	./retroscope --help > /dev/null
	! ./retroscope --unknown 2> /dev/null
	! ./retroscope --seconds 2> /dev/null
	! ./retroscope --seconds 0 2> /dev/null
	$(HEADLESS) ./retroscope --demo --seconds 2
	! $(HEADLESS) PULSE_SERVER=unix:$(CURDIR)/$(BUILD)/no-such-audio-server ./retroscope --seconds 2
	$(BUILD)/test_scope
	$(HEADLESS) RETROSCOPE_TEST_OUT=$(BUILD) $(BUILD)/test_instrument
	$(HEADLESS) $(BUILD)/test_modes

# AddressSanitizer + UndefinedBehaviorSanitizer (incl. LeakSanitizer where the
# platform supports it) over all suites and a short demo run.
sanitize: | $(BUILD)
	$(CC) $(CPPFLAGS) -std=c11 $(SANITIZE) src/retroscope.c -o $(BUILD)/retroscope-san $(LDLIBS)
	for t in $(TESTS); do \
		$(CC) $(CPPFLAGS) -std=c11 $(SANITIZE) tests/test_$$t.c -o $(BUILD)/test_$$t-san $(LDLIBS) && \
		$(HEADLESS) RETROSCOPE_TEST_OUT=$(BUILD) $(BUILD)/test_$$t-san || exit 1; \
	done
	$(HEADLESS) $(BUILD)/retroscope-san --demo --seconds 3

# Real capture against a private, throwaway PipeWire instance with null sinks.
# Touches neither the desktop audio session nor any hardware device.
audio-smoke: retroscope | $(BUILD)
	$(CC) $(CPPFLAGS) $(RS_CFLAGS) $(CFLAGS) tests/audio/capture_probe.c -o $(BUILD)/capture_probe $(LDFLAGS) $(LDLIBS)
	tests/audio/smoke.sh

# Regenerate the README screenshots and demo clip from the real running app on a
# Hyprland desktop (silent private PipeWire, synthetic signals; maintainers only).
showcase: retroscope | $(BUILD)
	$(CC) $(RS_CFLAGS) $(CFLAGS) tools/showcase/signal.c -o $(BUILD)/showcase_signal -lm
	tools/showcase/capture.sh

install: retroscope
	install -Dm755 retroscope $(DESTDIR)$(BINDIR)/retroscope
uninstall:
	rm -f $(DESTDIR)$(BINDIR)/retroscope
clean:
	rm -rf retroscope $(BUILD)
