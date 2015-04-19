CFLAGS += -g -O0

SRC = libttwatch.cpp \
      libttwatch_cpp.cpp \

OUTPUT = libttwatch.a

$(OUTPUT): $(SRC:.c=.o) $(SRC:.cpp=.o)
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

%.o: %.cpp
	@echo Compiling $@...
	@$(CC) -c $(CFLAGS) $< -o $*.o
	@$(CC) -MM $(CFLAGS) $< > $*.d
	@mv -f $*.d $*.d.tmp
	@sed -e 's|.*:|$*.o:|' <$*.d.tmp >$*.d
	@sed -e 's/.*://' -e 's/\\$$//' < $*.d.tmp | fmt -1 | \
	  sed -e 's/^ *//' -e 's/$$/:/' >> $*.d
	@rm -f $*.d.tmp

clean:
	@-rm $(OUTPUT) $(SRC:.cpp=.o) $(SRC:.cpp=.d) >/dev/null 2>&1

install: $(OUTPUT)

-include *.d

