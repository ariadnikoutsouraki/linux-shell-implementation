
CC = gcc

CFLAGS = -Wall -Wextra -g

# Το όνομα του εκτελέσιμου αρχείου
TARGET = tinyshell3

# Το αρχείο πηγαίου κώδικα
SRC = tinyshell3.c

# Τρέχει γράφοντας απλά 'make'
all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

run: $(TARGET)
	./$(TARGET)

# Καθαρισμός των αρχείων που δημιουργήθηκαν (το εκτελέσιμο)
clean:
	rm -f $(TARGET)

# Δηλώνουμε ότι τα 'all', 'clean' και 'run' δεν είναι αρχεία
.PHONY: all clean run