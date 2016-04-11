.PHONY: all install clean fmt

DEBUG ?= 0
CFLAGS ?= -O2 -Wall
CXXFLAGS = $(CFLAGS) -std=c++11
BINDIR ?= /usr/local/bin

OBJS = main.o

%.o: %.cc
	$(CXX) -DDEBUG=$(DEBUG) $(CXXFLAGS) -c -o $@ $<

lsrepostat: $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(OBJS)

all: lsrepostat

install: lsrepostat
	install -m 755 lsrepostat $(BINDIR)/lsrepostat

clean:
	rm -f $(OBJS) lsrepostat

fmt:
	@find . -iname '*.cc' -o -iname '*.h' -o -iname '*.c' | xargs clang-format -i
