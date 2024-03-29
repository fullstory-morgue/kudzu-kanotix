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

#ifndef _KUDZU_DDC_H_
#define _KUDZU_DDC_H_

#include "device.h"

struct ddcDevice {
    /* common fields */
    struct device *next;	/* next device in list */
	int index;
    enum deviceClass type;	/* type */
    enum deviceBus bus;		/* bus it's attached to */
    char * device;		/* device file associated with it */
    char * driver;		/* driver to load, if any */
    char * desc;		/* a description */
    int detached;
    void * classprivate;
    /* ddc-specific fields */
    struct ddcDevice *(*newDevice) (struct ddcDevice *dev);
    void (*freeDevice) (struct ddcDevice *dev);
    void (*writeDevice) (FILE *file, struct ddcDevice *dev);
    int (*compareDevice) (struct ddcDevice *dev1, struct ddcDevice *dev2);
    char * id;
    int horizSyncMin;
    int horizSyncMax;
    int vertRefreshMin;
    int vertRefreshMax;
    int *modes;
    long mem;
    int physicalWidth;
    int physicalHeight;
};

struct ddcDevice *ddcNewDevice(struct ddcDevice *dev);
struct device *ddcProbe(enum deviceClass probeClass, int probeFlags,
			struct device *devlist);
int ddcReadDrivers(char *filename);
void ddcFreeDrivers();
#endif
