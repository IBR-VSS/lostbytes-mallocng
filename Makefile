
ALL = libmallocng.a libmallocng.so

C_SRCS = malloc.c calloc.c free.c realloc.c aligned_alloc.c posix_memalign.c \
memalign.c malloc_usable_size.c dump.c mallocstat.c helper.c
CXX_SRCS = mallocstat_intervals.cc

C_OBJS = $(C_SRCS:.c=.o)
CXX_OBJS = $(CXX_SRCS:.cc=.o)
OBJS = $(C_OBJS) $(CXX_OBJS)

CFLAGS = -fPIC -Wall -O2 -ffreestanding -g
CXXFLAGS = -std=c++23 -fPIC -Wall -O2 -fno-exceptions -fno-rtti  -g 

CC ?= clang
CXX ?= clang++

-include config.mak

all: $(ALL)

# Pattern-Rule für C-Dateien
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Pattern-Rule für C++-Dateien
%.o: %.cc
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJS): meta.h glue.h

clean:
	rm -f $(ALL) $(OBJS)

libmallocng.a: $(OBJS)
	rm -f $@
	ar rc $@ $(OBJS)
	ranlib $@

libmallocng.so: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -shared -o $@ $(OBJS) -pthread -lstdc++
