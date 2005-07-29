/* Copyright 1999-2003 Red Hat, Inc.
 *
 * This software may be freely redistributed under the terms of the GNU
 * public license.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <ctype.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

#include <sys/stat.h>
#include <sys/utsname.h>

#include <pci/pci.h>

#include "pci.h"
#include "pciserial.h"

static struct pciDevice * pciDeviceList = NULL;
static int numPciDevices = 0;
static struct pci_access *pacc=NULL;
static jmp_buf pcibuf;
static char *pcifiledir = NULL;
static void pciWriteDevice(FILE *file, struct pciDevice *dev);

static int devCmp(const void * a, const void * b) {
    const struct pciDevice * one = a;
    const struct pciDevice * two = b;
    int x=0,y=0,z=0,xx=0,yy=0,zz=0;
    
    x = (one->vendorId - two->vendorId);
    xx = (one->subVendorId - two->subVendorId);
    y = (one->deviceId - two->deviceId);
    yy = (one->subDeviceId - two->subDeviceId);
    if (one->pciType && two->pciType)
	  z = (one->pciType - two->pciType);
    if (one->pcidom || two->pcidom) {
	  unsigned int mask;
	  
	  mask = one->pcibus ? one->pcibus : two->pcibus;
	  zz = ((one->pcidom & mask) - (two->pcidom & mask));
    }
    if (x)
      return x;
    else if (y)
      return y;
    else if (xx)
      return xx;
    else if (yy)
      return yy;
    else if (zz)
      return zz;
    else
      return z;
}

static int devCmp2(const void * a, const void * b) {
    const struct pciDevice * one = a;
    const struct pciDevice * two = b;
    int x=0,y=0,z=0,xx=0,yy=0;
    
    x = (one->vendorId - two->vendorId);
    xx = (one->subVendorId - two->subVendorId);
    y = (one->deviceId - two->deviceId);
    yy = (one->subDeviceId - two->subDeviceId);
    if (one->pciType && two->pciType)
	  z = (one->pciType - two->pciType);
    if (x)
      return x;
    else if (y)
      return y;
    else if (one->subVendorId != 0xffff && two->subVendorId != 0xffff) {
	    if (xx)
	      return xx;
	    else if (yy)
	      return yy;
    } 
	return z;
}

static int devCmp3(const void * a, const void * b) {
    const struct pciDevice * one = a;
    const struct pciDevice * two = b;
    int x=0,y=0,z=0,xx=0,yy=0,zz=0;
    
    x = (one->vendorId - two->vendorId);
    xx = (one->subVendorId - two->subVendorId);
    y = (one->deviceId - two->deviceId);
    yy = (one->subDeviceId - two->subDeviceId);
    if (one->pciType && two->pciType)
	  z = (one->pciType - two->pciType);
    if (one->pcidom && two->pcidom) {
	  unsigned int mask;
	  
	  mask = one->pcibus ? one->pcibus : two->pcibus;
	  zz = ((one->pcidom & mask) - (two->pcidom & mask));
    }
    if (x)
      return x;
    else if (y)
      return y;
    else if (xx)
      return xx;
    else if (yy)
      return yy;
    else if (zz)
      return zz;
    else
      return z;
}


static void pciFreeDevice(struct pciDevice *dev) {
    freeDevice((struct device *)dev);
}

static void pciWriteDevice(FILE *file, struct pciDevice *dev) {
	writeDevice(file, (struct device *)dev);
	fprintf(file,"vendorId: %04x\ndeviceId: %04x\nsubVendorId: %04x\nsubDeviceId: %04x\npciType: %d\npcidom: %4x\npcibus: %2x\npcidev: %2x\npcifn: %2x\n",
		dev->vendorId,dev->deviceId,dev->subVendorId,dev->subDeviceId,dev->pciType,dev->pcidom,dev->pcibus,dev->pcidev,dev->pcifn);
}

static int pciCompareDevice(struct pciDevice *dev1, struct pciDevice *dev2)
{
	int x,y;
	
	x = compareDevice((struct device *)dev1,(struct device *)dev2);
	if (x && x!=2) 
	  return x;
	if ((y=devCmp2( (void *)dev1, (void *)dev2 )))
	  return y;
	return x;
}
			    
struct pciDevice * pciNewDevice(struct pciDevice *dev) {
    struct pciDevice *ret;
    
    ret = malloc(sizeof(struct pciDevice));
    memset(ret,'\0',sizeof(struct pciDevice));
    ret=(struct pciDevice *)newDevice((struct device *)dev,(struct device *)ret);
    ret->bus = BUS_PCI;
    /* Older entries will come in with subvendor & subdevice IDs of 0x0.
     * 0x0 is a *valid* entry, though, so set it to something invalid. */
    ret->subVendorId = 0xffff;
    if (dev && dev->bus == BUS_PCI) {
	ret->vendorId = dev->vendorId;
	ret->deviceId = dev->deviceId;
	ret->pciType = dev->pciType;
	ret->subVendorId = dev->subVendorId;
        ret->subDeviceId = dev->subDeviceId;
	ret->pcidom = dev->pcidom;
	ret->pcibus = dev->pcibus;
	ret->pcidev = dev->pcidev;
	ret->pcifn = dev->pcifn;
    } else {
	ret->pciType = PCI_UNKNOWN;
    }
    ret->newDevice = pciNewDevice;
    ret->freeDevice = pciFreeDevice;
    ret->writeDevice = pciWriteDevice;
    ret->compareDevice = pciCompareDevice;
    return ret;
}

