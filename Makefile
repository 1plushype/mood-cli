CC = gcc
CFLAGS = -Wall -Wextra -O2
TARGET = mood

all: $(TARGET)

$(TARGET): mood.c
	$(CC) $(CFLAGS) mood.c -o $(TARGET) -lm

clean:
	rm -f $(TARGET)
