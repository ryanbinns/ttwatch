CFLAGS += -g -O0

SRC = ttbin.c \
      export_csv.c \
      export_kml.c \
      export_gpx.c \

OUTPUT = libttbin.a

$(OUTPUT): $(SRC:.c=.o)
	@echo Creating $(OUTPUT)...
	@$(AR) rcs $(OUTPUT) $^

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

-include *.d