static unsigned int kudzuToPci(enum deviceClass class) {
    switch (class) {
     case CLASS_UNSPEC:
	return 0;
     case CLASS_OTHER:
	return 0;
     case CLASS_NETWORK:
	return PCI_BASE_CLASS_NETWORK;
     case CLASS_VIDEO:
	return PCI_BASE_CLASS_DISPLAY;
     case CLASS_AUDIO:
	return PCI_CLASS_MULTIMEDIA_AUDIO;
     case CLASS_SCSI:
	return PCI_CLASS_STORAGE_SCSI;
     case CLASS_FLOPPY:
	return PCI_CLASS_STORAGE_FLOPPY;
     case CLASS_RAID:
	return PCI_CLASS_STORAGE_RAID;
     case CLASS_CAPTURE:
	return PCI_CLASS_MULTIMEDIA_VIDEO;
     case CLASS_MODEM:
	return PCI_CLASS_COMMUNICATION_SERIAL;
     case CLASS_MOUSE: /* !?!? */
	return PCI_CLASS_INPUT_MOUSE;
     case CLASS_USB:
	return PCI_CLASS_SERIAL_USB;
     case CLASS_FIREWIRE:
	return PCI_CLASS_SERIAL_FIREWIRE;
     case CLASS_SOCKET:
	return PCI_CLASS_BRIDGE_CARDBUS;
     case CLASS_IDE:
	return PCI_CLASS_STORAGE_IDE;
     default:
	return 0;
    }
}

static enum deviceClass pciToKudzu(unsigned int class) {
    
    if (!class) return CLASS_UNSPEC;
    switch (class >> 8) {
     case PCI_BASE_CLASS_DISPLAY:
	return CLASS_VIDEO;
    case PCI_BASE_CLASS_NETWORK:
	return CLASS_NETWORK;
     default:
	break;
    }
    switch (class) {
     case PCI_CLASS_STORAGE_SCSI:
     case PCI_CLASS_SERIAL_FIBER:
	return CLASS_SCSI;
     case PCI_CLASS_STORAGE_FLOPPY:
	return CLASS_FLOPPY;
     case PCI_CLASS_STORAGE_RAID:
	return CLASS_RAID;
     case PCI_CLASS_MULTIMEDIA_AUDIO:
     /* HD Audio */
     case 0x0403:
	return CLASS_AUDIO;
     case PCI_CLASS_INPUT_MOUSE:
	return CLASS_MOUSE;
     case PCI_CLASS_MULTIMEDIA_OTHER:
     case PCI_CLASS_MULTIMEDIA_VIDEO:
	return CLASS_CAPTURE;
     case PCI_CLASS_COMMUNICATION_SERIAL:
     case PCI_CLASS_COMMUNICATION_OTHER:
	return CLASS_MODEM;
     case PCI_CLASS_NOT_DEFINED_VGA:
	return CLASS_VIDEO;
     /* Fix for one of the megaraid variants.
      * It claims to be an I2O controller. */
     case 0x0e00:
	return CLASS_SCSI;
     case PCI_CLASS_SERIAL_USB:
	return CLASS_USB;
     case PCI_CLASS_SERIAL_FIREWIRE:
	return CLASS_FIREWIRE;
     case PCI_CLASS_BRIDGE_CARDBUS:
	return CLASS_SOCKET;
     case PCI_CLASS_NETWORK_ETHERNET:
	return CLASS_NETWORK;
     case PCI_CLASS_NETWORK_TOKEN_RING:
	return CLASS_NETWORK;
     case PCI_CLASS_NETWORK_FDDI:
	return CLASS_NETWORK;
     case PCI_CLASS_NETWORK_ATM:
	return CLASS_NETWORK;
     case PCI_CLASS_STORAGE_IDE:
	return CLASS_IDE;
     default:
	return CLASS_OTHER;
    }
}


