CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
PKG_CONFIG ?= pkg-config

CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
PKG_CONFIG ?= pkg-config

GTK_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk4)
GTK_LIBS   := $(shell $(PKG_CONFIG) --libs gtk4)

# GTK4 should use gtksourceview-5 (NOT -4)
GS_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtksourceview-5 2>/dev/null)
GS_LIBS   := $(shell $(PKG_CONFIG) --libs gtksourceview-5 2>/dev/null)

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
  src/app_controller.c

OBJ := $(SRC:.c=.o)
TARGET := netassist_gtk4

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(GTK_LIBS) $(EXTRA_LIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) $(EXTRA_CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET) $(TARGET).exe

run: $(TARGET)
	./$(TARGET)
