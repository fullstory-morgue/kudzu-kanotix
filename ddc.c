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
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "ddc.h"

#include "kudzuint.h"

static void ddcFreeDevice(struct ddcDevice *dev)
{
	if (dev->id) free(dev->id);
	freeDevice((struct device *) dev);
}

static void ddcWriteDevice(FILE *file, struct ddcDevice *dev)
{
	int x;
	
	writeDevice(file, (struct device *)dev);
	if (dev->id)
	  fprintf(file,"id: %s\n",
		  dev->id);
	if (dev->horizSyncMin)
	  fprintf(file,"horizSyncMin: %d\n",
		dev->horizSyncMin);
	if (dev->horizSyncMax)
	  fprintf(file,"horizSyncMax: %d\n",
		dev->horizSyncMax);
	if (dev->vertRefreshMin)
	  fprintf(file,"vertRefreshMin: %d\n",
		dev->vertRefreshMin);
	if (dev->vertRefreshMax)
	  fprintf(file,"vertRefreshMax: %d\n",
		dev->vertRefreshMax);	  
	if (dev->modes) {
		for (x=0;dev->modes[x]!=0;x+=2) {
			fprintf(file,"mode: %dx%d\n",
				dev->modes[x],dev->modes[x+1]);
		}
	}
	if (dev->mem)
	  fprintf(file,"mem: %ld\n",dev->mem);
}

static int ddcCompareDevice(struct ddcDevice *dev1, struct ddcDevice *dev2)
{
	return compareDevice((struct device *)dev1, (struct device *)dev2);
}

struct ddcDevice *ddcNewDevice(struct ddcDevice *old)
{
	struct ddcDevice *ret;

	ret = malloc(sizeof(struct ddcDevice));
	memset(ret, '\0', sizeof(struct ddcDevice));
	ret = (struct ddcDevice *) newDevice((struct device *) old, (struct device *) ret);
	ret->bus = BUS_DDC;
	ret->newDevice = ddcNewDevice;
	ret->freeDevice = ddcFreeDevice;
	ret->writeDevice = ddcWriteDevice;
	ret->compareDevice = ddcCompareDevice;
	if (old && old->bus == BUS_DDC) {
		if (old->id) 
		  ret->id = strdup(old->id);
		ret->horizSyncMin = old->horizSyncMin;
		ret->horizSyncMax = old->horizSyncMax;
		ret->vertRefreshMin = old->vertRefreshMin;
		ret->vertRefreshMax = old->vertRefreshMax;
		ret->mem = old->mem;
		if (old->modes) {
			int x;
			
			for (x=0;old->modes[x]!=0;x+=2);
			ret->modes = malloc((x+1)*sizeof(int));
			memcpy(ret->modes,old->modes,x+1);
		}
	}
	return ret;
}

#if defined(__i386__) || defined(__powerpc__) || defined(__x86_64__)

#include "ddcprobe/vbe.h"

struct monitor {
	char *model;
	char *id;
	int horiz[2];
	int vert[2];
};

static struct monitor * ddcDeviceList = NULL;
static int numDdcDevices = 0;
	  
static char *snip(char *string)
{
	int i;
	
	string[12] = '\0';
	while(((i = strlen(string)) > 0) &&
	      (isspace(string[i - 1]) ||
	       (string[i - 1] == '\n') ||
	       (string[i - 1] == '\r'))) {
		string[i - 1] = '\0';
	}
	return string;
}

static int devCmp(const void *a, const void *b) 
{
	const struct monitor *one = a;
	const struct monitor *two = b;

	/* do a case-insensitive compare because id returned from */
	/* directly probing card does not always have same case   */
	/* as matching entry in monitor db!                       */
	if (one->id && two->id)
		return strcasecmp(one->id,two->id);
	else
		return one->id - two->id;
}