int pciReadDrivers(char *filename) {
	int fd;
	char * buf;
	int numDrivers;
	int oldlength;
	int vendid, devid, subvendid, subdevid;
	int merge = 0;
	char * start, *ptr, *bufptr;
	struct pciDevice * nextDevice, *tmpdev = NULL, key;
	char *module;
	char path[256];
	struct utsname utsbuf;

	if (filename) {
		pcifiledir = dirname(strdup(filename));
		fd = open(filename, O_RDONLY);
		if (fd < 0)
		  goto pcimap;
	} else {
		fd = open("/usr/share/hwdata/pcitable", O_RDONLY);
		if (fd < 0) {
			fd = open("/etc/pcitable", O_RDONLY);
			if (fd < 0) {
				fd = open("/modules/pcitable", O_RDONLY);
				if (fd < 0) {
					fd = open("./pcitable", O_RDONLY);
					if (fd < 0)
					  goto pcimap;
				}
			}
		}
	}
	buf = bufFromFd(fd);
	if (!buf) goto pcimap;
	

	/* upper bound */
	numDrivers = 1;
	start = buf;
	while ((start = strchr(start, '\n'))) {
		numDrivers++;
		start++;
	}
    
	if (pciDeviceList)
	  merge = 1;

	pciDeviceList = realloc(pciDeviceList, sizeof(*pciDeviceList) *
				(numPciDevices + numDrivers));
	nextDevice = pciDeviceList + numPciDevices;

	start = buf;
	while (start && *start) {
		while (isspace(*start)) start++;
		if (*start != '#' && *start != '\n') {
			vendid = strtoul(start,&ptr,16);
			if (!(*ptr && *ptr != '\n'))
			  continue;
			start = ptr+1;
			devid = strtoul(start,&ptr,16);	
			if (!(*ptr && *ptr != '\n'))
			  continue;
			start = ptr+1;
			subvendid = strtoul(start,&ptr,16);
			if (start != ptr) {
				if (!(*ptr && *ptr != '\n'))
				  continue;
				start = ptr+1;
				subdevid = strtoul(start,&ptr,16);
			} else {
				subvendid = 0xffff;
				subdevid = 0;
			}
			while(*ptr && *ptr != '"' && *ptr != '\n') ptr++;
			if (*ptr != '"' ) continue;
			ptr++;
			start=ptr;
			while(*ptr && *ptr != '"' && *ptr != '\n') ptr++;
			if (*ptr != '"' ) continue;
			*ptr = '\0';
			module = strdup(start);
			ptr++;
			start=ptr;

			if (merge) {
				tmpdev = nextDevice;
				key.vendorId = vendid;
				key.deviceId = devid;

				key.subVendorId = subvendid;
				key.subDeviceId = subdevid;
				if (strncmp (module, "CardBus:", 8) == 0)
				  key.pciType = PCI_CARDBUS;
				else
				  key.pciType = PCI_NORMAL;
				key.pcidom = key.pcibus = 0;
				
				nextDevice = bsearch(&key,pciDeviceList,numPciDevices,
						     sizeof(struct pciDevice), devCmp);
				if (!nextDevice) {
					nextDevice = tmpdev;
					tmpdev = NULL;
					numPciDevices++;
				} else {
					if (nextDevice->device) free(nextDevice->device);
					if (nextDevice->driver) free(nextDevice->driver);
				}
			} else
			  numPciDevices++;
			nextDevice->vendorId = vendid;
			nextDevice->deviceId = devid;
			nextDevice->subVendorId = subvendid;
			nextDevice->subDeviceId = subdevid;
			if (strncmp (module, "CardBus:", 8) == 0) {
				nextDevice->pciType = PCI_CARDBUS;
				nextDevice->driver = strdup(&module [8]);
			} else {
				nextDevice->pciType = PCI_NORMAL;
				nextDevice->driver = strdup(module);
			}
			nextDevice->pcidom = nextDevice->pcibus = 0;
			nextDevice->desc = NULL;
			nextDevice->next = NULL;
			nextDevice->device = NULL;
			nextDevice->type = 0;
			nextDevice->bus = BUS_PCI;
			if (merge && tmpdev)
			  nextDevice = tmpdev;
			else {
				nextDevice++;
				if (merge)
				  qsort(pciDeviceList, numPciDevices, sizeof(*pciDeviceList), devCmp);
			}
			free(module);
		}

		while (*start && *start != '\n') start++;
		if (start) start++;
	}
	free(buf);

	qsort(pciDeviceList, numPciDevices, sizeof(*pciDeviceList), devCmp);

pcimap:
	fd = -1;
	/* OK, now read the modutils modules.pcimap file. */
	if (filename) {
		snprintf(path,255,"%s/modules.pcimap",pcifiledir);
		fd = open(path, O_RDONLY);
		if (fd < 0)
			return 0;
	} else {
		uname(&utsbuf);
		if (strstr(utsbuf.release,"BOOT")) {
			char kernelver[64];
			int len;
			
			len = strstr(utsbuf.release,"BOOT")-utsbuf.release;
			strncpy(kernelver,utsbuf.release,len);
			kernelver[len] = '\0';
			snprintf(path,255,"/lib/modules/%s/modules.pcimap", kernelver);
		} else {
			snprintf(path,255,"/lib/modules/%s/modules.pcimap", utsbuf.release);
		}
		fd = open(path, O_RDONLY);
		if (fd < 0) {
			fd = open("/etc/modules.pcimap",O_RDONLY);
			if (fd < 0) {
				fd = open("/modules/modules.pcimap",O_RDONLY);
				if (fd < 0) {
					fd = open("./modules.pcimap",O_RDONLY);
					if (fd < 0)
					  return 0;
				}
			}
		}
	}
	bufptr = bufFromFd(fd);
	if (!bufptr) return 0;

	start = buf = bufptr;
	numDrivers = 0;
	while ((start = strchr(start, '\n'))) {
		numDrivers++;
		start++;
	}
	pciDeviceList = realloc(pciDeviceList, sizeof(*pciDeviceList) *
				(numPciDevices + numDrivers));
	oldlength = numPciDevices;
	nextDevice = pciDeviceList + numPciDevices;

	start = buf = bufptr;
	while (start && *start) {
		int pclass, pclassmask;
		
		while (*buf && *buf != '\n') buf++;
		if (*buf) {
			*buf = '\0';
			buf++;
		}
		if (*start == '#') {
			start = buf;
			continue;
		}
		ptr = start;
		while (*ptr && *ptr != ' ') ptr++;
		if (*ptr) {
			*ptr = '\0';
			ptr++;
		}
		module = strdup(start);
		while (*ptr && *ptr == ' ') ptr++;
		if (!*ptr) {
			start = buf;
			free(module);
			continue;
		}
		start = ptr;
		while (*ptr && *ptr != ' ') ptr++;
		if (*ptr) {
			*ptr = '\0';
			ptr++;
		}
		vendid = strtoul(start,(char **)NULL,16);
		if (vendid == 0xffffffff)
			vendid = 0xffff;
		while (*ptr && *ptr == ' ') ptr++;
		if (!*ptr) {
			start = buf;
			free(module);
			continue;
		}
		start = ptr;
		while (*ptr && *ptr != ' ') ptr++;
		if (*ptr) {
			*ptr = '\0';
			ptr++;
		}
		devid = strtoul(start, (char **)NULL, 16);
		if (devid == 0xffffffff)
			devid = 0xffff;
		while (*ptr && *ptr == ' ') ptr++;
		if (!*ptr) {
			start = buf;
			free(module);
			continue;
		}
		start = ptr;
		while (*ptr && *ptr != ' ') ptr++;
		if (*ptr) {
			*ptr = '\0';
			ptr++;
		}
		subvendid = strtoul(start, (char **)NULL, 16);
		if (subvendid == 0xffffffff)
			subvendid = 0xffff;
		while (*ptr && *ptr == ' ') ptr++;
		if (!*ptr) {
			start = buf;
			free(module);
			continue;
		}
		start = ptr;
		while (*ptr && *ptr != ' ') ptr++;
		if (*ptr) {
			*ptr = '\0';
			ptr++;
		}
		subdevid = strtoul(start, (char **)NULL, 16);
		if (subdevid == 0xffffffff)
			subdevid = 0;
		while (*ptr && *ptr == ' ') ptr++;
		if (!*ptr) {
			start = buf;
			free(module);
			continue;
		}
		start = ptr;
		while (*ptr && *ptr != ' ') ptr++;
		if (*ptr) {
			*ptr = '\0';
			ptr++;
		}
		pclass = strtoul(start, (char **)NULL, 16);
		while (*ptr && *ptr == ' ') ptr++;
		if (!*ptr) {
			start = buf;
			free(module);
			continue;
		}
		start = ptr;
		while (*ptr && *ptr != ' ') ptr++;
		if (*ptr) {
			*ptr = '\0';
			ptr++;
		}
		pclassmask = strtoul(start, (char **)NULL, 16);
		start = buf;
		
		tmpdev = nextDevice;
		key.vendorId = vendid;
		key.deviceId = devid;
		key.subVendorId = subvendid;
		key.subDeviceId = subdevid;
		/* overload */
		key.pcidom = pclass;
		key.pcibus = pclassmask;
		
		key.pciType = PCI_NORMAL;
		nextDevice = bsearch(&key,pciDeviceList,oldlength,
				     sizeof(struct pciDevice), devCmp);
		/* HACK */
		if (nextDevice && !strcmp(nextDevice->driver, "bcm5700"))
			nextDevice = NULL;
		/* HACK */
		if (nextDevice && !strcmp(nextDevice->driver, "eepro100"))
			nextDevice = NULL;
		if (!nextDevice) {
			key.vendorId = vendid;
			key.deviceId = devid;
			key.subVendorId = 0xffff;
			key.subDeviceId = 0;
			key.pciType = PCI_NORMAL;
			nextDevice = bsearch(&key, pciDeviceList, oldlength,
					     sizeof (struct pciDevice), devCmp);
		}
		if (!nextDevice) {
			nextDevice = tmpdev;
			tmpdev = NULL;
			numPciDevices++;
			nextDevice->vendorId = vendid;
			nextDevice->deviceId = devid;
			nextDevice->subVendorId = subvendid;
			nextDevice->subDeviceId = subdevid;
			nextDevice->pciType = PCI_NORMAL;
			nextDevice->driver = strdup(module);
			nextDevice->pcidom = pclass;
			nextDevice->pcibus = pclassmask;
			nextDevice->desc = NULL;
			nextDevice->next = NULL;
			nextDevice->device = NULL;
			nextDevice->type = 0;
			nextDevice->bus = BUS_PCI;
		} else {
			if (!strcmp(nextDevice->driver,"unknown") ||
			    !strcmp(nextDevice->driver,"disabled") ||
			    !strcmp(nextDevice->driver,"ignore")) {
				free(nextDevice->driver);
				nextDevice->driver = strdup(module);
			}
		}
		if (tmpdev)
			nextDevice = tmpdev;
		else {
			nextDevice++;
		}
		free(module);
	}
	qsort(pciDeviceList, numPciDevices, sizeof(*pciDeviceList), devCmp);
	free(bufptr);
	return 0;
}

