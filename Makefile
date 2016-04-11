.PHONY: all install clean fmt

DEBUG ?= 0
CFLAGS ?= -O2 -Wall
CXXFLAGS = $(CFLAGS) -std=c++11

OBJS = main.o

%.o: %.cc
	$(CXX) -DDEBUG=$(DEBUG) $(CXXFLAGS) -c -o $@ $<

all: $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o lsrepostat $(OBJS)

install:

clean:
	rm -f $(OBJS) lsrepostat

fmt:
	@find . -iname '*.cc' -o -iname '*.h' -o -iname '*.c' | xargs clang-format -i
