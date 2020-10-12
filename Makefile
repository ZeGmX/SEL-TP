CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic \
         -Wformat=2 -Wno-unused-parameter -Wshadow \
         -Wwrite-strings -Wstrict-prototypes -Wold-style-definition \
         -Wredundant-decls -Wnested-externs -Wmissing-include-dirs
LDFLAGS = -l elf
OBJDIR = objdir

BIN = tp
SRC = main.c $(wildcard part$(PART)/*.c) 

OBJ = $(SRC:%.c=%.o)

all: clean $(BIN)

clean:
	rm -f *.o

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) $^ -o $@ 

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@