int ddcReadDrivers(char *filename) 
{
	int dbfile;
	char *start, *buf, *tmp, *filebuf;
	char *model, *id;
	int horizmin, horizmax;
	int vertmin, vertmax;
	
	if (filename) {
		dbfile = open(filename, O_RDONLY);
		if (dbfile < 0)
		  return -1;
	} else {
		dbfile = open("/usr/share/hwdata/MonitorsDB", O_RDONLY);
		if (dbfile < 0) {
			dbfile = open("/etc/MonitorsDB", O_RDONLY);
			if (dbfile < 0) {
				dbfile = open("./MonitorsDB", O_RDONLY);
				if (dbfile < 0) 
				  return -1;
			}
		}
	}
	
	filebuf = __bufFromFd(dbfile);
	if (!filebuf) return -1;
	
	start = filebuf;
	while (1) {
		vertmin = vertmax = horizmin = horizmax = 0;
		if (*start == '#' || isspace(*start)) {
			start = strstr(start,"\n");
			*start = '\0';
			start++;
			continue;
		}
		buf = start;
		start = strstr(start,";");
		if (!start) 
		  break;
		*start = '\0';
		start+=2;
		buf = start;
		start = strstr(start,";");
		if (!start) {
			break;
		}
		*start = '\0';
		start+=2;
		model = strdup(buf);
		buf = start;
		start = strstr(start,";");
		if (!start) {
			free(model);
			break;
		}
		*start = '\0';
		start+=2;
		if (buf[0] >= '0' && buf[0] <= '9') {
			if (model)
			  free(model);
			start++;
			start = strstr(start,"\n");
			if (!start)
			  break;
			*start = '\0';
			start++;
			continue;
		}
		id = strdup(buf);
		buf = start;
		start = strstr(start,";");
		if (!start)
			break;
		*start = '\0';
		start+=2;
		horizmin = horizmax = (int)strtof(buf,&tmp);
		if (tmp && *tmp == '-') {
			horizmax = (int)strtof(tmp+1,NULL);
		}
		buf = start;
		start = strstr(start,";");
		if (!start)
			break;
		vertmin = vertmax = (int)strtof(buf,&tmp);
		if (tmp && *tmp == '-') {
			vertmax = (int)strtof(tmp+1,NULL);
		}
		ddcDeviceList = realloc(ddcDeviceList, (numDdcDevices+1)*
					sizeof(struct monitor));
		ddcDeviceList[numDdcDevices].id = id;
		ddcDeviceList[numDdcDevices].model = model;
		ddcDeviceList[numDdcDevices].horiz[0] = horizmin;
		ddcDeviceList[numDdcDevices].horiz[1] = horizmax;
		ddcDeviceList[numDdcDevices].vert[0] = vertmin;
		ddcDeviceList[numDdcDevices].vert[1] = vertmax;
		numDdcDevices++;
		start++;
		start = strstr(start,"\n");
		if (!start)
		  break;
		*start = '\0';
		start++;
	}
	free(filebuf);
	qsort(ddcDeviceList, numDdcDevices, sizeof(*ddcDeviceList), devCmp);
	return 0;
}

void ddcFreeDrivers()
{
	int x;
	
	if (ddcDeviceList) {
		for (x=0; x< numDdcDevices; x++) {
			if (ddcDeviceList[x].id) free(ddcDeviceList[x].id);
			if (ddcDeviceList[x].model) free(ddcDeviceList[x].model);
		}
		free(ddcDeviceList);
		ddcDeviceList = NULL;
		numDdcDevices = 0;
	}
}

static struct ddcDevice *parseEDID(struct edid1_info *edid_info)
{
	struct ddcDevice *ret;
	struct monitor mon, *searched;
	int x;
	int pos = 0;
	
	if ((edid_info == NULL) || (edid_info->version ==0))
		return NULL;
	
	ret = ddcNewDevice(NULL);
	ret->id = malloc(10);
	snprintf(ret->id,8,"%c%c%c%04x",
		 edid_info->manufacturer_name.char1 + 'A' - 1,
		 edid_info->manufacturer_name.char2 + 'A' - 1,
		 edid_info->manufacturer_name.char3 + 'A' - 1,
		 edid_info->product_code);
	mon.id = ret->id;
	searched = bsearch(&mon,ddcDeviceList,numDdcDevices,
		      sizeof(struct monitor),devCmp);
	if (searched) {
		ret->desc = strdup(searched->model);
	}
	ret->physicalWidth = edid_info->max_size_horizontal * 10;
	ret->physicalHeight = edid_info->max_size_vertical * 10;
	for (x=0;x<4;x++) {
		struct edid_monitor_descriptor *monitor = NULL;
		monitor = &edid_info->monitor_details.monitor_descriptor[x];

		switch (monitor->type) {
		case edid_monitor_descriptor_name:
			if (!ret->desc)
				ret->desc = strdup(snip(monitor->data.string));
			break;
		case edid_monitor_descriptor_range:
			ret->horizSyncMin = monitor->data.range_data.horizontal_min;
			ret->horizSyncMax = monitor->data.range_data.horizontal_max;
			ret->vertRefreshMin = monitor->data.range_data.vertical_min;
			ret->vertRefreshMax = monitor->data.range_data.vertical_max;
			break;
		default:
			break;
		}
	}
	if ((ret->horizSyncMin * ret->horizSyncMax == 0) && (searched)) {
		ret->horizSyncMin = searched->horiz[0];
		ret->horizSyncMax = searched->horiz[1];
		ret->vertRefreshMin = searched->vert[0];
		ret->vertRefreshMax = searched->vert[1];
	}
	for (x=0;x<8;x++) {
		double aspect = 1;
		unsigned char xres, vfreq;
		
		xres = edid_info->standard_timing[x].xresolution;
		vfreq = edid_info->standard_timing[x].vfreq;
		if ((xres != vfreq) ||  
		    ((xres != 0) && (xres != 1)) ||
		    ((vfreq != 0) && (vfreq != 1))) {
			switch(edid_info->standard_timing[x].aspect) {
			case 0: aspect = 0.625; break;
			case 1: aspect = 0.750; break;
			case 2: aspect = 0.800; break;
			case 3: aspect = 0.5625; break;
			}
			ret->modes = realloc(ret->modes,(pos+3)*sizeof(int));
			ret->modes[pos] = (xres+31) * 8;
			ret->modes[pos+1] = ret->modes[pos] * aspect;
			ret->modes[pos+2] = 0;
			pos += 2;
		}
	}
	if (!ret->desc) ret->desc = strdup(ret->id);
	ret->type = CLASS_MONITOR;
	return ret;
}

