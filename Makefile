CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
PKG_CONFIG ?= pkg-config

GTK_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk4)
GTK_LIBS   := $(shell $(PKG_CONFIG) --libs gtk4)

# GTK4 should use gtksourceview-5 (NOT -4)
GS_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtksourceview-5 2>/dev/null)
GS_LIBS   := $(shell $(PKG_CONFIG) --libs gtksourceview-5 2>/dev/null)

# Windows needs ws2_32 for sockets
ifeq ($(OS),Windows_NT)
WS2LIB := -lws2_32
else
WS2LIB :=
endif

# If gtksourceview-5 is available, add define and flags
ifeq ($(strip $(GS_CFLAGS)),)
	EXTRA_CFLAGS :=
	EXTRA_LIBS :=
else
	EXTRA_CFLAGS := $(GS_CFLAGS) -DHAVE_GTK_SOURCE_5
	EXTRA_LIBS := $(GS_LIBS)
endif

SRC := \
  src/main.c \
  src/ui_main.c \
	src/app_controller.c \
	src/udp_io.c

# Build directory for object and dependency files
BUILD_DIR := build

# Objects go into $(BUILD_DIR)
OBJ := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRC))

# Dependency files (.d) also live in $(BUILD_DIR)
DEPS := $(patsubst src/%.c,$(BUILD_DIR)/%.d,$(SRC))

TARGET := netassist_gtk4

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(GTK_LIBS) $(EXTRA_LIBS) $(WS2LIB)


# Ensure build directory exists before compiling
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Compile sources into $(BUILD_DIR) and generate dependency files there
$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) $(EXTRA_CFLAGS) -MMD -MP -MF $(BUILD_DIR)/$*.d -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET) $(TARGET).exe

# Include generated dependency files if present
-include $(DEPS)

.PHONY: all clean

run: $(TARGET)
	./$(TARGET)
