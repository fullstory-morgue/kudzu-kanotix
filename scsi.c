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
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <scsi/scsi_ioctl.h>
#include <scsi/scsi.h>

#include "scsi.h"
#include "modules.h"

#define SCSI_ID(s) ((s).dev_id & 0xFF)
#define SCSI_LUN(s) (((s).dev_id >> 8) & 0xFF)
#define SCSI_CHANNEL(s) (((s).dev_id >> 16) & 0xFF)
#define SCSI_HOST(s) (((s).dev_id >> 24) & 0xFF)

/* Gross */
struct scsi_idlun {
    unsigned int dev_id;
    unsigned int host_unique_id;
} ;

static void scsiFreeDevice( struct scsiDevice *dev ) {
    if (dev->generic) free(dev->generic);
    freeDevice( (struct device *)dev);
}

static void scsiWriteDevice( FILE *file, struct scsiDevice *dev) {
	writeDevice(file, (struct device *)dev);
	fprintf(file,"host: %d\nid: %d\nchannel: %d\nlun: %d\n",
		dev->host, dev->id, dev->channel, dev->lun);
	if (dev->generic)
		fprintf(file,"generic: %s\n",dev->generic);
}

static int scsiCompareDevice (struct scsiDevice *dev1, struct scsiDevice *dev2)
{
        int c,h,i,l,x;
	x = compareDevice( (struct device *)dev1, (struct device *)dev2);
	if (x && x!=2) return x;
	c=dev1->channel-dev2->channel;
	h=dev1->host-dev2->host;
	i=dev1->id-dev2->id;
	l=dev1->lun-dev2->lun;
	if ( c || h || i || l) return 1;
	if (dev1->generic && dev2->generic)
		return strcmp(dev1->generic,dev2->generic);
	else
		return dev1->generic - dev2->generic;
	return x;
}

struct scsiDevice *scsiNewDevice( struct scsiDevice *old) {
    struct scsiDevice *ret;
    
    ret = malloc( sizeof(struct scsiDevice) );
    memset(ret, '\0', sizeof (struct scsiDevice));
    ret=(struct scsiDevice *)newDevice((struct device *)old,(struct device *)ret);
    ret->bus = BUS_SCSI;
    ret->newDevice = scsiNewDevice;
    ret->freeDevice = scsiFreeDevice;
	ret->writeDevice = scsiWriteDevice;
	ret->compareDevice = scsiCompareDevice;
    if (old && old->bus ==BUS_SCSI) {
	ret->host = old->host;
	ret->id = old->id;
	ret->channel = old->channel;
	ret->lun = old->lun;
	if (old->generic)
 	    ret->generic = strdup(old->generic);
    }
    return ret;
}

#define SCSISCSI_TOP    0
#define SCSISCSI_HOST   1
#define SCSISCSI_VENDOR 2
#define SCSISCSI_TYPE   3

static int findClassNames(struct scsiDevice * devlist, char * prefix, 
			  enum deviceClass class, int useLetters) {
    struct scsiDevice * dev;
    int count = 0;
    int devCount;
    char devName[50];
    int fd;
    int madeDeviceNode;
    struct scsi_idlun scsiId;

    for (dev = devlist; dev; dev = (struct scsiDevice *) dev->next) {
	if (dev->type & class)
	    count++;
	else if (class == CLASS_UNSPEC)
	    count++;
    }

    devCount = 0;
    while (count && devCount < 256) {
	if (!useLetters) {
	    sprintf(devName, "/dev/%s%d", prefix, devCount);
	} else {
	    if (devCount < 26)
		  sprintf(devName, "/dev/%s%c", prefix,
			    devCount + 'a');
	    else
		  sprintf(devName, "/dev/%s%c%c", prefix,
			    (devCount/26-1) + 'a', (devCount%26) + 'a');
	}

	devCount++;

	madeDeviceNode = 0;
	if (access(devName, O_RDONLY)) {
#ifdef __LOADER__
	    devMakeInode(basename(devName), dirname(devName));
	    madeDeviceNode = 1;
#else
	    return 1;
#endif
	}

	fd = open(devName, O_RDONLY | O_NONBLOCK);
	if (fd >= 0)  {
	    ioctl(fd, SCSI_IOCTL_GET_IDLUN, &scsiId);

	    close(fd);
#ifdef __LOADER__
	    if (madeDeviceNode)
			unlink(devName);
#endif
	    /* look for the device we just found */

	    for (dev = devlist; dev; 
			    dev = (struct scsiDevice *) dev->next) {
		if (dev->host == SCSI_HOST(scsiId) &&
		    dev->channel == SCSI_CHANNEL(scsiId) &&
		    dev->id == SCSI_ID(scsiId) &&
		    dev->lun == SCSI_LUN(scsiId)) break;
	    }

	    if (!dev) {
		DEBUG("CANNOT FIND DEVICE %d %d %d %d\n",
			SCSI_HOST(scsiId), SCSI_CHANNEL(scsiId),
			SCSI_ID(scsiId), SCSI_LUN(scsiId));
		return 1;
	    }

	    /* don't let the scan for generic devices overwrite this */
	    if (!dev->device)
		dev->device = strdup(devName + 5);

	    if (class == CLASS_UNSPEC)
		dev->generic = strdup(devName + 5);
	    count--;
	}
    }

    if (count) {
	DEBUG("CANNOT MAP ALL SCSI DEVICES\n");
	return 1;
    }

    return 0;
}