static struct edid1_info *readEDIDFromACPI()
{
	static glob_t globres;
	static int index = -1;
	int f;
	struct edid1_info *ret;
	u_int16_t man;
	
	if (index == -1) {
		if (glob("/proc/acpi/video/*/*/EDID",0,NULL, &globres) != 0)
			return NULL;
		index = 0;
	}
	
	if (!globres.gl_pathv[index])
		goto out;
	f = open(globres.gl_pathv[index], O_RDONLY);
	if (f == -1) 
		goto out;
	index++;
	ret = (struct edid1_info *)__bufFromFd(f);
	if (!ret)
		goto out;
        memcpy(&man, &ret->manufacturer_name, 2);
	man = ntohs(man);
        memcpy(&ret->manufacturer_name, &man, 2);
	return ret;
out:
	globfree(&globres);
	return NULL;
}

struct device *ddcProbe(enum deviceClass probeClass, int probeFlags,
			struct device *devlist)
{
	struct ddcDevice *newdev;
	struct vbe_info *vbe_info = NULL;
	struct edid1_info *edid_info = NULL;
	int init_list = 0;
	
	if (probeFlags & PROBE_SAFE) return devlist;	

	if (
	    (probeClass & CLASS_OTHER) ||
	    (probeClass & CLASS_VIDEO) ||
	    (probeClass & CLASS_MONITOR)
	    ) {
		
		if (!ddcDeviceList) {
			ddcReadDrivers(NULL);
			init_list = 1;
		}
		if (probeClass & CLASS_VIDEO) {
			vbe_info = vbe_get_vbe_info();
			if (vbe_info) {
				newdev = ddcNewDevice(NULL);
				newdev->mem = vbe_info->memory_size * 64;
				if (vbe_info->product_name.string && vbe_info->vendor_name.string &&
				    strcmp(vbe_info->vendor_name.string,"Vendor Name")) {
					if (!strncasecmp(vbe_info->product_name.string, vbe_info->vendor_name.string,
							 strlen(vbe_info->vendor_name.string))) {
						newdev->desc = strdup(vbe_info->product_name.string);
					} else {
						newdev->desc = malloc(256);
						memset(newdev->desc,'\0',256);
						snprintf(newdev->desc, 255, "%s %s",vbe_info->vendor_name.string,
							 vbe_info->product_name.string);
					}
				}
				if (!newdev->desc && vbe_info->oem_name.string) {
					newdev->desc = strdup(vbe_info->oem_name.string);
				}
				if (!newdev->desc)
					newdev->desc = strdup("Some Random Video Card");
				newdev->type = CLASS_VIDEO;
				if (devlist)
					newdev->next = devlist;
				devlist = (struct device *)newdev;
			}
		}
		if (probeClass & CLASS_MONITOR) {
			if (!get_edid_supported())
				goto acpi;
			edid_info = get_edid_info();
			newdev = parseEDID(edid_info);
			if (newdev) {
				if (devlist)
					newdev->next = devlist;
				devlist = (struct device *)newdev;
				goto out;
			}
acpi:
			while ((edid_info = readEDIDFromACPI())) {
				newdev = parseEDID(edid_info);
				if (newdev) {
					if (devlist)
						newdev->next = devlist;
					devlist = (struct device *)newdev;
				}
			}
		}
	}
out:
	if (ddcDeviceList && init_list)
		ddcFreeDrivers();
	return devlist;
}

#else
int ddcReadDrivers(char *filename)
{
	return 0;
}

void ddcFreeDrivers() 
{
};


struct device *ddcProbe(enum deviceClass probeClass, int probeFlags,
			struct device *devlist)
{
	return devlist;
}
#endif
