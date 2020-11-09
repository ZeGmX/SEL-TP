CC = gcc
CFLAGS = -DPART=$(PART) \
	 -Wall -Wextra -Wpedantic \
         -Wformat=2 -Wno-unused-parameter -Wshadow \
         -Wwrite-strings -Wstrict-prototypes \
         -Wredundant-decls -Wnested-externs -Wmissing-include-dirs
LDFLAGS =
OBJDIR = objdir

BIN = tp
TARGET = target
SRC = main.c $(wildcard part$(PART)/*.c)

OBJ = $(SRC:%.c=%.o)

all: clean $(BIN) $(TARGET)


clean:
	rm -f *.o

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) $^ -o $@

$(TARGET): $(TARGET).c
	$(CC) $(CFLAGS) $^ -o $@

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@
