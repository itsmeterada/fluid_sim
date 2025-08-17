# Makefile for Fluid Simulation

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2
LIBS = -lSDL2 -lm

# Platform specific settings
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # macOS
    CFLAGS += -I/opt/homebrew/include -I/usr/local/include
    LIBS += -L/opt/homebrew/lib -L/usr/local/lib
else ifeq ($(UNAME_S),Linux)
    # Linux
    LIBS += `pkg-config --cflags --libs sdl2`
else
    # Windows (MinGW)
    LIBS += -lmingw32 -lSDL2main -lSDL2
endif

TARGET = fluid_sim
SOURCE = fluid_sim.c

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE) $(LIBS)

clean:
	rm -f $(TARGET)

install:
	# Ubuntu/Debian
	sudo apt-get update
	sudo apt-get install libsdl2-dev
	# Or for macOS with Homebrew:
	# brew install sdl2

run: $(TARGET)
	./$(TARGET)

help:
	@echo "Available targets:"
	@echo "  all     - Build the fluid simulation"
	@echo "  clean   - Remove built files"
	@echo "  install - Install SDL2 dependencies (Linux)"
	@echo "  run     - Build and run the simulation"
	@echo "  help    - Show this help message"
	@echo ""
	@echo "Controls:"
	@echo "  SPACE     - Pause/Resume simulation"
	@echo "  R         - Reset simulation"
	@echo "  UP/DOWN   - Add/Remove particles"
	@echo "  LEFT/RIGHT - Set gravity direction (left/right)"
	@echo "  U/D       - Set gravity direction (up/down)"
	@echo "  G         - Toggle gravity (down/off)"
	@echo "  M         - Toggle display mode (Smooth/Classic)"
	@echo "  Left Click - Add particles at mouse position"
	@echo "  ESC       - Exit"
