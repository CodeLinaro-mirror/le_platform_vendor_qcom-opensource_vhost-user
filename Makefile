#this makefile is for desktop

#executable name
TARGET=vhost-user-qti

#specify QEMU root dir
# QEMU_DIR=

# api path
INC_DIR =
CC=gcc

BIN_DIR=../bin

# intermediate obj path
OBJ_DIR=../obj

# local lib path
LIB_DIR = ../bin

CFLAGS=-I../.. -I../../include -I$(QEMU_DIR)/include -I$(QEMU_DIR) -DQEMU_DESKTOP

# extra libs
LIBS=

# dependency (api)
_DEPS =
DEPS = $(patsubst %,$(INC_DIR)/%,$(_DEPS))

# source files
_OBJ = vhost_user_q.o vhost_ioctl.o
OBJ = $(patsubst %,$(OBJ_DIR)/%,$(_OBJ))


$(OBJ_DIR)/%.o: %.c $(DEPS)
	@mkdir -p $(@D)
	$(CC) -g -c -o $@ $< $(CFLAGS)

$(BIN_DIR)/$(TARGET): $(OBJ)
	@mkdir -p $(@D)
	gcc -g -o $@ $^ $(CFLAGS) -L$(LIB_DIR) $(LIBS)

.PHONY: clean

clean:
	rm -f $(OBJ_DIR)/*.o *~ core $(INCDIR)/*~ $(BIN_DIR)/$(TARGET)


