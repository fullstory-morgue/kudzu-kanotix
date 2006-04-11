ARCH := $(patsubst i%86,i386,$(shell uname -m))
ARCH := $(patsubst sparc%,sparc,$(ARCH))
ARCH := $(patsubst ppc%,ppc,$(ARCH))

CC = gcc
AR = ar
RANLIB = ranlib
CFLAGS = -Wall -O2 -g -fpic# -DDEBUG
DDC_OBJS = 
X86EMU =

ifeq ($(ARCH),i386)
CFLAGS += -Ix86emu -Ix86emu/x86emu
DDC_OBJS = vesamode.o common.o lrmi.o thunk.o vbe.o x86-common.o x86emu/debug.o x86emu/decode.o x86emu/fpu.o x86emu/ops2.o x86emu/ops.o x86emu/prim_ops.o x86emu/sys.o
X86EMU=x86emu/libx86emu.a
endif

ifeq ($(ARCH),x86_64)
CFLAGS += -Ix86emu -Ix86emu/x86emu
DDC_OBJS = vesamode.o common.o thunk.o vbe.o x86-common.o x86emu/debug.o x86emu/decode.o x86emu/fpu.o x86emu/ops2.o x86emu/ops.o x86emu/prim_ops.o x86emu/sys.o
X86EMU=x86emu/libx86emu.a
endif

ifeq ($(ARCH),ppc)
DDC_OBJS = vesamode.o common.o of.o ../minifind.o
endif

all: libvbe.a $(X86EMU)

%.o: %.c %.h
	$(CC) -c $(CFLAGS) -fpic -o $@ $<

libvbe.a: $(X86EMU) $(DDC_OBJS)
	$(AR) cru $@ $(DDC_OBJS)
	$(RANLIB) $@

x86emu/libx86emu.a:
	( cd x86emu ; make CFLAGS="-fpic $(RPM_OPT_FLAGS)" )

clean:
	$(RM) *.o *.a .depend core
	( cd x86emu ; make clean )
