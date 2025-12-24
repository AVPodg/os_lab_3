CC = gcc
CFLAGS = -Wall -std=gnu99 -D_POSIX_C_SOURCE=200112L -D_XOPEN_SOURCE=500
TARGET = lab3
SRCDIR = src

$(TARGET): $(SRCDIR)/main.c
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCDIR)/main.c -lrt

test: $(TARGET)
	@echo "100 2 5" > test.txt
	@echo "64 4 2 2" >> test.txt
	@echo "15 3" >> test.txt
	@echo "4" >> test.txt
	@./$(TARGET) test.txt
	@rm -f test.txt

test-error: $(TARGET)
	@echo "100 2 5" > test.txt
	@echo "64 0 2" >> test.txt
	@./$(TARGET) test.txt || true
	@rm -f test.txt

clean:
	rm -f $(TARGET) test.txt

.PHONY: test test-error clean