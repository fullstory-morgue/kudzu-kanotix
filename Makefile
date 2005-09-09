VERSION:=$(shell awk '/Version:/ { print $$2 }' kudzu.spec)

CFLAGS= $(RPM_OPT_FLAGS) -Wall -D_GNU_SOURCE -g
LDFLAGS = 

prefix=$(DESTDIR)/usr
sysconfdir=$(DESTDIR)/etc
bindir=$(prefix)/bin
sbindir=$(prefix)/sbin
datadir=$(prefix)/share
mandir=$(datadir)/man
includedir=$(prefix)/include
libdir=$(prefix)/lib

CVSROOT:=$(shell cat CVS/Root 2>/dev/null || :)

CVSTAG = kudzu-r$(subst .,-,$(VERSION))

PYTHONVERS = $(shell ls /usr/include/python*/Python.h | sed "s|/usr/include/||g"| sed "s|/Python.h||g")

CFLAGS += -I. -DVERSION=\"$(VERSION)\"

ARCH := $(patsubst ppc64,ppc,$(patsubst sparc64,sparc,$(patsubst i%86,i386,$(shell uname -m))))

VBELIBS=
VBEOBJS=
ifeq (i386,$(ARCH))
VBELIBS=vbe
VBEOBJS=./ddcprobe/lrmi.o ./ddcprobe/vesamode.o ./ddcprobe/vbe.o ./ddcprobe/common.o
endif
ifeq (ppc,$(ARCH))
VBEOBJS=./ddcprobe/vesamode.o ./ddcprobe/vbe.o ./ddcprobe/of.o ./ddcprobe/common.o minifind.o
endif

all: $(VBELIBS) libkudzu.a libkudzu_loader.a _kudzumodule.so kudzu module_upgrade

AR = ar
RANLIB = ranlib

HEADERS = device.h alias.h adb.h ddc.h firewire.h ide.h isapnp.h keyboard.h kudzu.h macio.h misc.h modules.h \
	    	parallel.h pci.h pcmcia.h psaux.h usb.h sbus.h scsi.h serial.h
OBJS = kudzu.o modules.o pci.o pcmcia.o serial.o ide.o misc.o scsi.o parallel.o psaux.o usb.o sbus.o keyboard.o \
		ddc.o pciserial.o isapnp.o firewire.o adb.o macio.o vio.o s390.o alias.o
LOADEROBJ = kudzu_loader.o misc.o pci.o pcmcia.o scsi.o ide.o usb.o firewire.o alias.o

ifeq (sparc,$(ARCH))
LOADEROBJ += sbus.o
endif
ifeq (ppc,$(ARCH))
LOADEROBJ += minifind.o macio.o adb.o vio.o
endif
ifneq (,$(filter s390 s390x,$(ARCH)))
LOADEROBJ += s390.o
endif

KUDOBJS = hwconf.o 
LOADEROBJS = $(LOADEROBJ)

%.o: %.c %.h
	$(CC) -c $(CFLAGS) -fpic -o $@ $<

vbe:
	( cd ddcprobe ; make libvbe.a CFLAGS="-fpic $(RPM_OPT_FLAGS)" )

kudzu_loader.o: kudzu.c
	$(CC) -c $(CFLAGS) -D__LOADER__ -o $@ $<

libkudzu_loader.a: $(LOADEROBJS)
	$(AR) cr libkudzu_loader.a $(LOADEROBJS)
	$(RANLIB) libkudzu_loader.a

libkudzu.a: $(OBJS) $(VBEOBJS)
	$(AR) cr libkudzu.a $(OBJS) $(VBEOBJS)
	$(RANLIB) libkudzu.a

kudzu: libkudzu.a $(KUDOBJS) po
	$(CC) $(CFLAGS) $(LDFLAGS) $(KUDOBJS) -o kudzu -L. -lkudzu -L. -lpci -lpopt

updfstab: libkudzu.a updfstab.o po
	$(CC) $(CFLAGS) $(LDFLAGS) updfstab.o -o updfstab -L. -lkudzu -lpci -lpopt

module_upgrade: libkudzu.a module_upgrade.c
	$(CC) $(CFLAGS) $(LDFLAGS) module_upgrade.c -o module_upgrade -L. -lkudzu -lpci

