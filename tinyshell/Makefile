# =============================================================
# Makefile for C Shell Program using GCC
# =============================================================

# --- Variables ---
CC = gcc # Define the C Compiler
EXECUTABLE = tinyshell
SOURCE = tinyshell.c # Set the source file to .c extension
# Flags: -std=c99 (or c11) for C standard, -Wall for all warnings, -g for debugging info
CFLAGS = -std=c99 -Wall -g
LDFLAGS = 

# -------------------------------------------------------------
# Default Target: 'all'
# -------------------------------------------------------------
all: $(EXECUTABLE)

# -------------------------------------------------------------
# Linking Rule
# Creates the final executable from the source file.
# Note: Uses CC (C compiler) and CFLAGS (C flags)
# -------------------------------------------------------------
$(EXECUTABLE): $(SOURCE)
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

# -------------------------------------------------------------
# Clean Rule
# Removes the executable and any object files (*.o)
# -------------------------------------------------------------
.PHONY: all clean

clean:
	rm -f $(EXECUTABLE) *.o