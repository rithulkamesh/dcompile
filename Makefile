# dcompile build.
#
#   make            build build/dcompile
#   make test       run the JIT self-test (needs no game files)
#   make smoke      boot the game for a while and check it renders (needs the game and a display)
#   make run        build and run the game
#   make format     format all sources with clang-format
#   make clean
#
# Requirements: macOS on Apple silicon, Xcode command line tools, and Homebrew's ffmpeg + pkg-config (scripts/setup.sh).

CC ?= clang
BUILD := build
TARGET := $(BUILD)/dcompile

FFMPEG_PKGS := libavformat libavcodec libavutil libswscale libswresample
FFMPEG_CFLAGS := $(shell pkg-config --cflags $(FFMPEG_PKGS))
FFMPEG_LIBS := $(shell pkg-config --libs $(FFMPEG_PKGS))

CFLAGS := -O2 -g -Isrc -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-deprecated-declarations \
          -Wno-sign-compare -Wno-missing-field-initializers $(FFMPEG_CFLAGS)
OBJCFLAGS := -fno-objc-arc
FRAMEWORKS := -framework CoreFoundation -framework Cocoa -framework OpenGL -framework AudioToolbox \
              -framework CoreAudio -framework IOKit

# The emulator core is a single translation unit (see docs/architecture.md): every header under src/ is a dependency of main.c.
HEADERS := $(shell find src -name '*.h')

all: $(TARGET)

$(BUILD)/main.o: src/main.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/host/%.m $(wildcard src/host/*.h) | $(BUILD)
	$(CC) $(CFLAGS) $(OBJCFLAGS) -c $< -o $@

$(TARGET): $(BUILD)/main.o $(BUILD)/hostgfx.o $(BUILD)/hostaudio.o
	$(CC) $^ -o $@ $(FFMPEG_LIBS) $(FRAMEWORKS)

$(BUILD):
	mkdir -p $(BUILD)

test: $(TARGET)
	DC_JITSELFTEST=5000 $(TARGET)

smoke: $(TARGET)
	scripts/smoke-test.sh

run: $(TARGET)
	./run.sh

format:
	scripts/format.sh

clean:
	rm -rf $(BUILD)

.PHONY: all test smoke run format clean
