# Standalone build for write_serial.
#
# Use this when you only want the tool and do not want to configure the whole
# host/ CMake tree.  For an in-tree build, write_serial is registered in
# host/utilities/CMakeLists.txt and is built together with bladeRF-cli.
#
#   make                 # build against an installed libbladeRF
#   make install         # install to $(PREFIX)/bin  (default /usr/local)
#   make clean

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=c99
PREFIX  ?= /usr/local

TARGET  := write_serial
SRC     := write_serial.c

# Prefer pkg-config; fall back to a plain /usr/local layout, which is where
# "sudo make install" from host/build puts libbladeRF.
PKGCONFIG_CFLAGS := $(shell pkg-config --cflags libbladeRF 2>/dev/null)
PKGCONFIG_LIBS   := $(shell pkg-config --libs   libbladeRF 2>/dev/null)

ifeq ($(strip $(PKGCONFIG_LIBS)),)
    BLADERF_CFLAGS := -I$(PREFIX)/include
    BLADERF_LIBS   := -L$(PREFIX)/lib -lbladeRF
else
    BLADERF_CFLAGS := $(PKGCONFIG_CFLAGS)
    BLADERF_LIBS   := $(PKGCONFIG_LIBS)
endif

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(BLADERF_CFLAGS) -o $@ $< $(BLADERF_LIBS)

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)

clean:
	rm -f $(TARGET)
