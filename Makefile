CC = gcc
CFLAGS = -Wall -Wextra -O2
TARGET = mood
PREFIX ?= /usr/local

all: $(TARGET)

$(TARGET): mood.c
	$(CC) $(CFLAGS) mood.c -o $(TARGET) -lm

install: $(TARGET)
	install -Dm755 $(TARGET) $(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(PREFIX)/bin/$(TARGET)

clean:
	rm -f $(TARGET)
