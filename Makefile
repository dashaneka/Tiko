CC      ?= cc
TARGET  := tiko
PKGS    := gtk4 libadwaita-1 webkitgtk-6.0
TEST_PKGS := glib-2.0

PKG_CFLAGS := $(shell pkg-config --cflags $(PKGS))
PKG_LIBS   := $(shell pkg-config --libs $(PKGS))
TEST_CFLAGS := $(shell pkg-config --cflags $(TEST_PKGS))
TEST_LIBS   := $(shell pkg-config --libs $(TEST_PKGS))

WARN    := -std=c17 -Wall -Wextra -Werror -Wformat=2 -Wshadow
OPT     := -Oz -flto=auto -ffunction-sections -fdata-sections -fvisibility=hidden \
           -fno-asynchronous-unwind-tables -fno-unwind-tables -fomit-frame-pointer \
           -fno-ident -fmerge-all-constants -DNDEBUG
LDFLAGS := -Wl,--gc-sections -Wl,--as-needed -Wl,-O1 -Wl,--build-id=none \
           -Wl,--no-eh-frame-hdr -s

SRCS := src/app.c src/core.c

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SRCS) src/core.h src/inject.h
	$(CC) $(WARN) $(OPT) $(PKG_CFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS) $(PKG_LIBS) -lm
	strip --strip-all --remove-section=.comment --remove-section=.note* $(TARGET) 2>/dev/null || true
	@ls -lh $(TARGET)

test: tests/test_core.c src/core.c src/core.h
	$(CC) $(WARN) -O2 -g -D_FORTIFY_SOURCE=2 -fstack-protector-strong \
		$(TEST_CFLAGS) tests/test_core.c src/core.c -o test_core $(TEST_LIBS) -lm
	./test_core
	rm -f test_core

clean:
	rm -f $(TARGET) test_core