void pciFreeDrivers(void) {
	int x;
	
	if (pciDeviceList) {
		for (x=0;x<numPciDevices;x++) {
			if (pciDeviceList[x].device) free (pciDeviceList[x].device);
			if (pciDeviceList[x].driver) free (pciDeviceList[x].driver);
		}
		free(pciDeviceList);
		pciDeviceList=NULL;
		numPciDevices=0;
	}
}

static struct pciDevice * pciGetDeviceInfo(unsigned int vend, unsigned int dev, 
				    unsigned int subvend, unsigned int subdev,
				    int bus, unsigned int pclass) {
    struct pciDevice *searchDev, key;
    
    key.vendorId = vend;
    key.deviceId = dev;
    key.pciType = bus;
    key.subVendorId = subvend;
    key.subDeviceId = subdev;
    key.pcidom = pclass;
    key.pcibus = 0;
    
    searchDev = bsearch(&key,pciDeviceList,numPciDevices,
			sizeof(struct pciDevice), devCmp3);
    if (!searchDev && key.pciType != PCI_NORMAL) {
	key.pciType = PCI_NORMAL;
        searchDev = bsearch(&key,pciDeviceList,numPciDevices,
			    sizeof(struct pciDevice), devCmp3);
    }
    if (!searchDev) {
	    key.pciType = bus;
	    key.subVendorId = 0xffff;
	    key.subDeviceId = 0;
	    searchDev = bsearch(&key,pciDeviceList,numPciDevices,
				sizeof(struct pciDevice), devCmp3);
    }
    if (!searchDev && key.pciType != PCI_NORMAL) {
	    key.pciType = PCI_NORMAL;
	    searchDev = bsearch(&key,pciDeviceList,numPciDevices,
				sizeof(struct pciDevice), devCmp3);
    }
    if (!searchDev) {
	    key.pciType = bus;
	    key.deviceId = 0xffff;
	    searchDev = bsearch(&key,pciDeviceList,numPciDevices,
				sizeof(struct pciDevice), devCmp3);
    }
    if (!searchDev && key.pciType != PCI_NORMAL) {
	    key.pciType = PCI_NORMAL;
	    searchDev = bsearch(&key,pciDeviceList,numPciDevices,
				sizeof(struct pciDevice), devCmp3);
    }
    if (!searchDev) {
	    key.pciType = bus;
	    key.vendorId = 0xffff;
	    searchDev = bsearch(&key,pciDeviceList,numPciDevices,
				sizeof(struct pciDevice), devCmp3);
    }
    if (!searchDev && key.pciType != PCI_NORMAL) {
	    key.pciType = PCI_NORMAL;
	    searchDev = bsearch(&key,pciDeviceList,numPciDevices,
				sizeof(struct pciDevice), devCmp3);
    }
    if (!searchDev) {
	searchDev = pciNewDevice(NULL);
	searchDev->vendorId = vend;
	searchDev->deviceId = dev;
	searchDev->pciType = bus;
	searchDev->subVendorId = subvend;
	searchDev->subDeviceId = subdev;
	searchDev->driver = strdup("unknown");
    } else {
	searchDev->desc = NULL;
	searchDev = pciNewDevice(searchDev);
	searchDev->pciType = bus;
	searchDev->subVendorId = subvend;
	searchDev->subDeviceId = subdev;
	searchDev->vendorId = vend;
	searchDev->deviceId = dev;
    }
    return searchDev;
}

