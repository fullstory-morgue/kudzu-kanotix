#############################################################################
#
#						Realmode X86 Emulator Library
#
#            	Copyright (C) 1996-1999 SciTech Software, Inc.
#
#  ========================================================================
#
#  Permission to use, copy, modify, distribute, and sell this software and
#  its documentation for any purpose is hereby granted without fee,
#  provided that the above copyright notice appear in all copies and that
#  both that copyright notice and this permission notice appear in
#  supporting documentation, and that the name of the authors not be used
#  in advertising or publicity pertaining to distribution of the software
#  without specific, written prior permission.  The authors makes no
#  representations about the suitability of this software for any purpose.
#  It is provided "as is" without express or implied warranty.
#
#  THE AUTHORS DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
#  INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
#  EVENT SHALL THE AUTHORS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
#  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
#  USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
#  OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
#  PERFORMANCE OF THIS SOFTWARE.
#
#  ========================================================================
#
# Descripton:   Linux specific makefile for the x86emu library.
#
#############################################################################

TARGETLIB = libx86emu.a

OBJS=\
debug.o \
decode.o \
fpu.o \
ops.o \
ops2.o \
prim_ops.o \
sys.o

all: $(TARGETLIB)

$(TARGETLIB): $(OBJS)
	ar rv $(TARGETLIB) $(OBJS)

INCS   = -I. -Ix86emu -I../../include -I../x86emu_include
CFLAGS = -g -Wall

.c.o:
	gcc -g -O -Wall -c $(CFLAGS) $(INCS) $*.c

.cpp.o:
	gcc -c $(CFLAGS) $(INCS) $*.cpp

distclean: clean

clean:
	rm -f *.a *.o

install:
