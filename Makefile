CC      = gcc
CFLAGS  = -std=c89 -Wall -Wextra -pedantic -O2 -Isrc \
          -DKON_RUNTIME_INC='"$(CURDIR)/runtime/rt_console.inc"'
BUILDDIR = build
OBJS    = $(BUILDDIR)/main.o $(BUILDDIR)/cp866.o $(BUILDDIR)/lexer.o \
          $(BUILDDIR)/ast.o $(BUILDDIR)/parser.o $(BUILDDIR)/codegen.o \
          $(BUILDDIR)/interp.o

all: konlang

konlang: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) -lm

$(BUILDDIR)/%.o: src/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

check: konlang
	./konlang check examples/hello.kn

clean:
	rm -rf $(BUILDDIR) konlang

.PHONY: all check clean