static void pcinull(char * foo, ...)
{
}

static void pcibail(char * foo, ...)
{
    longjmp(pcibuf,1);
}

static int isDisabled(struct pci_dev *p, u_int8_t config[256]) {
	int disabled;
	int i;
#ifdef __i386__
	int limit = 6;
#else
	int limit = 1;
#endif	
	unsigned int devtype, command;
	
	devtype = p->device_class;
	if (p->irq || pciToKudzu(devtype) != CLASS_VIDEO) {
		return 0;
	}
	/* Only check video cards for now. */
	command = config[PCI_COMMAND];
	disabled = 0;
	for (i = 0; i < limit ; i++) {
		int x = PCI_BASE_ADDRESS_0 + 4 * i;
		pciaddr_t pos = p->base_addr[i];
		pciaddr_t len = (p->known_fields & PCI_FILL_SIZES) ? p->size[i] : 0;
		u32 flag = config[x+3] << 24 | config[x+2] << 16 | config[x+1] << 8 | config[x];
		if ((flag == 0xffffffff || !flag) && !pos && !len)
		  continue;
		disabled++;
		if ((flag & PCI_BASE_ADDRESS_SPACE_IO) && (command & PCI_COMMAND_IO))
		  disabled--;
		else if (command & PCI_COMMAND_MEMORY)
		  disabled--;			
	}
	return disabled;
}

