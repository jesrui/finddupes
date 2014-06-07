CFLAGS = -Wall -O2 -g -I.
OBJS = finddupes.o md5/md5.o

# Use malloc wrappers to abort on failure? This works with gcc and clang
LDFLAGS += -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc
OBJS += wrapmalloc.o

finddupes: $(OBJS)

finddupes.o: finddupes.c klib/khash.h klib/klist.h md5/md5.h
md5/md5.o: md5/md5.h

.PHONY : clean
clean :
	${RM} finddupes ${OBJS}