/* This tries to match devices names by bus number, etc. If it can't open
   things in /dev, it just makes it up though (or the install wouldn't work) */
static void scsiFindDeviceNames(struct scsiDevice * devlist, int forceSeq) {
    struct scsiDevice * dev;
    int rc;
    int genericNum = 0;
    char devName[50], gdevName[50];

    if (forceSeq) {
	int driveNum = 0;
	int tapeNum = 0;
	int cdromNum = 0;

	/* Fallback to best guess. This won't work well with hotplug, but
	   it works fine for install time. */ 

	for (dev = devlist; dev; dev = (struct scsiDevice *) dev->next) {
		*devName = '\0';
		
		if (dev->bus != BUS_SCSI) continue;

		sprintf(gdevName, "sg%d", genericNum++);
		if (dev->generic) free(dev->generic);
		dev->generic = strdup(gdevName);
		
		switch (dev->type) {
		 case CLASS_HD:
		 case CLASS_FLOPPY:
			if (driveNum < 26)
			  sprintf(devName, "sd%c", driveNum + 'a');
			else
			  sprintf(devName, "sd%c%c", (driveNum/26-1) + 'a',
				  (driveNum%26) + 'a');
			driveNum++;
			break;
			
		 case CLASS_TAPE:
			sprintf(devName, "st%d", tapeNum++);
			break;
			
		 case CLASS_CDROM:
			sprintf(devName, "scd%d", cdromNum++);
			break;
			
		 case CLASS_SCANNER:
		 case CLASS_PRINTER:
		 default:
			strcpy(devName, gdevName);
			break;
		}
		
		if (dev->device) free(dev->device);
		dev->device = strdup(devName);
	}

	return;
    }
	
    rc = 0;
    rc |= findClassNames(devlist, "scd",    CLASS_CDROM,    0);
    rc |= findClassNames(devlist, "st",	    CLASS_TAPE,	    0);
    rc |= findClassNames(devlist, "sd",	    CLASS_HD | CLASS_FLOPPY,	    1);
    
    /* Hack - replace *all* of this with proper sysfs stuff. */
	for (dev = devlist; dev; dev = (struct scsiDevice *) dev->next) {
		*devName = '\0';
		
		if (dev->bus != BUS_SCSI) continue;

		sprintf(devName, "sg%d", genericNum++);
		switch (dev->type) {
		 case CLASS_HD:
		 case CLASS_FLOPPY:
		 case CLASS_TAPE:
		 case CLASS_CDROM:
		 	if (!dev->generic)
		 		dev->generic = strdup(devName);
			break;
			
		 case CLASS_SCANNER:
		 case CLASS_PRINTER:
		 default:
		 	if (!dev->device)
		 		dev->device = strdup(devName);
		 	if (!dev->generic)
		 		dev->generic = strdup(devName);
			break;
		}
	}
		
    if (rc)
	scsiFindDeviceNames(devlist, 1);
}

