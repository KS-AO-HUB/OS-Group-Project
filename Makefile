# =============================================================================
#  Makefile for Catalan Numbers Multithreading Project
#  COP 6611 — Operating Systems
# =============================================================================

CC       = gcc
CFLAGS   = -Wall -Wextra -std=c11 -g
LDFLAGS  = -pthread
TARGET   = catalan
SRC      = catalan.c
INPUT    = input.txt

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET) $(INPUT)

clean:
	rm -f $(TARGET)