CC ?= cc
TARGET ?= build/aurora
CFLAGS ?= -O2 -pipe
CFLAGS += -std=c11 -Wall -Wextra -D_DEFAULT_SOURCE -pthread
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)

.PHONY: all clean release check
all: $(TARGET)

$(TARGET): src/aurora.c
	@pkg-config --exists sdl2 || { echo "ERROR: SDL2 development files are missing."; exit 1; }
	@mkdir -p build
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $< $(SDL_LIBS) -lm -pthread

release: CFLAGS += -DNDEBUG
release: clean all

check:
	@sh tests/test_package.sh

clean:
	rm -rf build
