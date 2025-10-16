# Makefile for mysh (COMP 3659 – Project 1)
# Only standard C / POSIX system-call wrappers are used by the program.
# No extra libraries are linked.

# --- Toolchain ---
CC      := gcc
CSTD    := -std=c99
WARN    := -Wall -Wextra -Wpedantic -Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes
OPT     := -O2
DEFS    := -D_POSIX_C_SOURCE=200809L
CFLAGS  := $(CSTD) $(WARN) $(OPT) $(DEFS)
LDFLAGS :=                 # intentionally empty (no extra libs)

# --- Files ---
TARGET  := mysh
SRCS    := mysh.c
OBJS    := $(SRCS:.c=.o)

# --- Default ---
.PHONY: all
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# --- Developer helpers ---
.PHONY: run
run: $(TARGET)
	./$(TARGET)

# quick non-interactive smoke test (does not cover everything)
.PHONY: smoke
smoke: $(TARGET)
	@printf "echo hello\nexit\n" | ./$(TARGET)
	@printf "echo hi > out.txt\ncat < out.txt\nexit\n" | ./$(TARGET)
	@printf "ls | wc -l\nexit\n" | ./$(TARGET)

# --- Cleanup ---
.PHONY: clean distclean
clean:
	@rm -f $(OBJS) $(TARGET) out.txt *.tmp
distclean: clean
	@rm -f mysh.tar.gz

# --- Archive for submission (adjust 'projdir' to your directory name one level up) ---
.PHONY: submit
submit: clean
	@cd .. && tar cf mysh.tar assignments && gzip -f mysh.tar && echo "Created ../mysh.tar.gz"
