CC     = gcc
TARGET = atom_sim
SRC    = atom_sim.c

RBASE  = /home/dante/raylib

ifneq ($(wildcard $(RBASE)/src/raylib.h),)
  RINC = $(RBASE)/src
  RLIB = $(RBASE)/src
else ifneq ($(wildcard $(RBASE)/include/raylib.h),)
  RINC = $(RBASE)/include
  RLIB = $(RBASE)/lib
else
  RINC = $(RBASE)
  RLIB = $(RBASE)
endif

CFLAGS   = -std=c99 -O2 -Wall -Wno-missing-braces -Wno-unused-result
INCLUDES = -I$(RINC)
LDFLAGS  = -L$(RLIB) -lraylib -lGL -lm -lpthread -ldl -lrt -lX11

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) $(INCLUDES) $(LDFLAGS) -o $(TARGET)
	@echo "  ✓  ./$(TARGET)"

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all run clean
