CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -Wpedantic -std=c11 -O2
TARGET  = healthcare
SRC     = main.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

run: $(TARGET)
	./$(TARGET)

# Removes only the compiled program. Patient data is kept.
clean:
	rm -f $(TARGET)

# Removes the program AND all saved patient data.
distclean: clean
	rm -f patients.tsv patients.tsv.tmp

.PHONY: all run clean distclean
