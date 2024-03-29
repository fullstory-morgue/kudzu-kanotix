/* Copyright 2005 Red Hat, Inc.
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
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fd.h>

#include "xen.h"
#include "kudzuint.h"
#include "modules.h"

static void xenFreeDevice(struct xenDevice *dev)
{
	freeDevice((struct device *) dev);
}

static void xenWriteDevice(FILE *file, struct xenDevice *dev)
{
	writeDevice(file, (struct device *)dev);
}

static int xenCompareDevice(struct xenDevice *dev1, struct xenDevice *dev2)
{
	return compareDevice( (struct device *)dev1, (struct device *)dev2);
}

struct xenDevice *xenNewDevice(struct xenDevice *old)
{
	struct xenDevice *ret;

	ret = malloc(sizeof(struct xenDevice));
	memset(ret, '\0', sizeof(struct xenDevice));
	ret = (struct xenDevice *) newDevice((struct device *) old, (struct device *) ret);
	ret->bus = BUS_XEN;
	ret->newDevice = xenNewDevice;
	ret->freeDevice = xenFreeDevice;
	ret->writeDevice = xenWriteDevice;
	ret->compareDevice = xenCompareDevice;
	return ret;
}

struct device *xenProbe(enum deviceClass probeClass, int probeFlags,
			struct device *devlist)
{
    struct xenDevice *xendev;
    if (probeClass & CLASS_HD) {
      DIR * dir;
      struct dirent * ent;
      char path[64], dev[64];
      struct stat sb;
      
      if (access("/sys/bus/xen/devices", R_OK)) 
        goto vbdout;

      dir = opendir("/sys/bus/xen/devices");
      while ((ent = readdir(dir))) {
	DIR *devdir;
	struct dirent *devent;
        if (strncmp("vbd-", ent->d_name, 4))
          continue;
        snprintf(path, 64, "/sys/bus/xen/devices/%s", ent->d_name);
	devdir = opendir(path);
	if (!devdir) continue;
	while ((devent = readdir(devdir))) {
		if (!strncmp(devent->d_name,"block:",6)) {
			xendev = xenNewDevice(NULL);
			xendev->device = strdup(devent->d_name+6);
			xendev->desc = strdup("Xen Virtual Block Device");
			xendev->type = CLASS_HD;
			xendev->driver = strdup("xenblk");
			if (devlist)
				xendev->next = devlist;
			devlist = (struct device *) xendev;
		}
	}
	closedir(devdir);
      }
      closedir(dir);
    }

 vbdout:

    if (probeClass & CLASS_NETWORK) {
      DIR * dir;
      struct dirent * ent;

      if (access("/sys/bus/xen/devices", R_OK))
        goto xvifout;

      dir = opendir("/sys/bus/xen/devices");
      while ((ent = readdir(dir))) {
        if (strncmp("vif-", ent->d_name, 4))
          continue;
        xendev = xenNewDevice(NULL);
        xendev->device = strdup("eth");
        xendev->desc = strdup("Xen Virtual Ethernet");
        xendev->type = CLASS_NETWORK;
        xendev->driver = strdup("xennet");
        if (devlist)
          xendev->next = devlist;
        devlist = (struct device *) xendev;
      }
      closedir(dir);
    }

 xvifout:
      
    return devlist;
}