static int loadMissingHosts(int * numMissingHosts) {
    int num = 0;
    char fn[256];
    int numMissing = 0;
    DIR *dir;
    struct dirent *ent;
    FILE * f;
    char * chptr;
    char * end;

    while (1) {
	sprintf(fn, "/proc/scsi/usb-storage-%d", num++);

	if (!(dir = opendir(fn))) return numMissing;

	while ((ent = readdir(dir))) {
	    if (ent->d_name[0] == '.') continue;

	    strcat(fn, "/");
	    strcat(fn, ent->d_name);

	    f = fopen(fn, "r");
	    if (f) {
		while (fgets(fn, sizeof(fn), f)) {
		    for (chptr = fn; *chptr && isspace(*chptr); chptr++);

		    if (!strncmp(chptr, "Attached:", 9)) {
			chptr = chptr + 9;
			while (isspace(*chptr)) chptr++;
			
			/* there is a \n at the end */
			end = chptr + strlen(chptr) - 2;
			while (isspace(*end) && end > chptr) *end--;
			*(end + 1) = '\0';

			if (*chptr == '0' || !strcasecmp(chptr, "no")) {
			    numMissingHosts[numMissing++] = atoi(ent->d_name);
			}
		    }
		}

		fclose(f);
	    }

	}

	closedir(dir);
    }

    abort();
}

