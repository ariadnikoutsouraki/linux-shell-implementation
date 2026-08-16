
CC = gcc
CFLAGS = -Wall -Wextra -std=c99
SOURCE = project2.c
EXECUTABLE = project2

# Προεπιλεγμένος στόχος: Φτιάχνει το εκτελέσιμο
all: $(EXECUTABLE)

$(EXECUTABLE): $(SOURCE)
	$(CC) $(CFLAGS) $(SOURCE) -o $(EXECUTABLE)

# Στόχος: Καθαρισμός. Αφαιρεί το εκτελέσιμο και τυχόν αρχεία αντικειμένων
clean:
	rm -f $(EXECUTABLE)
	rm -f *.o
	
.PHONY: all clean