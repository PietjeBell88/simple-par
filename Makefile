all: default
# Sources
SRCS  = common.c diskfile.c reedsolomon.c \
        extern/md5.c extern/crc32.c extern/getopt.c \
        par2.c

OBJS   += $(SRCS:%.c=%.o)

CFLAGS = -pthread -Wall -g -O3 -std=c99

.PHONY: default clean

default: spar2_version.h
	gcc $(CFLAGS) $(SRCS) -o spar2

clean:
	rm -f $(OBJS) *.a *.lib *.exp spar2 spar2_version.h

spar2_version.h:
	./version.sh > spar2_version.h