struct device *scsiProbe( enum deviceClass probeClass, int probeFlags,
			 struct device *devlist) {
    int fd;
    char buf[16384];
    char linebuf[80];
    char *scsibuf = NULL;
    char path[64];
    struct stat sb;
    int rc;
    unsigned long bytes = 0;
    int i, state = SCSISCSI_TOP;
    char * start, * chptr, * next, *end;
    int host=-1, channel=-1, id=-1, lun=-1;
    struct scsiDevice *newdev;
    struct device * devend = NULL, *devhead = NULL;
    int missingHosts[256];
    int numMissingHosts = 0;
    
    if (
	(probeClass & CLASS_OTHER) ||
	(probeClass & CLASS_CDROM) ||
	(probeClass & CLASS_TAPE) ||
	(probeClass & CLASS_FLOPPY) ||
	(probeClass & CLASS_SCANNER) ||
	(probeClass & CLASS_HD) ||
	(probeClass & CLASS_PRINTER)
	) {

	numMissingHosts = loadMissingHosts(missingHosts);
	    
	if (access("/proc/scsi/scsi", R_OK)) goto out;

	fd = open("/proc/scsi/scsi", O_RDONLY);
	if (fd < 0) goto out;
    
	memset(buf, '\0', sizeof(buf));
	while (read(fd, buf, sizeof(buf)) > 0) {
		scsibuf = realloc(scsibuf, bytes + sizeof(buf));
		memcpy(scsibuf + bytes, buf, sizeof(buf));
		bytes += sizeof(buf);
		memset(buf, '\0', sizeof(buf));
	}
	close(fd);

	start = scsibuf;
	while (*start) {
	    chptr = start;
	    while (*chptr && *chptr != '\n') chptr++;
	    *chptr = '\0';
	    next = chptr + 1;
	    
	    switch (state) {
	     case SCSISCSI_TOP:
		if (strncmp("Attached devices:", start, 17)) {
		    goto out;
 		}
		state = SCSISCSI_HOST;
		break;

	     case SCSISCSI_HOST:
		if (strncmp("Host: ", start, 6)) {
		    goto out;
		}
		
		start = strstr(start, "scsi");
		if (!start) {
		    goto out;
		}
		start += 4;
		host = strtol(start, NULL, 10);

		start = strstr(start, "Channel: ");
		if (!start) {
		    goto out;
		}
		start += 9;
		channel = strtol(start, NULL, 10);

		start = strstr(start, "Id: ");
		if (!start) {
		    goto out;
		}
		start += 4;
		id = strtol(start, NULL, 10);
		
		start = strstr(start, "Lun: ");
		if (!start) {
		    goto out;
		}
		start += 5;
		lun = strtol(start, NULL, 10);
		
		state = SCSISCSI_VENDOR;
		break;
		
	     case SCSISCSI_VENDOR:
		if (strncmp("  Vendor: ", start, 10)) {
		    goto out;
		}
		
		start += 10;
		end = chptr = strstr(start, "Model:");
		if (!chptr) {
		    goto out;
		}
		
		chptr--;
		while (*chptr == ' ' && *chptr != ':' ) chptr--;
	        if (*chptr == ':') {
			chptr++;
			*(chptr + 1) = '\0';
			strcpy(linebuf,"Unknown");
		} else {
 			*(chptr + 1) = '\0';
			strcpy(linebuf, start);
		}
		*linebuf = toupper(*linebuf);
		chptr = linebuf + 1;
		while (*chptr) {
		    *chptr = tolower(*chptr);
		    chptr++;
		}
		
		start = end;  /* beginning of "Model:" */
		start += 7;
		
		chptr = strstr(start, "Rev:");
		if (!chptr) {
		    goto out;
		}
		
		chptr--;
		while (*chptr == ' ') chptr--;
		*(chptr + 1) = '\0';
		
		strcat(linebuf, " ");
		strcat(linebuf, start);
		
		state = SCSISCSI_TYPE;
		
		break;
		
	     case SCSISCSI_TYPE:
		if (strncmp("  Type:", start, 7)) {
		    goto out;
		}
		
		newdev = scsiNewDevice(NULL);
		if (strstr(start, "Direct-Access") || strstr(start, "Optical Device")) {
		    newdev->type = CLASS_HD;
		} else if (strstr(start, "Sequential-Access")) {
		    newdev->type = CLASS_TAPE;
		} else if (strstr(start, "CD-ROM")) {
		    newdev->type = CLASS_CDROM;
		} else if (strstr(start, "WORM")) {
		    newdev->type = CLASS_CDROM;
		} else if (strstr(start, "Scanner")) {
		    newdev->type = CLASS_SCANNER;
		} else if (strstr(start, "Printer")) { /* WTF? */
		    newdev->type = CLASS_PRINTER;
		} else {
		    newdev->type = CLASS_OTHER;
		}
		newdev->device = NULL;
		newdev->host = host;
		newdev->channel = channel;
		newdev->lun = lun;
		newdev->id = id;
		newdev->desc = strdup(linebuf);
		
		i = 0;
		while (1) {
			snprintf(path,63, "/proc/scsi/usb-storage-%d", i);
			if (stat(path, &sb)) break;
			snprintf(path,63, "/proc/scsi/usb-storage-%d/%d", i, newdev->host);
			i++;
			fd = open(path, O_RDONLY);
			if (fd < 0) continue;
			memset(buf, '\0', sizeof(buf));
			rc = read(fd, buf, 16384);
			if (rc <= 0) continue;
			buf[rc-1] = '\0';
			close(fd);
			if (strstr(buf, "Protocol: Uniform Floppy Interface (UFI)")) {
			  newdev->type = CLASS_FLOPPY;
			}
			break;
		}

		for (i = 0; i < numMissingHosts; i++) {
		    if (missingHosts[i] == newdev->host) {
			newdev->detached = 1;
			break;
		    }
		}

		newdev->next = NULL;
		if (devend)
		      devend->next = (struct device *)newdev;
		else
		      devhead = (struct device *)newdev;
		devend = (struct device *)newdev;
		state = SCSISCSI_HOST;
	    }
	    start = next;
	}
    }

out:
    if (devend)
	  devend->next = devlist;
    if (devhead)
	  devlist = (struct device *)devhead;
    if (scsibuf)
	  free(scsibuf);
	
    scsiFindDeviceNames((struct scsiDevice *) devlist, 0);
    
    /* SCSI device naming *sucks*. To get it right, we have to always
     * list all SCSI devices, and then strip out the ones we aren't
     * interested in. */
    
    if (probeClass != CLASS_UNSPEC) {
	    devend = devlist;
	    
	    while (devend) {
		    if (devend->bus == BUS_SCSI &&
			!(devend->type & probeClass)) {
			    devlist = listRemove(devlist, devend);
			    devend = devlist;
		    } else {
			    devend = devend->next;
		    }
	    }
    }
	
    return devlist;
}
