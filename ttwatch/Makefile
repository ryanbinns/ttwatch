CFLAGS += -g -O0 -I../ttbin
LDFLAGS += -L../ttbin -lttbin -lusb-1.0 -lcurl -lc -lgcc -lm

SRC = ttwatch.c \
      messages.c \
      log.c \

OUTPUT = ttwatch

$(OUTPUT): $(SRC:.c=.o) ../ttbin/libttbin.a
	@echo Linking $(OUTPUT)...
	@$(CC) -o $(OUTPUT) $(SRC:.c=.o) $(LDFLAGS)

%.o: %.c
	@echo Compiling $@...
	@$(CC) -c $(CFLAGS) $< -o $*.o
	@$(CC) -MM $(CFLAGS) $< > $*.d
	@mv -f $*.d $*.d.tmp
	@sed -e 's|.*:|$*.o:|' <$*.d.tmp >$*.d
	@sed -e 's/.*://' -e 's/\\$$//' < $*.d.tmp | fmt -1 | \
	  sed -e 's/^ *//' -e 's/$$/:/' >> $*.d
	@rm -f $*.d.tmp

clean:
	@rm $(OUTPUT) $(SRC:.c=.o) $(SRC:.c=.d)

install: $(OUTPUT)
	-install -m0755 $(OUTPUT) $(PREFIX)/bin

-include *.d