_kudzumodule.so: libkudzu.a
	for ver in $(PYTHONVERS) ; do \
		if [ ! -f "$$ver/_kudzumodule.so" -o libkudzu.a -nt "$$ver/_kudzumodule.so" ]; then \
			mkdir -p $$ver ;\
			$(CC) $(CFLAGS) -I/usr/include/$$ver -fpic -c -o $$ver/kudzumodule.o kudzumodule.c ;\
	        	$(CC) -o $$ver/_kudzumodule.so $$ver/kudzumodule.o -shared -Wl,-soname,_kudzumodule.so -L. -lkudzu -lpci ;\
		fi ; \
	done

po:	dummy
	make -C po

install-program: kudzu module_upgrade
	install -m 755 -d $(sbindir)
	install -m 755 -d $(mandir)/man8
	install -m 755 kudzu $(sbindir)/kudzu
	install -m 755 module_upgrade $(sbindir)/module_upgrade
	install -m 755 -d $(sysconfdir)/rc.d/init.d
	install -m 755 -d $(sysconfdir)/sysconfig
	install -m 644 kudzu.8 $(mandir)/man8/kudzu.8
	install -m 644 module_upgrade.8 $(mandir)/man8/module_upgrade.8
	install -m 755 kudzu.init $(sysconfdir)/rc.d/init.d/kudzu
	install -m 644 kudzu.sysconfig $(sysconfdir)/sysconfig/kudzu
	install -m 755 -d $(libdir)
	install -m 644 libkudzu.a $(libdir)/libkudzu.a
	install -m 644 libkudzu_loader.a $(libdir)/libkudzu_loader.a
	install -m 755 -d $(includedir)/kudzu
	for header in $(HEADERS) ; do \
		install -m 644 $$header $(includedir)/kudzu/$$header ; \
	done
	make -C po instroot=$(prefix) install

install-updfstab:
	install -m 755 -d $(sbindir)
	install -m 755 -d $(mandir)/man8
	install -m 755 updfstab $(sbindir)/updfstab
	install -m 644 updfstab.8 $(mandir)/man8/updfstab.8

install:  _kudzumodule.so
	for ver in $(PYTHONVERS) ; do \
	 mkdir -p $(libdir)/$$ver/site-packages ; \
	 install -m 755 $$ver/_kudzumodule.so $(libdir)/$$ver/site-packages ;\
	 install -m 644 kudzu.py $(libdir)/$$ver/site-packages ;\
	 if [ $$ver = "python1.5" ]; then \
	   python -c 'from compileall import *; compile_dir("'$$DESTDIR'/usr/lib/'$$ver'",10,"/usr/lib/'$$ver'")'  || :;\
	 else \
	   python2 -c 'from compileall import *; compile_dir("'$$DESTDIR'/usr/lib/'$$ver'",10,"/usr/lib/'$$ver'")' || python -c 'from compileall import *; compile_dir("'$$DESTDIR'/usr/lib/'$$ver'",10,"/usr/lib/'$$ver'")' ; \
	 fi ;\
	done

tag-archive:
	@cvs -Q tag -F $(CVSTAG)

create-archive:
	@rm -rf /tmp/kudzu
	@cd /tmp ; cvs -Q -d $(CVSROOT) export -r$(CVSTAG) kudzu || echo "Um... export aborted."
	@mv /tmp/kudzu /tmp/kudzu-$(VERSION)
	@cd /tmp ; tar -czSpf kudzu-$(VERSION).tar.gz kudzu-$(VERSION)
	@rm -rf /tmp/kudzu-$(VERSION)
	@cp /tmp/kudzu-$(VERSION).tar.gz .
	@rm -f /tmp/kudzu-$(VERSION).tar.gz
	@echo ""
	@echo "The final archive is in kudzu-$(VERSION).tar.gz"

archive: clean tag-archive create-archive

clean:
	rm -f *.o *.do *.so *.pyc updfstab kudzu modules module_upgrade *.a core *~ *.tar.gz
	rm -rf python*
	( cd ddcprobe ; make clean )
	( cd po ; make clean )

dummy:
