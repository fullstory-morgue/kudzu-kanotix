/* Copyright 2003 Red Hat, Inc.
 *
 * This software may be freely redistributed under the terms of the GNU
 * public license.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Portions of this code from pcmcia-cs, copyright David A. Hinds
 * <dahinds@users.sourceforge.net>
 * 
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pcmcia.h"

struct pcmciaSearchDev {
	struct pcmciaDevice pdev;
	char *vers_str[5];
};

static struct pcmciaSearchDev * pcmciaDeviceList = NULL;
static int numPcmciaDevices = 0;

static int devCmp(const void * a, const void * b) {
	const struct pcmciaDevice * one = a;
	const struct pcmciaDevice * two = b;
	int x=0,y=0, z=0;
    
	x = (one->vendorId - two->vendorId);
	y = (one->deviceId - two->deviceId);
	z = (one->function - two->function);
	if (x)
		return x;
	else if (y)
		return y;
	else return z;
}

static int devCmpSort(const void * a, const void * b) {
	const struct pcmciaSearchDev * one = a;
	const struct pcmciaSearchDev * two = b;
	int x=0,y=0,z=0;
	int i;
	
	x = (one->pdev.vendorId - two->pdev.vendorId);
	y = (one->pdev.deviceId - two->pdev.deviceId);
	if (x)
		return x;
	else if (y)
		return y;
	for (i=0; i<5; i++) {
		if (one->vers_str[i] && two->vers_str[i])
			z = strcmp(one->vers_str[i], two->vers_str[i]);
		else
			z = one->vers_str[i] - two->vers_str[i];
		if (z)
			return z;
	}
	return 0;
}

static int devCmpSearch(const void * a, const void * b) {
	const struct pcmciaSearchDev * one = a;
	const struct pcmciaSearchDev * two = b;
	int x=0,y=0,z=0;
	int i;
	
	x = (one->pdev.vendorId - two->pdev.vendorId);
	y = (one->pdev.deviceId - two->pdev.deviceId);
	if (x)
		return x;
	else if (y)
		return y;
	for (i=0; i<5; i++) {
		if (one->vers_str[i] && two->vers_str[i]) {
			if (!strcmp(two->vers_str[i],"*"))
				z = 0;
			else
				z = strcmp(one->vers_str[i], two->vers_str[i]);
		} else
			z = one->vers_str[i] - two->vers_str[i];
		if (z)
			return z;
	}
	return 0;
}


static void pcmciaFreeDevice(struct pcmciaDevice *dev) {
    freeDevice((struct device *)dev);
}

static void pcmciaWriteDevice(FILE *file, struct pcmciaDevice *dev) {
	writeDevice(file, (struct device *)dev);
	fprintf(file,"vendorId: %04x\ndeviceId: %04x\nfunction: %d\nslot: %d\n",dev->vendorId,dev->deviceId,dev->function,dev->slot);
}

static int pcmciaCompareDevice(struct pcmciaDevice *dev1, struct pcmciaDevice *dev2)
{
	int x,y;
	
	x = compareDevice((struct device *)dev1,(struct device *)dev2);
	if (x && x!=2) 
	  return x;
	if ((y=devCmp( (void *)dev1, (void *)dev2 )))
	  return y;
	return x;
}
			    
struct pcmciaDevice * pcmciaNewDevice(struct pcmciaDevice *dev) {
    struct pcmciaDevice *ret;
    
    ret = malloc(sizeof(struct pcmciaDevice));
    memset(ret,'\0',sizeof(struct pcmciaDevice));
    ret=(struct pcmciaDevice *)newDevice((struct device *)dev,(struct device *)ret);
    ret->bus = BUS_PCMCIA;
    if (dev && dev->bus == BUS_PCMCIA) {
	ret->vendorId = dev->vendorId;
	ret->deviceId = dev->deviceId;
	ret->slot = dev->slot;
	ret->port = dev->port;
	ret->function = dev->function;
    }
    ret->newDevice = pcmciaNewDevice;
    ret->freeDevice = pcmciaFreeDevice;
    ret->writeDevice = pcmciaWriteDevice;
    ret->compareDevice = pcmciaCompareDevice;
    return ret;
}

int pcmciaReadDrivers(char *filename) {
	int fd;
	struct pcmciaSearchDev *nextDevice, *tmpdev = NULL, key;
	char *module, *m1, *m2;
	char *descrip = NULL;
	char *vers_str[5];
	char *buf, *start, *tmp, *next;
	int numDrivers, merge = 0, i;
	unsigned int vendid, devid;
	
	if (filename) {
		fd = open(filename,O_RDONLY);
		if (fd < 0)
			return -1;
	} else {
		fd = open("/etc/pcmcia/config", O_RDONLY);
		if (fd < 0) {
			fd = open("./config", O_RDONLY);
			if (fd < 0)
				return -1;
		}
	}
	buf = bufFromFd(fd);
	if (!buf)
		return -1;
	/* upper bound */
	numDrivers = 1;
	start = buf;
	do {
		if (start[0] == '\n') start++;
		if (!strncmp(start,"card \"",5)) 
			numDrivers++;
	} while ( (start = strchr(start,'\n')));
	
	if (pcmciaDeviceList)
		merge = 1;
	
	pcmciaDeviceList = realloc(pcmciaDeviceList, sizeof(*pcmciaDeviceList) *
				   (numPcmciaDevices + numDrivers));
	nextDevice = pcmciaDeviceList + numPcmciaDevices;
	
	memset(vers_str,'\0',sizeof(vers_str));
	vendid = devid = 0;
	m1 = m2 = NULL;
	
	start = buf;
	while (start && *start) {
		while (isspace(*start)) start++;
		next = strchr(start,'\n');
		if (next)
			next ++;
		if (!strncmp(start,"card \"",6)) {
			start += 6;
			tmp = strchr(start,'"');
			*tmp = '\0'; tmp++;
			descrip = strdup(start);
			start = next;
			continue;
		}
		if (!strncmp(start,"version \"",9)) {
			start += 9;
			for (i=0 ; i < 5 ; i++) {
				tmp = strchr(start,'"');
				*tmp ='\0'; tmp++;
				vers_str[i] = strdup(start);
				start = tmp;
				if ( (start = strchr(start,'"')) && start < (next-1)) {
					start++;
					continue;
				} else {
					break;
				}
			}
			start = next;
			continue;
		}
		if (!strncmp(start,"manfid ",7)) {
			start += 7;
			vendid = strtoul(start, &start, 16);
			if (vendid && start) {
				start++;
				devid = strtoul(start, NULL, 16);
			}
			start = next;
			continue;
		}
		if (!strncmp(start,"bind \"",6)) {
			start += 6;
			tmp = strchr(start,'"');
			*tmp = '\0'; tmp++;
			m1 = strdup(start);
			if ( (start = strchr(tmp,',')) && start < next-1 ) {
				if ( (start = strchr(start,'"')) && start < next -1 ) {
					start++;
					tmp = strchr(start,'"');
					*tmp = '\0';
					m2 = strdup(start);
				}
			}
			if (m1 && m2) {
				module = malloc(strlen(m1)+strlen(m2)+2);
				sprintf(module,"%s/%s",m1,m2);
				free(m1);
				free(m2);
			} else {
				module = m1;
			}
			
			if (merge) {
				tmpdev = nextDevice;
				key.pdev.vendorId = vendid;
				key.pdev.deviceId = devid;
				for (i = 0; i< 5; i++) {
					key.vers_str[i] = vers_str[i];
				}
				nextDevice = bsearch(&key,pcmciaDeviceList,
						     numPcmciaDevices,
						     sizeof(struct pcmciaSearchDev), devCmpSort);
				
				if (!nextDevice) {
					nextDevice = tmpdev;
					tmpdev = NULL;
					numPcmciaDevices++;
				} else {
					if (nextDevice->pdev.device) free(nextDevice->pdev.device);
					if (nextDevice->pdev.desc) free(nextDevice->pdev.desc);
					if (nextDevice->pdev.driver) free(nextDevice->pdev.driver);
					for (i=0; i<5; i++) {
						if (nextDevice->vers_str[i])
							free(nextDevice->vers_str[i]);
					}
				}
			} else
				numPcmciaDevices++;
			nextDevice->pdev.vendorId = vendid;
			nextDevice->pdev.deviceId = devid;
			nextDevice->pdev.desc = strdup(descrip);
 			nextDevice->pdev.next = NULL;
			nextDevice->pdev.device = NULL;
			nextDevice->pdev.type = 0;
			nextDevice->pdev.bus = BUS_PCMCIA;
			nextDevice->pdev.driver = strdup(module);
			for (i = 0; i< 5; i++) {
				nextDevice->vers_str[i] = vers_str[i];
			}
			if (merge && tmpdev)
				nextDevice = tmpdev;
			else {
				nextDevice++;
				if (merge)
					qsort(pcmciaDeviceList,numPcmciaDevices,
					      sizeof(*pcmciaDeviceList), devCmpSort);
			}
					
			free(module);
			free(descrip);
			m1 = m2 = NULL;
			memset(vers_str,'\0',sizeof(vers_str));
			vendid = devid = 0;
		}
		start = next;
	}
	qsort(pcmciaDeviceList,numPcmciaDevices,sizeof(*pcmciaDeviceList), devCmpSort);
	return 0;
}

void pcmciaFreeDrivers(void) {
	int x,i;
	
	if (pcmciaDeviceList) {
		for (x=0;x<numPcmciaDevices;x++) {
			if (pcmciaDeviceList[x].pdev.device) free(pcmciaDeviceList[x].pdev.device);
			if (pcmciaDeviceList[x].pdev.driver) free(pcmciaDeviceList[x].pdev.driver);
			if (pcmciaDeviceList[x].pdev.desc) free(pcmciaDeviceList[x].pdev.desc);
			for (i=0; i<5; i++)
				if (pcmciaDeviceList[x].vers_str[i]) free(pcmciaDeviceList[x].vers_str[i]);
		}
		free(pcmciaDeviceList);
		pcmciaDeviceList=NULL;
		numPcmciaDevices=0;
	}
}

#if defined(__i386__) || defined(__alpha__)

#include "pcmcia/cs_types.h"
#include "pcmcia/cistpl.h"
#include "pcmcia/cs.h"
#include "pcmcia/bulkmem.h"
#include "pcmcia/ds.h"

static enum deviceClass pcmciaToKudzu(unsigned int class) {
	if (!class) return CLASS_UNSPEC;
	switch (class) {
	case CISTPL_FUNCID_NETWORK:
		return CLASS_NETWORK;
	case CISTPL_FUNCID_SCSI:
		return CLASS_SCSI;
	case CISTPL_FUNCID_SERIAL:
		return CLASS_MODEM;
	case CISTPL_FUNCID_FIXED:
		return CLASS_HD;
	default:
		return CLASS_OTHER;
	}
}



static int get_tuple(int fd, unsigned char code, ds_ioctl_arg_t *arg) {
	arg->tuple.DesiredTuple = code;
	arg->tuple.Attributes = TUPLE_RETURN_COMMON;
	arg->tuple.TupleOffset = 0;
	if ((ioctl(fd, DS_GET_FIRST_TUPLE, arg) == 0) &&
	    (ioctl(fd, DS_GET_TUPLE_DATA, arg) == 0) &&
	    (ioctl(fd, DS_PARSE_TUPLE, arg) == 0))
		return 0;
	else
		return -1;
}

static int pcmcia_major = 0;

static int lookup_dev(char *name) {
	    FILE *f;
	    int n;
	    char s[32], t[32];
	     
	    f = fopen("/proc/devices", "r");
	    if (f == NULL)
		        return -errno;
	    while (fgets(s, 32, f) != NULL) {
		            if (sscanf(s, "%d %s", &n, t) == 2)
			                if (strcmp(name, t) == 0)
				                    break;
	    }
	    fclose(f);
	    if (strcmp(name, t) == 0)
		        return n;
	    else
		        return -ENODEV;
}

static int open_sock(int sock) {
	int fd;
	char fn[64];
	dev_t dev = (pcmcia_major<<8) + sock;
	
	snprintf(fn, 64, "/tmp/tmpdev-%d", getpid());
	if (mknod(fn, (S_IFCHR|0600), dev) == 0) {
		fd = open(fn, O_RDONLY);
		unlink(fn);
		if (fd >= 0)
			return fd;
	}
	return -1;
}

#define REMOVE	1
#define ADD	2
#define MEM	1
#define IO	2
#define IRQ	3

static struct adjust_t adj[] =  {
	{ ADD, IO, 0, .resource.io = { 0x0c00, 0x0100, 0 } },
	{ ADD, IO, 0, .resource.io = { 0x0100, 0x0400, 0 } },
	{ ADD, MEM, 0, .resource.memory = { 0xc0000, 0x40000 } },
	{ ADD, MEM, 0, .resource.memory = { 0x60000000, 0x1000000 } },
	{ ADD, MEM, 0, .resource.memory = { 0xa0000000, 0x1000000 } },
	{ ADD, IO, 0, .resource.io = { 0x0a00, 0x0100, 0 } },
	{ REMOVE, IRQ, 0, .resource.irq = { 4 } },
	{ REMOVE, IRQ, 0, .resource.irq = { 7 } },
	{ REMOVE, IRQ, 0, .resource.irq = { 12 } },
	{ REMOVE, IO, 0, .resource.io = { 0x3B0, 0x00B, 0 } },
	{ REMOVE, IO, 0, .resource.io = { 0x3D3, 0x001, 0 } },
	{ 0, 0, 0 }
};

static void adjust_resources(fd)
{
    int i=0;
    
    for (; adj[i].Action ; i++) {
	ioctl(fd, DS_ADJUST_RESOURCE_INFO, &adj[i]);
    }
}

static char *pcmciaDesc[] = {
		NULL,
		"Generic Memory Card",
		"Generic Serial/Modem Card",
		"Generic Parallel Port Card",
		"Generic ATA/IDE Disk",
		NULL,
		NULL,
		NULL,
		NULL,
};

static char *pcmciaDriver[] = {
		NULL,
		"memory_cs",
		"serial_cs",
		"parallel_cs",
		"ide-cs",
		NULL,
		NULL,
		NULL,
		NULL,
};

struct device * pcmciaProbe(enum deviceClass probeClass, int probeFlags, struct device *devlist) {
	
	struct pcmciaDevice *pcmciadev, *seconddev;
	int fd, ns, i, init_list =0;
	ds_ioctl_arg_t arg;
	cistpl_vers_1_t *vers = &arg.tuple_parse.parse.version_1;
	cistpl_manfid_t *manfid = &arg.tuple_parse.parse.manfid;
	cistpl_funcid_t *funcid = &arg.tuple_parse.parse.funcid;
	config_info_t cfg;
	cs_status_t status;
	char *temp;
	struct pcmciaSearchDev key, *searchdev;

	if (
	    (probeClass & CLASS_OTHER) ||
	    (probeClass & CLASS_NETWORK) ||
	    (probeClass & CLASS_SCSI) ||
	    (probeClass & CLASS_MODEM)) {
		
		if (!pcmciaDeviceList) {
			pcmciaReadDrivers(NULL);
			init_list = 1;
		}
	    
		pcmcia_major = lookup_dev("pcmcia");
		if (pcmcia_major < 0)
			goto out;
		for (ns = 0; ns < 8; ns++) {
			unsigned short vend = 0, dev = 0;
			unsigned char function = 0;
			char *version_strs[5];
			int found = 0;
			int port[2] = { 0, 0 };
			char desc[256];

			fd = open_sock(ns);
			if (fd < 0) break;

			memset(version_strs,'\0',5*sizeof(char *));
			memset(desc,'\0',256*sizeof(char));
		    
			adjust_resources(fd);
			/* Ignore cardbus cards */
			status.Function = 0;
			if (ioctl(fd, DS_GET_STATUS, &status) == 0) {
				if (status.CardState & CS_EVENT_CB_DETECT ||
				    !status.CardState & CS_EVENT_CARD_DETECT)
					continue;
			} else
				continue;
		    
			if (get_tuple(fd, CISTPL_VERS_1, &arg) == 0) {
				for (i = 0; i < vers->ns; i++) {
					version_strs[i] = strdup(vers->str+vers->ofs[i]);
				}
				version_strs[i]= NULL;
				found++;
			}
			if (get_tuple(fd, CISTPL_MANFID, &arg) == 0) {
				vend = manfid->manf;
				dev = manfid->card;
				found++;
			}
			if (get_tuple(fd, CISTPL_FUNCID, &arg) == 0) {
				function = funcid->func;
				found++;
			}
			memset (&cfg, '\0', sizeof(config_info_t));
			for (i = 0; i < 4; i++) {
				cfg.Function = i;
				if (ioctl(fd, DS_GET_CONFIGURATION_INFO, &cfg) == 0) {
					if (cfg.NumPorts1 > 0) {
						port[0] = cfg.BasePort1;
					}
					if (cfg.NumPorts2 > 0) {
						port[1] = cfg.BasePort2;
					}
				}
			}
			
			if (found) {
				pcmciadev = NULL;
				memset(&key,'\0',sizeof(key));
				key.pdev.vendorId = vend;
				key.pdev.deviceId = dev;
				for (i=0; i<5; i++)
					key.vers_str[i] = version_strs[i];
				for (i=4; i>0; i--) {
					key.vers_str[i] = NULL;
					searchdev = bsearch(&key,pcmciaDeviceList,
							    numPcmciaDevices,
							    sizeof(struct pcmciaSearchDev),
							    devCmpSearch);
					if (searchdev) {
						pcmciadev = pcmciaNewDevice(&searchdev->pdev);
						break;
					}
				}
				if (!pcmciadev) {
					memset(&key,'\0',sizeof(key));
					for (i=0; i<5; i++)
						key.vers_str[i] = version_strs[i];
					for (i=4; i>0; i--) {
						key.vers_str[i] = NULL;
						searchdev = bsearch(&key,pcmciaDeviceList,
								    numPcmciaDevices,
								    sizeof(struct pcmciaSearchDev),
								    devCmpSearch);
						if (searchdev) {
							pcmciadev = pcmciaNewDevice(&searchdev->pdev);
							break;
						}
					}
				}
				if (!pcmciadev) {
					memset(&key,'\0',sizeof(key));
					key.pdev.vendorId = vend;
					key.pdev.deviceId = dev;
					searchdev = bsearch(&key,pcmciaDeviceList,
							    numPcmciaDevices,
							    sizeof(struct pcmciaSearchDev),
							    devCmpSearch);
					if (searchdev) {
						pcmciadev = pcmciaNewDevice(&searchdev->pdev);
					}
				}
				if (!pcmciadev)
					switch (function) {
					case CISTPL_FUNCID_MEMORY:
					case CISTPL_FUNCID_SERIAL:
					case CISTPL_FUNCID_PARALLEL:
					case CISTPL_FUNCID_FIXED:
						pcmciadev = pcmciaNewDevice(NULL);
						pcmciadev->type = pcmciaToKudzu(function);
						pcmciadev->desc = strdup(pcmciaDesc[function]);
						pcmciadev->driver = strdup(pcmciaDriver[function]);
						pcmciadev->slot = ns;
						pcmciadev->vendorId = vend;
						pcmciadev->deviceId = dev;
					default:
						break;
				}
				if (!pcmciadev && (probeFlags & PROBE_ALL)) {
					char tmpstr[256];
					
					memset(tmpstr,'\0', sizeof(tmpstr));
					pcmciadev = pcmciaNewDevice(NULL);
					if (version_strs[0]) {
						strcat(tmpstr,version_strs[0]);
						for (i=1; i<5 ; i++) {
							if (version_strs[i]) {
								strcat(tmpstr," ");
								strcat(tmpstr,version_strs[i]);
							}
						}
					} else {
						pcmciadev->desc = strdup("Unknown PCMCIA Card");
					}
					pcmciadev->desc = strdup(tmpstr);
					pcmciadev->driver = strdup("unknown");
				}
					
				if (pcmciadev) {
					pcmciadev->function = 0;
					pcmciadev->type = pcmciaToKudzu(function);
					pcmciadev->vendorId = vend;
					pcmciadev->deviceId = dev;
					pcmciadev->port = port[0];
					pcmciadev->slot = ns;
					if ( (temp=strchr(pcmciadev->driver,'/'))) {
						*temp = '\0';
						seconddev = pcmciaNewDevice(pcmciadev);
						seconddev->function = 1;
						free(seconddev->driver);
						seconddev->driver = strdup(temp+1);
						seconddev->port = port[1];
						/* fixup */
						if (!strcmp(seconddev->driver,"serial_cs"))
							seconddev->type = CLASS_MODEM;
						if (!strcmp(pcmciadev->driver,"serial_cs"))
							pcmciadev->type = CLASS_MODEM;
						if (!strcmp(seconddev->driver,"xirc2ps_cs") ||
						    !strcmp(seconddev->driver,"3c574_cs") ||
						    !strcmp(seconddev->driver,"3c589_cs") ||
						    !strcmp(seconddev->driver,"pcnet_cs") ||
						    !strcmp(seconddev->driver,"smc91c92_cs"))
							seconddev->type = CLASS_NETWORK;
						if (!strcmp(pcmciadev->driver,"xirc2ps_cs") ||
						    !strcmp(pcmciadev->driver,"3c574_cs") ||
						    !strcmp(pcmciadev->driver,"3c589_cs") ||
						    !strcmp(pcmciadev->driver,"pcnet_cs") ||
						    !strcmp(pcmciadev->driver,"smc91c92_cs"))
							pcmciadev->type = CLASS_NETWORK;
						
					} else
						seconddev = NULL;
					if (seconddev && seconddev->type == CLASS_NETWORK)
						seconddev->device = strdup("eth");
					if (pcmciadev->type == CLASS_NETWORK)
						pcmciadev->device = strdup("eth");
					if (probeClass & pcmciadev->type) {
						if (devlist)
							pcmciadev->next = devlist;
						devlist = (struct device *) pcmciadev;
					}
					if (seconddev && (probeClass & seconddev->type)) {
						if (devlist)
							seconddev->next = devlist;
						devlist = (struct device *) seconddev;
					}
				}
			}
		}
	}
out:
	if (pcmciaDeviceList && init_list)
		pcmciaFreeDrivers();
	return devlist;
}

#else

struct device *pcmciaProbe(enum deviceClass probeClass, int probeFlags,
			   struct device *devlist) {
	return devlist;
}

#endif

