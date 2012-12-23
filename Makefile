# Makefile

include config.mak

.PHONY: default clean

all: default
default:

SRCS  = common.c diskfile.c reedsolomon.c \
        extern/md5.c extern/crc32.c \
        par2.c

OBJS =
OBJCLI =

CONFIG := $(shell cat config.h)

# MMX/SSE optims
ifneq ($(AS),)
ASMSRC = x86/doblock-a.asm

ifeq ($(ARCH),X86)
ARCH_X86 = yes
ASFLAGS += -DARCH_X86_64=0
endif

ifeq ($(ARCH),X86_64)
ARCH_X86 = yes
ASFLAGS += -DARCH_X86_64=1
endif

ifdef ARCH_X86
ASFLAGS += -I./x86/
OBJASM  = $(ASMSRC:%.asm=%.o)
$(OBJASM): x86/x86inc.asm
endif
endif



ifneq ($(HAVE_GETOPT_LONG),1)
SRCCLI += extern/getopt.c
endif

OBJS   += $(SRCS:%.c=%.o)
OBJCLI += $(SRCCLI:%.c=%.o)

.PHONY: all default clean distclean install uninstall lib-static cli

cli: spar2$(EXE)
lib-static: $(LIBSPAR2)

$(LIBSPAR2): .depend $(OBJS) $(OBJASM)
	rm -f $(LIBSPAR2)
	$(AR)$@ $(OBJS) $(OBJASM)
	$(if $(RANLIB), $(RANLIB) $@)

ifneq ($(EXE),)
.PHONY: spar2
spar2: spar2$(EXE)
endif

spar2$(EXE): .depend $(OBJS) $(OBJCLI) $(OBJASM)
	$(LD)$@ $(OBJS) $(OBJASM) $(OBJCLI) $(LDFLAGS)

$(OBJS) $(OBJASM) $(OBJCLI): .depend

%.o: %.asm
	$(AS) $(ASFLAGS) -o $@ $<
	-@ $(if $(STRIP), $(STRIP) -x $@) # delete local/anonymous symbols, so they don't show up in oprofile

%.o: %.S
	$(AS) $(ASFLAGS) -o $@ $<
	-@ $(if $(STRIP), $(STRIP) -x $@) # delete local/anonymous symbols, so they don't show up in oprofile

%.o: %.rc par2.h
	$(RC)$@ $<

.depend: config.mak
	@rm -f .depend
	@$(foreach SRC, $(SRCS) $(SRCCLI), $(CC) $(CFLAGS) $(SRC) $(DEPMT) $(SRC:%.c=%.o) $(DEPMM) 1>> .depend;)

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
	rm -f $(OBJS) $(OBJASM) $(OBJCLI) *.a *.lib *.exp *.pdb spar2 spar2.exe .depend
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
