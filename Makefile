CFLAGS = -Wall -std=c99 -D_BSD_SOURCE -D_FILE_OFFSET_BITS=64 -O2 -g -I.
OBJS = finddupes.o md5/md5.o
PREFIX = /usr/local

# If the sources come from a git repo, look for program version in repo tag
GIT_VERSION := $(shell git describe --tags | sed "s/-/./g")
CFLAGS += -DGIT_VERSION='"$(GIT_VERSION)"'

# Use malloc wrappers to abort on failure? This works with gcc and clang
LDFLAGS += -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc
OBJS += wrapmalloc.o

finddupes: $(OBJS)

finddupes.o: finddupes.c klib/khash.h klib/klist.h md5/md5.h
md5/md5.o: md5/md5.h

.PHONY: clean
clean:
	${RM} finddupes ${OBJS}

.PHONY: install
install: finddupes
	 install -D finddupes ${PREFIX}/bin/finddupes
	 install -Dm644 finddupes.1 ${PREFIX}/man/man1/finddupes.1
