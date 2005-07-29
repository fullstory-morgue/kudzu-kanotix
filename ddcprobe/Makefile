ARCH := $(patsubst i%86,i386,$(shell uname -m))
ARCH := $(patsubst sparc%,sparc,$(ARCH))
ARCH := $(patsubst ppc%,ppc,$(ARCH))

CC = gcc
CFLAGS = -Wall -O2 -g# -DDEBUG
TARGETS = ddcprobe ddcxinfo 
DDC_OBJS = vesamode.o common.o
DDC_LIBS =

ifeq ($(ARCH),i386)
TARGETS += svgamodes modetest libvbe.a
DDC_OBJS += lrmi.o vbe.o
endif

ifeq ($(ARCH),ppc)
DDC_OBJS += of.o ../minifind.o
endif

ifeq (.depend,$(wildcard .depend))
TARGET=all
else
TARGET=depend all
endif

everything: $(TARGET)

all: $(TARGETS)

install: $(TARGETS)
	cp -a ddcprobe $(DESTDIR)/usr/sbin/ddcprobe

 
# i386 and ppc targets
ddcprobe: $(DDC_OBJS) ddcprobe.o
	$(CC) $(CFLAGS) -o ddcprobe $(DDC_OBJS) ddcprobe.o $(DDC_LIBS)
ddcxinfo: $(DDC_OBJS) ddcxinfo.o
	$(CC) $(CFLAGS) -o ddcxinfo $(DDC_OBJS) ddcxinfo.o $(DDC_LIBS)

# i386 only targets
svgamodes: lrmi.o vesamode.o vbe.o svgamodes.o
modetest: lrmi.o vesamode.o vbe.o modetest.o
libvbe.a: lrmi.o vesamode.o vbe.o
	$(AR) cru $@ $^

install-lib: $(prefix)/include/vbe.h $(prefix)/lib/libvbe.a

$(prefix)/include/vbe.h:
	install -m 644 vbe.h $(prefix)/include/vbe.h

$(prefix)/lib/libvbe.a:
	install -m 644 libvbe.a $(prefix)/lib/libvbe.a

clean:
	$(RM) $(TARGETS) *.o .depend core

depend:
	$(CPP) -M $(CFLAGS) *.c > .depend

ifeq (.depend,$(wildcard .depend))
include .depend
endif                                           
