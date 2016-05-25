override CFLAGS += --std=c99 -Wall
LDFLAGS += -lcurl -lbluetooth -lpopt

SRC = ttblue.c \
      bbatt.c \
      ttops.c \
      util.c \
      version.c

HEADERS = bbatt.h \
          ttops.h \
          att-types.h \
          util.h \
          version.h

OUTPUT = ttblue

all: $(OUTPUT)

setcap: $(OUTPUT)
	@echo 'This will give ttblue permissions to create raw'
	@echo 'network sockets and thereby improve the speed of'
	@echo 'the BLE connection.'
	sudo setcap 'cap_net_raw,cap_net_admin+eip' $(OUTPUT)

$(SRC:.c=.o): $(HEADERS)

%.o: %.c
	@echo Compiling $@...
	$(CC) -c $(CFLAGS) $< -o $*.o

$(OUTPUT): $(SRC:.c=.o)
	@echo Linking $(OUTPUT)...
	$(CC) -o $(OUTPUT) $(SRC:.c=.o) $(LDFLAGS)

clean:
	-rm $(OUTPUT) $(SRC:.c=.o) >/dev/null 2>&1
