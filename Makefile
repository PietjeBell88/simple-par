# Makefile

include config.mak

.PHONY: default clean

default: spar2$(EXE)

all: default
default:

SRCS  = common.c diskfile.c reedsolomon.c \
        extern/md5.c extern/crc32.c extern/getopt.c \
        par2.c

OBJS =

CONFIG := $(shell cat config.h)

ifneq ($(HAVE_GETOPT_LONG),1)
SRCCLI += extras/getopt.c
endif

OBJS   += $(SRCS:%.c=%.o)

.PHONY: all default clean distclean install uninstall

ifneq ($(EXE),)
.PHONY: spar2
spar2: spar2$(EXE)
endif

spar2$(EXE): .depend $(OBJS)
	$(LD)$@ $(OBJS) $(LDFLAGS)

$(OBJS): .depend

%.o: %.rc par2.h
	$(RC)$@ $<

.depend: config.mak
	@rm -f .depend
	@$(foreach SRC, $(SRCS), $(CC) $(CFLAGS) $(SRC) $(DEPMT) $(SRC:%.c=%.o) $(DEPMM) 1>> .depend;)

config.mak:
	./configure

depend: .depend
ifneq ($(wildcard .depend),)
include .depend
endif

# These should cover most of the important codepaths
OPT0 = -s640000 -r100 -m1
OPT1 = -s640000 -r100

ifeq (,$(INFILE))
fprofiled:
	@echo 'usage: make fprofiled INFILE="infile1 infile2 ..."'
else
fprofiled:
	$(MAKE) clean
	$(MAKE) spar2$(EXE) CFLAGS="$(CFLAGS) $(PROF_GEN_CC)" LDFLAGS="$(LDFLAGS) $(PROF_GEN_LD)"
	$(foreach I, 0 1, ./spar2$(EXE) $(OPT$I) --threads 1 $(DEVNULL) $(INFILE);)
	rm -f $(OBJS)
	$(MAKE) CFLAGS="$(CFLAGS) $(PROF_USE_CC)" LDFLAGS="$(LDFLAGS) $(PROF_USE_LD)"
	rm -f $(SRCS:%.c=%.gcda) $(SRCS:%.c=%.gcno) *.dyn pgopti.dpi pgopti.dpi.lock
endif

clean:
	rm -f $(OBJS) *.a *.lib *.exp *.pdb spar2 spar2.exe .depend
	rm -f $(SRCS:%.c=%.gcda) $(SRCS	:%.c=%.gcno) *.dyn pgopti.dpi pgopti.dpi.lock

distclean: clean
	rm -f config.mak spar2_version.h spar2.pc config.log config.h

install:
	install -d $(DESTDIR)$(bindir)
	install spar2$(EXE) $(DESTDIR)$(bindir)

uninstall:
	rm -f $(DESTDIR)$(bindir)/spar2$(EXE)

etags: TAGS

TAGS:
	etags $(SRCS)
