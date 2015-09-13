CFLAGS += -g -O0 -I../ttbin -I../libttwatch
LDFLAGS += -L../ttbin -L../libttwatch -lttbin -lpcap -lttwatch -lusb-1.0 -lcurl -lstdc++ -lc -lgcc -lm

SRC = ttpcap.c

OUTPUT = ttpcap

$(OUTPUT): $(SRC:.c=.o) ../ttbin/libttbin.a ../libttwatch/libttwatch.a
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
	@-rm $(OUTPUT) $(SRC:.c=.o) $(SRC:.c=.d) >/dev/null 2>&1

install: $(OUTPUT)
	-install -m0755 $(OUTPUT) $(PREFIX)/bin

-include *.d

