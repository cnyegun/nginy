CC = gcc
CFLAGS = -Wall -Wextra -O3

TARGET = nginy

all: $(TARGET)

$(TARGET): nginy.c
	$(CC) $(CFLAGS) -o $(TARGET) nginy.c

clean:
	rm -f $(TARGET)

run: $(TARGET)
	./$(TARGET)
