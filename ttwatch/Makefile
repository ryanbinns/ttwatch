CFLAGS += -g -O0 -I../ttbin -I../libttwatch
LDFLAGS += -L../ttbin -L../libttwatch -lttbin -lttwatch -lusb-1.0 -lcurl -lstdc++ -lc -lgcc -lm

SRC = ttwatch.c \
      log.c \
      options.c \

OUTPUT = ttwatch

$(OUTPUT): $(SRC:.c=.o) ../ttbin/libttbin.a ../libttwatch/libttwatch.a
	@echo Linking $(OUTPUT)...
	@$(CC) -o $(OUTPUT) $(SRC:.c=.o) $(LDFLAGS)

ttwatch.o: manifest_definitions.h manifest_definitions_0001082e.h manifest_definitions_00010113.h

%.o: %.c
	@echo Compiling $@...
	@$(CC) -c $(CFLAGS) $< -o $*.o
	@$(CC) -MM $(CFLAGS) $< > $*.d
	@mv -f $*.d $*.d.tmp
	@sed -e 's|.*:|$*.o:|' <$*.d.tmp >$*.d
	@sed -e 's/.*://' -e 's/\\$$//' < $*.d.tmp | fmt -1 | \
	  sed -e 's/^ *//' -e 's/$$/:/' >> $*.d
	@rm -f $*.d.tmp

manifest_definitions.h: manifest.txt make_manifest.pl
	@./make_manifest.pl <$< >$@

manifest_definitions_0001082e.h: manifest_0001082e.txt make_manifest.pl
	@./make_manifest.pl <$< >$@

manifest_definitions_00010113.h: manifest_00010113.txt make_manifest.pl
	@./make_manifest.pl <$< >$@

clean:
	@-rm $(OUTPUT) $(SRC:.c=.o) $(SRC:.c=.d) manifest_definitions*.h >/dev/null 2>&1

install: $(OUTPUT)
	-install -m0755 $(OUTPUT) $(PREFIX)/bin

-include *.d

