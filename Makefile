# Compiler and Flags
CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O2
LDFLAGS ?= -lpthread -lgpiod

# Target Binary Names
TARGET1 = qups-guard2
TARGET2 = qups-guard2-ha

# Source Files
SRC1    = qups-guard2.c
SRC2    = qups-guard2-ha.c

# Installation Path
PREFIX  ?= /usr/local

.PHONY: all clean install uninstall

# Default target: build both binaries
all: $(TARGET1) $(TARGET2)

# Build standard daemon
$(TARGET1): $(SRC1)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# Build MQTT-enabled daemon (requires libmosquitto & libcjson)
$(TARGET2): $(SRC2)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) -lmosquitto -lcjson

# Install binaries to system path
install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET1) $(DESTDIR)$(PREFIX)/bin/
	install -m 755 $(TARGET2) $(DESTDIR)$(PREFIX)/bin/

# Remove installed binaries
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET1)
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET2)

# Clean up build artifacts
clean:
	rm -f $(TARGET1) $(TARGET2) *.o