struct device * pciProbe(enum deviceClass probeClass, int probeFlags, struct device *devlist) {
    struct pci_dev *p;
    /* This should be plenty. */
    int cardbus_bridges[32];
    int bridgenum = 0;
    int init_list = 0;
    char pcifile[128];
    unsigned int type = kudzuToPci(probeClass),devtype;
    
    if (
	(probeClass & CLASS_OTHER) ||
	(probeClass & CLASS_NETWORK) ||
	(probeClass & CLASS_SCSI) ||
	(probeClass & CLASS_IDE) ||
	(probeClass & CLASS_VIDEO) ||
	(probeClass & CLASS_AUDIO) ||
	(probeClass & CLASS_MODEM) ||
	(probeClass & CLASS_USB) ||
	(probeClass & CLASS_FIREWIRE) ||
	(probeClass & CLASS_SOCKET) ||
	(probeClass & CLASS_CAPTURE) ||
	(probeClass & CLASS_RAID)) {
	pacc = pci_alloc();
	if (!pacc) return devlist;
	if (!pciDeviceList) {
		pciReadDrivers(NULL);
		init_list = 1;
	}
	pacc->debug=pacc->warning=pcinull;
	pacc->error=pcibail;
	if (!access("/usr/share/hwdata/pci.ids", R_OK))
		    pacc->id_file_name = "/usr/share/hwdata/pci.ids";
	else if (!access("/etc/pci.ids", R_OK))
		    pacc->id_file_name = "/etc/pci.ids";
	else if (!access("/modules/pci.ids", R_OK))
		    pacc->id_file_name = "/modules/pci.ids";
	else if (!access("./pci.ids", R_OK))
		    pacc->id_file_name = "./pci.ids";
	else if (pcifiledir) {
		snprintf(pcifile,128,"%s/pci.ids",pcifiledir);
		if (!access(pcifile, R_OK))
			pacc->id_file_name = pcifile;
	}
		
		    
	if (!setjmp(pcibuf)) {
	    int order=0;
		
	    pci_init(pacc);
	    pci_scan_bus(pacc);
		
	    memset(cardbus_bridges,'\0',32);
	    /* enumerate cardbus bridges first */
	    for (p = pacc->devices; p; p=p->next) {
		u_int8_t config[256];
		
		memset(config,'\0',256);
	        pci_read_block(p, 0, config, 64);
		if ((config[PCI_HEADER_TYPE] & 0x7f) == PCI_HEADER_TYPE_CARDBUS) {
		    /* Cardbus bridge */
		    pci_read_block(p, 64, config+64, 64);
		    for (bridgenum=0; cardbus_bridges[bridgenum];bridgenum++);
		    cardbus_bridges[bridgenum] = config[PCI_CB_CARD_BUS];
		}
	    }
	    
	    for (p = pacc->devices; p; p=p->next) {
		u_int8_t config[256];
		int bustype;
		unsigned int subvend, subdev;
		struct pciDevice *dev,*a_dev;
		
		memset(config,'\0',256);
		pci_read_block(p, 0, config, 64);
		if ((config[PCI_HEADER_TYPE] & 0x7f) == PCI_HEADER_TYPE_CARDBUS) {
		    /* Cardbus bridge */
		    pci_read_block(p, 64, config+64, 64);
		    subvend = config[PCI_CB_SUBSYSTEM_VENDOR_ID+1] << 8 | config[PCI_CB_SUBSYSTEM_VENDOR_ID];
		    subdev = config[PCI_CB_SUBSYSTEM_ID+1] << 8 | config[PCI_CB_SUBSYSTEM_ID];
		} else {
		    subvend = config[PCI_SUBSYSTEM_VENDOR_ID+1] << 8 | config[PCI_SUBSYSTEM_VENDOR_ID];
		    subdev = config[PCI_SUBSYSTEM_ID+1] << 8 | config[PCI_SUBSYSTEM_ID];
		}
		pci_fill_info(p, PCI_FILL_IDENT | PCI_FILL_CLASS | PCI_FILL_IRQ | PCI_FILL_BASES | PCI_FILL_ROM_BASE | PCI_FILL_SIZES);
		bustype = PCI_NORMAL;
	        for (bridgenum=0; cardbus_bridges[bridgenum]; bridgenum++) {
			if (p->bus == cardbus_bridges[bridgenum])
			  bustype = PCI_CARDBUS;
		}
		dev = pciGetDeviceInfo(p->vendor_id,p->device_id, subvend, subdev, bustype, p->device_class << 8 | config[PCI_CLASS_PROG]);
		devtype = p->device_class;
		if (devtype == PCI_CLASS_SERIAL_USB) {    
			/* Test to see if it's UHCI or OHCI */
			if (config[PCI_CLASS_PROG] == 0) {
				free(dev->driver);
				if (kernel_release >= 2.5) 
					dev->driver = strdup("uhci-hcd");
				else
					dev->driver = strdup("usb-uhci");
			} else if (config[PCI_CLASS_PROG] == 0x10) {
				free(dev->driver);
				if (kernel_release >= 2.5)
					dev->driver = strdup("ohci-hcd");
				else
					dev->driver = strdup("usb-ohci");
			} else if (config[PCI_CLASS_PROG] == 0x20) {
				free(dev->driver);
				dev->driver = strdup("ehci-hcd");
			}
		}
		if (devtype == PCI_CLASS_SERIAL_FIREWIRE) {    
			/* Test to see if it's OHCI */
			if (config[PCI_CLASS_PROG] == 0x10) {
				free (dev->driver);
				dev->driver = strdup("ohci1394");
			}
		}
		/* Check for an i2o device. Note that symbios controllers
		 * also need i2o_scsi module. Dunno how to delineate that
		 * here. */
		if (devtype == 0x0e00) {
			if ((config[PCI_CLASS_PROG] == 0 || config[PCI_CLASS_PROG] == 1) &&
				!strcmp(dev->driver,"unknown")) {
				free(dev->driver);
				dev->driver = strdup("i2o_block");
			}
		}
		if (devtype == PCI_CLASS_BRIDGE_CARDBUS) {
			free(dev->driver);
			dev->driver = strdup("yenta_socket");
		}
		if (isDisabled(p, config)) {
			free(dev->driver);
			dev->driver = strdup("disabled");
		}
	        if (dev->pciType == PCI_CARDBUS) {
			dev->detached = 1;
		}
		/* Sure, reuse a pci id for an incompatible card. */
		if (dev->vendorId == 0x10ec && dev->deviceId == 0x8139 &&
		    config[PCI_REVISION_ID] >= 0x20) {
			free(dev->driver);
			dev->driver = strdup("8139cp");
		}
		/* nForce4 boards show up with their ethernet controller
		 * as a bridge; hack it */
		if (dev->vendorId == 0x10de && dev->deviceId == 0x0057)
			    devtype = PCI_CLASS_NETWORK_ETHERNET;
		dev->pcidom = p->domain;
		dev->pcibus = p->bus;
		dev->pcidev = p->dev;
		dev->pcifn = p->func;
		if (dev->desc) free(dev->desc);
		dev->desc = malloc(128);
		dev->desc = pci_lookup_name(pacc, dev->desc, 128,
					    PCI_LOOKUP_VENDOR | PCI_LOOKUP_DEVICE,
					    p->vendor_id, p->device_id, 0, 0);
			
		if ( (probeFlags & PROBE_ALL) || (strcmp(dev->driver,"unknown") && strcmp(dev->driver, "disabled") && strcmp(dev->driver,"ignore"))) {
		    if (!type || (type<0xff && (type==devtype>>8))
			|| (type== kudzuToPci (pciToKudzu (devtype)))) {
			a_dev = pciNewDevice(dev);
			a_dev->type = pciToKudzu(devtype);
			switch (a_dev->type) {
			case CLASS_NETWORK:
				if (devtype == PCI_CLASS_NETWORK_TOKEN_RING)
				  a_dev->device = strdup("tr");
				else if (devtype == PCI_CLASS_NETWORK_FDDI)
				  a_dev->device = strdup("fddi");
				else
				  a_dev->device = strdup("eth");
				break;
			case CLASS_MODEM:
				checkPCISerial (a_dev, p);
				break;
			default:
				break;
			}
			a_dev->index = order;
			order++;
			if (devlist) {
			    a_dev->next = devlist;
			}
			devlist = (struct device *)a_dev;
		    } 
		}
		pciFreeDevice(dev);
	    }
	    pci_cleanup(pacc);
	}
    }
    if (pciDeviceList && init_list)
	  pciFreeDrivers();
    return devlist;
}

