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

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <mntent.h>
#include <popt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/cdrom.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "kudzu.h"

#define _(a) a

#define MTAB		"/etc/mtab"
#define FSTAB		"/etc/fstab"
#define	FSTAB_DEVICES	"/etc/updfstab.conf"

#define PROGRAM_NAME	"updfstab"

#define CLASS_RESERVED_HACK 0

struct matchInfo {
    int type;
    const char * descPattern;
};

struct typeInfo {
    const char * name;
    int makeSymlink;
    int autoMount;
    int skip;
    int noFstab;
    int ignoreMulti;
    int defaultPartition;
    struct matchInfo * matches;
    int numMatches;
};

struct mountInfo {
    dev_t devNum;
    char * device;
    char * mountPoint;
    int kudzuGenerated;
    int shouldRemove;
    struct mountInfo * next;
};

struct probedDevice {
    const struct typeInfo * type;
    char * device;	    /* /dev entry		    */
    dev_t devNum;
    struct probedDevice * next;
};

struct entryToAdd {
    char * mountPoint;
    char * symlink;		    /* may be NULL */
    struct probedDevice * device;
    struct entryToAdd * next;
};

struct partitionEntry {
    char * device;
    int partition;
};

static struct stat fstabsb;

static int readMountTable(char * fileName, struct mountInfo ** mountListPtr);
static int isWritable(char * path);
static int getDeviceNum(char * path, dev_t * device);
static int probeDeviceType(struct partitionEntry * partList,
			   const struct typeInfo * type, 
			   struct device ** kudzuDevices,
			   struct probedDevice ** list);
static int removeDeadDevices(struct mountInfo * mountTable, 
			     struct probedDevice * deviceList);
static int probeAllDevices(struct partitionEntry * partList,
			   struct typeInfo * deviceInfoList,
			   int skipprobe,
			   struct probedDevice ** list);
static int buildAddList(struct mountInfo * mountedDevices, 
			struct mountInfo * mountTable, 
			struct probedDevice * deviceList,
			struct typeInfo * deviceInfoList,
			struct entryToAdd ** list,
			int normalize);
static int mountDevices(struct entryToAdd * addList, int test);
static int updateFstab(char * fileName, struct mountInfo * mountTable, 
		       struct entryToAdd * addList, int test);
/* returns NULL on error, NULL terminated list */
static struct partitionEntry * readPartitionList(void);

/* return the device number of the full disk (the one w/ the partition table) */
static dev_t fullDiskDevice(dev_t dev);

static dev_t fullDiskDevice(dev_t dev) {
    int maj = major(dev);
    int min = minor(dev);
    int mask = 0;
	
    switch (maj) {
      /* ide */
      case 3: case 22: case 33: case 34: case 56: case 57: case 88:
      case 89: case 90: case 91:
	mask = 0x3f;
	break;

      /* scsi */
      case 8: case 65: case 66: case 67: case 68: case 69: case 70: case 71:
        mask = 0x0f;
	break;

      /* smartarray */
      case 72: case 73: case 74: case 75: case 76: case 77: case 78: case 79:
      case 104: case 105: case 106: case 107: case 108: case 109: case 110: case 111:
	mask = 0x0f;
	break;
      
      /* floppy */
      case 2:
	mask = 0;
	break;
	    
      default:
	mask = 0;
	break;
    }

    return makedev(maj, (min & ~mask));
}

static int isWritable(char *dev) {
    int fd, capability;
    char path[50];
    
    snprintf(path,50,"/dev/%s",dev);
    
    fd = open(path, O_RDONLY|O_NONBLOCK);
    if (fd == -1) return 0;
    capability = ioctl (fd, CDROM_GET_CAPABILITY, &capability);
    close(fd);
    if (capability == -1) return 0;
    if (capability & (
		      0x2000 |  /* CD-R */
		      0x4000 |  /* CD-RW */
		      0x10000 | /* DVD-R */
		      0x20000   /* DVD-RAM */
		      ))
        return 1;
    return 0;
}

static int getDeviceNum(char * path, dev_t * device) {
    struct stat sb;

    if (stat(path, &sb)) {
	fprintf(stderr, _("cannot stat %s: %s\n"), path, strerror(errno));
	return 1;
    }

    if (!S_ISBLK(sb.st_mode)) {
	fprintf(stderr, _("unexpected file type for %s\n"), path);
	return 1;
    } 

    *device = sb.st_rdev;

    return 0;
}

static int readMountTable(char * fileName, struct mountInfo ** mountListPtr) {
    struct mountInfo * mountList = *mountListPtr;
    struct mntent * mntent;
    FILE * mtab;
    struct mountInfo * newMount;

    if (!(mtab = setmntent(fileName, "r"))) {
	fprintf(stderr, _("failed to open %s for read: %s\n"),
		fileName, strerror(errno));
	return 1;
    }

    while ((mntent = getmntent(mtab))) {
	/* Ignore mounts that aren't of /dev/something; we just don't care */
	if (strncmp(mntent->mnt_fsname, "/dev/", 5)) continue;

	newMount = malloc(sizeof(*newMount));
	newMount->kudzuGenerated = strstr(mntent->mnt_opts, "kudzu") != NULL;
	newMount->shouldRemove = 0;
	newMount->next = NULL;
	newMount->mountPoint = strdup(mntent->mnt_dir);
	newMount->device = strdup(mntent->mnt_fsname);

	if (getDeviceNum(mntent->mnt_fsname, &newMount->devNum))
	    return 1;

	if (!mountList) {
	    mountList = newMount;
	} else {
	    newMount->next = mountList;
	    mountList = newMount;
	}
    }

    endmntent(mtab);

    *mountListPtr = mountList;

    return 0;
}

static int probeDeviceType(struct partitionEntry * partList,
			   const struct typeInfo * type,
			   struct device ** kudzuDevices,
			   struct probedDevice ** list) {
    struct probedDevice * endOfList = *list;
    struct probedDevice * newDevice;
    struct probedDevice * devListPtr;
    struct partitionEntry * part;
    struct device ** aDevicePtr, * aDevice;
    int keepDevice, flag;
    char *devName;

    if (type->skip) return 0;

    while (endOfList && endOfList->next) endOfList = endOfList->next;

    for (aDevicePtr = kudzuDevices; *aDevicePtr; aDevicePtr++) {
	aDevice = *aDevicePtr;
	keepDevice = 1;

	if (aDevice->detached)
	    keepDevice = 0;

	if (!type->matches)
	    keepDevice = 0;
	    
        if (!(aDevice->bus == BUS_SCSI ||
	      aDevice->bus == BUS_IDE ||
	      aDevice->bus == BUS_MISC ||
	      aDevice->bus == BUS_FIREWIRE))
	    keepDevice = 0;
	
	if (!aDevice->device)
	    keepDevice = 0;

	if (keepDevice) {
	    int i;

	    keepDevice = 0;
	    for (i = 0; i < type->numMatches; i++) {
		if (aDevice->type == type->matches[i].type ||
		    /* CD-RW hack! */
		    ( aDevice->type == CLASS_CDROM &&
		      isWritable(aDevice->device) &&
		      type->matches[i].type == CLASS_RESERVED_HACK )
		    ) {
		    if (!type->matches[i].descPattern ||
		      strcasestr(aDevice->desc, type->matches[i].descPattern)) {
			keepDevice = 1;
		    }
		}
	    }
	}

	if (keepDevice) {
	    for (part = partList; part->device; part++) {
		if (!strcmp(part->device, aDevice->device)) break;
	    }

	    /* if there are multiple partitions on a device, bail. note
	       the kernel sorts /proc/partitions for us */
	    if (part->device && part[1].device &&
		!strcmp(part[1].device, aDevice->device) && !type->ignoreMulti) {
		continue;
	    }
		
	    devName = malloc(strlen(aDevice->device) + 10);
	    
	    if (part->device && !type->ignoreMulti)
		sprintf(devName, "/dev/%s%d", part->device,
			part->partition);
	    else if (type->defaultPartition)
		sprintf(devName, "/dev/%s%d", aDevice->device,
			type->defaultPartition);
	    else
		sprintf(devName, "/dev/%s", aDevice->device);
	    
	    /* possibly already probed this device w/a different pattern */
	    if (type->matches[0].type != CLASS_RESERVED_HACK)
	    for (flag = 0, devListPtr = *list; devListPtr;
		 devListPtr = devListPtr->next) {
		if (!strcmp(devListPtr->device, devName)) {
		    flag = 1;
		    break;
		}
	    }
	    
	    if (flag) {
		free(devName);
		continue;
	    }

	    newDevice = malloc(sizeof(*newDevice));
	    newDevice->device = devName;
	    newDevice->type = type;
	    newDevice->next = NULL;

	    if (!newDevice->type->noFstab &&
		getDeviceNum(newDevice->device, &newDevice->devNum))
		return 1;

	    if (endOfList) {
		endOfList->next = newDevice;
		endOfList = newDevice;
	    } else {
		*list = newDevice;
		endOfList = newDevice;
	    }
	}
    }

    return 0;
}

static int probeAllDevices(struct partitionEntry * partList,
			   struct typeInfo * deviceInfoList,
			   int skipprobe, 
			   struct probedDevice ** list) {
    const struct typeInfo * aType;
    struct device ** kudzuDevices;
    int x;
    struct stat sbuf;

    if (skipprobe) {
	if (stat("/etc/sysconfig/hwconf", &sbuf) == -1) return 0;
	kudzuDevices = readDevices("/etc/sysconfig/hwconf");
    } else {
	kudzuDevices = probeDevices(CLASS_CDROM | CLASS_HD | CLASS_FLOPPY,
				    BUS_IDE | BUS_SCSI |
				    BUS_MISC | BUS_FIREWIRE, 0);
    }
    if (!kudzuDevices) return 0;
    for (aType = deviceInfoList; aType->name; aType++)
	if (probeDeviceType(partList, aType, kudzuDevices, list)) return 1;

    for (x=0;kudzuDevices[x];x++)
	kudzuDevices[x]->freeDevice(kudzuDevices[x]);

    free(kudzuDevices);
    return 0;
}

/* Devices that we added to the mount table, but for which devices no
   longer exist, should be removed */
static int removeDeadDevices(struct mountInfo * mountTable, 
			     struct probedDevice * deviceList) {
    struct mountInfo * mnt;
    struct probedDevice * dev;

    for (mnt = mountTable; mnt; mnt = mnt->next) {
	if (!mnt->kudzuGenerated) continue;

	for (dev = deviceList; dev && (dev->devNum != mnt->devNum);
		dev = dev->next);
	if (!dev)
	    mnt->shouldRemove = 1;
    }

    return 0;
}

static int buildAddList(struct mountInfo * mountedDevices, 
			struct mountInfo * mountTable, 
			struct probedDevice * deviceList,
			struct typeInfo * deviceInfoList,
			struct entryToAdd ** list,
			int normalize) {
    struct probedDevice * device;
    struct mountInfo * mnt, * needle;
    int * mountCounters;
    int typeNum;
    char newMntPoint[PATH_MAX];
    char newSymLink[PATH_MAX];
    struct entryToAdd * endOfList = *list, *addition;
    int i;

    for (i = 0; deviceInfoList[i].name; i++);
    mountCounters = calloc(sizeof(*mountCounters), i);

    while (endOfList && endOfList->next) endOfList = endOfList->next;

    /* add new items to fstab */
    for (device = deviceList; device; device = device->next) {
	for (mnt = mountTable; mnt && 
		fullDiskDevice(mnt->devNum) != fullDiskDevice(device->devNum);
	     mnt = mnt->next);

	/* the device is there, and should be left there */
	if (mnt && (!mnt->kudzuGenerated || !normalize))
	    continue;

	typeNum = device->type - deviceInfoList;

	/* find a mountpoint to use */
	while (1) {
	    sprintf(newMntPoint, "/mnt/%s", device->type->name);

	    /* This is absolutely, totally moronic. We're only doing
	       this because iomega thinks it's a good idea. Which it's
	       not. */
	    if (mountCounters[typeNum] ||
		(newMntPoint[strlen(newMntPoint) - 1] == '.')) {
		sprintf(newSymLink, "%d", mountCounters[typeNum]);
		strcat(newMntPoint, newSymLink);
	    }
		
	    strcpy(newSymLink, newMntPoint + 5);

	    mountCounters[typeNum]++;

	    /* if this name is already used by us, we're done */
	    if (mnt && !strcmp(mnt->mountPoint, newMntPoint)) break;

	    /* if this name is already mounted, we can't use it unless it's
	       being used for this device */
	    for (needle = mountedDevices;
		    needle && strcmp(needle->mountPoint, newMntPoint);
		    needle = needle->next) ;
	    if (needle && needle->devNum != device->devNum) continue;

	    /* if this name is used for anything else in the fstab, and
	       it's staying but we're not normalizing, we can't use it */
	    for (needle = mountTable; 
		    needle && strcmp(needle->mountPoint, newMntPoint);
		    needle = needle->next) ;
	    if (needle && !needle->kudzuGenerated) continue;
	    if (needle && !normalize && !needle->shouldRemove) continue;

	    if (device->type->makeSymlink) {
		/* if this symlink is in the mount table, and is staying,
		   we can't use it [same idea as above ] */
		for (needle = mountTable; needle; needle = needle->next) {
		    if (strncmp("/dev/", needle->device, 5)) continue;
		    if (!strcmp(needle->device + 5, newSymLink)) break;
		}
	    }

	    if (needle && !needle->kudzuGenerated) continue;
	    if (needle && !normalize && !needle->shouldRemove) continue;

	    /* if we didn't find the symlink in the mount table, but it's
	       in the filesystem we'll just blow it away */

	    if (needle) {
		/* Normalize the entry. This is okay, because if we'd
		   seen this device in the device list already and it 
		   should be left alone, the mountCounter for this device
		   would be past it's entry. */
		needle->shouldRemove = 1;
	    }

	    /* we can use this one */
	    addition = malloc(sizeof(*addition));
	    addition->mountPoint = strdup(newMntPoint);

	    if (device->type->makeSymlink)
		addition->symlink = strdup(newSymLink);
	    else
		addition->symlink = NULL;

	    addition->device = device;
	    addition->next = NULL;

	    if (endOfList) {
		endOfList->next = addition;
		endOfList = addition;
	    } else {
		*list = addition;
		endOfList = addition;
	    }

	    /* if we normalized this entry, make sure we don't have a dup */
	    if (normalize)
		if (mnt) mnt->shouldRemove = 1;

	    break;
	}
    }

    return 0;
}

static int mountDevices(struct entryToAdd * addList, int test) {
    struct entryToAdd * addition;
    char * argv[] = { "/bin/mount", NULL, NULL };
    pid_t pid;
    int status;
    int errors = 0;

    if (test) return 1;

    for (addition = addList; addition; addition = addition->next) {
	if (addition->device->type->autoMount) {
	    argv[1] = addition->mountPoint;
	    
	    if (!(pid = fork())) {
		execv(argv[0], argv);
		_exit(1);
	    }

	    waitpid(pid, &status, 0);

	    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
		fprintf(stderr, _("/bin/mount failed\n"));
		errors++;
	    }
	}
    }

    return (errors != 0);
}

static int updateFstab(char * fileName, struct mountInfo * mountTable, 
		       struct entryToAdd * addList, int test) {
    int fd;
    struct stat before, after;
    char * fstab;
    FILE * output, * revoke;
    char * start, * end, * lineStart;
    char * outputFileName = NULL;
    char * revokeFileName = NULL;
    int keepLine;
    struct mountInfo * mnt;
    struct stat sb;
    char * deviceName;
    struct entryToAdd * addition;
    char symlinkSource[PATH_MAX];
    pid_t pid;
    int status;

    /* see if there is anything to do */
    fd = 0;
    if (addList) {
	fd = 1;
    } else  {
	for (mnt = mountTable; mnt; mnt = mnt->next)
	    if (mnt->shouldRemove) fd = 1;
    }

    if (!fd) {
	if (test) printf("(nothing to do)\n");
	return 0;
    }

    if ((fd = open(fileName, O_RDONLY)) < 0) {
	fprintf(stderr, _("failed to open %s for read: %s\n"),
		fileName, strerror(errno));
	return 1;
    }

    fstat(fd, &before);

    if (before.st_mtime != fstabsb.st_mtime ||
	    before.st_dev != fstabsb.st_dev ||
	    before.st_ino != fstabsb.st_ino ||
	    before.st_size != fstabsb.st_size) {
	    fprintf(stderr, _("%s changed during run -- aborting\n"),
		    fileName);
	    return 1;
    }

    fstab = alloca(before.st_size + 2);
    if (read(fd, fstab, before.st_size) != before.st_size) {
	fprintf(stderr, _("failed to read %s: %s\n"), fileName, 
		strerror(errno));
	exit(1);
    }

    close(fd);

    /* make sure there is a trailing \n\0 sequence */
    fstab[before.st_size] = '\0';

    if (fstab[before.st_size - 1] != '\n') {
	fstab[before.st_size] = '\n';
	fstab[before.st_size + 1] = '\0';
    }

    if (test) {
	output = stdout;
	outputFileName = NULL;

	revoke = stdout;
	revokeFileName = NULL;
    } else {
	outputFileName = alloca(strlen(fileName) + 12);
	strcpy(outputFileName, fileName);
	strcat(outputFileName, ".NEW.XXXXXX");

	if ((fd = mkstemp(outputFileName)) < 0 ||
	    !(output = fdopen(fd, "w"))) {
	    fprintf(stderr, _("failed to open %s: %s\n"),
		    outputFileName, strerror(errno));
	    return 1;
	}
	chmod(outputFileName,0644);

	revokeFileName = alloca(strlen(fileName) + 17);
	strcpy(revokeFileName, fileName);
	strcat(revokeFileName, ".REVOKE.XXXXXX");

	if ((fd = mkstemp(revokeFileName)) < 0 ||
	    !(revoke = fdopen(fd, "w"))) {
	    fprintf(stderr, _("failed to open %s: %s\n"),
		    revokeFileName, strerror(errno));
	    return 1;
	}
    }

    lineStart = fstab;

    /* copy what's there, removing things which need to disappear */
    while (*lineStart) {
	start = lineStart;
	end = strchr(lineStart, '\n');
	*end = '\0';

	while (*start && isspace(*start)) start++;

	if (*start == '#' || !*start) {
	    keepLine = 1;
	} else if (strncmp(start, "/dev/", 5)) {
	    keepLine = 1;
	} else {
	    char * chptr = start;

	    while (!isspace(*chptr) && *chptr) chptr++;
	    
	    if (isspace(*chptr)) {
		char * dev;
		dev_t devNum;

		dev = malloc(chptr - start + 1);
		strncpy(dev, start, chptr - start);
		dev[chptr - start] = '\0';

		if (getDeviceNum(dev, &devNum)) {
		    if (outputFileName) unlink(outputFileName);
		    if (revokeFileName) unlink(revokeFileName);
		    return 1;
		}

		for (mnt = mountTable; mnt && mnt->devNum != devNum;
		     mnt = mnt->next);

		if (!mnt || !mnt->shouldRemove)
		    keepLine = 1;
		else
		    keepLine = 0;
	    } else {
		keepLine = 1;
	    }
	}

	if (keepLine) {
	    fprintf(output, "%s\n", lineStart);
	} else {
	    if (!test) {
		/* this will fail harmlessly if the directory is in use */
		rmdir(mnt->mountPoint);

		if (!lstat(mnt->device, &sb) && S_ISLNK(sb.st_mode))
		    unlink(mnt->device);
	    }

	    if (test) 
		fprintf(revoke, "REVOKE: ");
	    fprintf(revoke, "%s %s auto defaults\n",
		    mnt->device, mnt->mountPoint);
	}


	lineStart = end + 1;
    }

    if (!test)
	fclose(revoke);

    for (addition = addList; addition; addition = addition->next) {
	if (!addition->symlink) {
	    deviceName = addition->device->device;
	} else {
	    sprintf(symlinkSource, "/dev/%s", addition->symlink);

	    if (!test) {
		unlink(symlinkSource);
		if (symlink(addition->device->device, symlinkSource)) {
		    fprintf(stderr, _("failed to create %s -> %s symlink: %s"),
			    symlinkSource, addition->device->device, 
			    strerror(errno));
		    if (outputFileName) unlink(outputFileName);
		    if (revokeFileName) unlink(revokeFileName);
		    return 1;
		}
	    }

	    deviceName = symlinkSource;
	}

	if (!test) {
	    if (!addition->device->type->noFstab &&
		mkdir(addition->mountPoint, 0775) && errno != EEXIST) {
		fprintf(stderr, _("failed to create %s: %s\n"),
			addition->mountPoint, strerror(errno));

		if (outputFileName) unlink(outputFileName);
		if (revokeFileName) unlink(revokeFileName);
		return 1;
	    }
	}

	/* CD-RW hack! */
	if (!addition->device->type->noFstab)
	    fprintf(output, "%-23s %-23s %-7s %-15s %d %d\n",
		    deviceName,
		    addition->mountPoint,
		    !(strcmp(addition->device->type->name, "cdrom") &&
		      strcmp(addition->device->type->name, "cdwriter") &&
		      strcmp(addition->device->type->name, "cdrw")) ?
		    "udf,iso9660" : "auto",
		    !(strcmp(addition->device->type->name, "cdrom") &&
		      strcmp(addition->device->type->name, "cdwriter") &&
		      strcmp(addition->device->type->name, "cdrw")) ?
		    "noauto,owner,kudzu,ro" : "noauto,owner,kudzu",
		    0, 0);
    }

    if (!test) {
	fclose(output);

	stat(fileName, &after);

	if (before.st_mtime != after.st_mtime ||
	    before.st_dev != after.st_dev ||
	    before.st_ino != after.st_ino ||
	    before.st_size != after.st_size) {
	    fprintf(stderr, _("%s changed during run -- aborting\n"),
		    fileName);
	    unlink(outputFileName);
	    unlink(revokeFileName);

	    return 1;
	}

	/* do this before moving onto /etc/fstab; this keeps magicdev
	   from noticing the change before the permissions are correct */

	pid = fork();
	if (!pid) {
	    execl("/sbin/pam_console_apply", "/sbin/pam_console_apply",
		  "-f", revokeFileName, "-r", NULL);
	    printf("failed\n");
	    exit(-1);
	}
	waitpid(pid, &status, 0);

	if (!WIFEXITED(status) || WEXITSTATUS(status))
	    fprintf(stderr, _("/sbin/pam_console_apply -r failed\n"));

	pid = fork();
	if (!pid) {
	    execl("/sbin/pam_console_apply", "/sbin/pam_console_apply",
		  "-f", outputFileName, NULL);
	    printf("failed\n");
	    exit(-1);
	}

	waitpid(pid, &status, 0);
	if (!WIFEXITED(status) || WEXITSTATUS(status))
	    fprintf(stderr, _("/sbin/pam_console_apply failed\n"));

	if (rename(outputFileName, fileName)) {
	    fprintf(stderr, _("failed to rename %s to %s: %s\n"),
		    outputFileName, fileName, strerror(errno));
	    unlink(revokeFileName);
	    unlink(outputFileName);
	    return 1;
	}
    }
    if (revokeFileName) unlink(revokeFileName);

    return 0;
}

static struct partitionEntry * readPartitionList(void) {
    FILE * f;
    struct partitionEntry * list = NULL;
    int numItems = 0;
    char line[256];
    int major, minor;
    long long blocks;
    char name[256];
    char * frc;
    char * chptr;
    char lastFullDevice[256];
    char *iobuf = NULL;
    long long lastBlockCount = -1;
    int len, fd;
    
    fd = open("/proc/partitions", O_RDONLY, 0);
    if (fd < 0) {
	fprintf(stderr, _("failed to open /proc/partitions: %s\n"),
		strerror(errno));
	return NULL;
    }
    iobuf = (char *)malloc(16384);
    if (!iobuf) {
	    fprintf(stderr, _("out of memory.\n"));
	    return NULL;
    }
    len = read(fd, iobuf, 16384);
    if (len < 0) {
	fprintf(stderr, _("failed to read /proc/partitions: %s\n"),
		strerror(errno));
	close(fd);
	return NULL;
    }
	    
    f = fmemopen(iobuf, len, "r");
    
    /* first two lines are junk */
    frc = fgets(line, sizeof(line), f);
    if (frc) frc = fgets(line, sizeof(line), f);

    if (frc) frc = fgets(line, sizeof(line), f);
    while (frc) {
	if (sscanf(line, "%d %d %lld %s", &major, &minor, &blocks, name) != 4) {
	    line[strlen(line) - 1] = '\0';  /* chop */
	    fprintf(stderr, "invalid line in /proc/partitions: '%s'\n", line);
	    if (iobuf) free(iobuf);

	    /* XXX memory leak */
	    return NULL;
	}

	/* throw away devices w/ aren't partitions, or don't begin with a
	   letter (because we don't know what those are) */
	chptr = name + strlen(name) - 1;
	if (!isdigit(*name) && isdigit(*chptr)) {
	    while (isdigit(*chptr)) chptr--;
	    chptr++;

	    /* if the blocks don't add up properly, give up on this device */
	    if (blocks < 0 || (blocks > lastBlockCount))
		lastBlockCount = -1;

	    if (lastBlockCount != -1) {
		list = realloc(list, (numItems + 1) * sizeof(*list));
		list[numItems].partition = strtol(chptr, NULL, 10);
		*chptr = '\0';
		list[numItems].device = strdup(name);
		numItems++;
	    }
	} else {
	    strcpy(lastFullDevice, name);
	    lastBlockCount = blocks;
	}

	frc = fgets(line, sizeof(line), f);
    }

    if (frc) {
	fprintf(stderr, _("error reading /proc/partitions: %s\n"),
		strerror(errno));

	/* XXX memory leak */

	return NULL;
    }

    list = realloc(list, (numItems + 1) * sizeof(*list));
    list[numItems++].device = NULL;

    fclose(f);
    free(iobuf);
    close(fd);

    return list;
}

struct cfInfo {
    const char * name;
    int itemRequired;
    enum { STRING, STRINGOPT, BOOLEAN, INTEGER, NONE } arg1, arg2;
};

/* automount is disabled for now; it's not trivial to get autounmounting
   right as we have to match strings from the fstab against strings from
   the configuration to see if automatic unmounting is the right thing to
   do or not */
struct cfInfo cfOptions[] = {
    /* {	"automount",	0,  BOOLEAN,    NONE },*/
    {	"device",	0,  STRING,	STRING },
    {	"ignoremulti",  0,  BOOLEAN,    NONE },
    {	"include",	0,  STRING,	NONE  },
    {	"match",	1,  STRING,	STRINGOPT },
    {	"nofstab",	0,  BOOLEAN,	NONE  },
    {	"partition",	0,  INTEGER,    NONE  },
    {	"skip",		0,  BOOLEAN,    NONE  },
    {	"symlink",	0,  BOOLEAN,    NONE  },
    {	"}",		1,  NONE,	NONE  },
};

const int cfNumOptions = sizeof(cfOptions) / sizeof(struct cfInfo);

/* doesn't free things properly on error */
struct typeInfo * readConfigFile(const char * path, 
				    struct typeInfo * types,
				    struct typeInfo * defaults) {
    int fd;
    char * buf;
    struct stat sb;
    char * start;
    char * end;
    int line = 0;
    int lineArgc;
    const char ** lineArgv;
    int i, j;
    int intArg1;
    struct typeInfo defaultStruct = { NULL, 0, 0, 0, 0, 0, 0, NULL, 0 };
    struct typeInfo * item;

    if (!defaults) 
	defaults = &defaultStruct;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
	fprintf(stderr, "error opening %s: %s\n", path, strerror(errno));
	return NULL;
    }

    fstat(fd, &sb);
    buf = alloca(sb.st_size + 2);
    if (read(fd, buf, sb.st_size) != sb.st_size) {
	fprintf(stderr, "error reading %s: %s\n", path, strerror(errno));
	return NULL;
    }

    if (buf[sb.st_size - 1] != '\n')
	buf[sb.st_size++] = '\n';
    buf[sb.st_size] = '\0';

    item = defaults;

    start = buf;
    while (*start) {
	line++;

	end = strchr(start, '\n');
	*end = '\0';

	while (isspace(*start)) start++;
	if (start == end) {
	    start++;
	    continue;
	}

	if (poptParseArgvString(start, &lineArgc, &lineArgv)) {
	    fprintf(stderr, "%s:%d parse error\n", path, line);
	    return NULL;
	}

	if (*lineArgv[0] == '#') {
	    free(lineArgv);
	    start = end + 1;
	    continue;
	}

	for (i = 0; i < cfNumOptions; i++)
	    if (!strcmp(cfOptions[i].name, lineArgv[0])) break;

	if (i == cfNumOptions) {
	    fprintf(stderr, "%s:%d unknown directive %s\n", path, 
		    line, lineArgv[0]);
	    free(lineArgv);
	    return NULL;
	}

	j = 1;
	if (cfOptions[i].arg1 != NONE) j++;
	if (cfOptions[i].arg2 != NONE) j++;

	if (lineArgc != j) {
	    if (cfOptions[i].arg2 == STRINGOPT) j--;

	    if (lineArgc != j) {
		if (lineArgc < j)
		    fprintf(stderr, "%s:%d missing arguments\n", path, line);
		else
		    fprintf(stderr, "%s:%d unexpected arguments\n", path, line);

		free(lineArgv);
		return NULL;
	    }
	}

	if (cfOptions[i].itemRequired && item == defaults) {
	    fprintf(stderr, "%s:%d %s may only be used in a device definition\n",
		    path, line, lineArgv[0]);
	    free(lineArgv);
	    return NULL;
	}

	switch (cfOptions[i].arg1) {
	  case INTEGER:
	    intArg1 = strtol(lineArgv[1], &start, 0);
	    if (*start) {
		fprintf(stderr, "%s:%d integer expected\n", path, line);
		free(lineArgv);
		return NULL;
	    }
	    break;

	  case BOOLEAN:
	    if (!strcmp(lineArgv[1], "false") || 
		!strcmp(lineArgv[1], "no") || !strcmp(lineArgv[1], "0")) 
	        intArg1 = 0;
	    else if (!strcmp(lineArgv[1], "true") || 
		     !strcmp(lineArgv[1], "yes") || !strcmp(lineArgv[1], "1")) 
	        intArg1 = 1;
	    else {
		fprintf(stderr, "%s:%d boolean expected\n", path, line);
		free(lineArgv);
		return NULL;
	    }

	  default:		/* for gcc */
	    ;
	}

	if (!strcmp(lineArgv[0], "include")) {
	    if (item != defaults) {
		fprintf(stderr, "%s:%d include may not be used in a device section\n",
			path, line);
		free(lineArgv);
		return NULL;
	    }
	    types = readConfigFile(lineArgv[1], types, defaults);
	    if (!types) return NULL;
	} else if (!strcmp(lineArgv[0], "partition")) {
	    if (intArg1 < 0 || intArg1 > 4) {
		fprintf(stderr, "%s:%d value between 0 and 4 expected\n",
			path, line);
		free(lineArgv);
		return NULL;
	    }

	    item->defaultPartition = intArg1;
	} else if (!strcmp(lineArgv[0], "automount")) {
	    item->autoMount = intArg1;
	} else if (!strcmp(lineArgv[0], "skip")) {
	    item->skip = intArg1;
	} else if (!strcmp(lineArgv[0], "nofstab")) {
	    item->noFstab = intArg1;	
	} else if (!strcmp(lineArgv[0], "symlink")) {
	    item->makeSymlink = intArg1;
	} else if (!strcmp(lineArgv[0], "ignoremulti")) {
            item->ignoreMulti = intArg1;
	} else if (!strcmp(lineArgv[0], "device")) {
	    if (strcmp(lineArgv[2], "{")) {
		fprintf(stderr, "%s:%d { expected\n", path, line);
		free(lineArgv);
		return NULL;
	    } else if (item != defaults) {
		fprintf(stderr, "%s:%d device directives may not be nested\n",
			path, line);
		free(lineArgv);
		return NULL;
	    }

	    /* see if we've already started defining this device */
	    for (i = 0; types[i].name; i++)
		if (!strcmp(types[i].name, lineArgv[1])) break;

	    if (!types[i].name) {
		types = realloc(types, sizeof(*types) * (i + 2));

		item = types + i;
		*item = *defaults;
		item->name = strdup(lineArgv[1]);

		item[1].name = NULL;
	    } else {
		item = types + i;
	    }
	} else if (!strcmp(lineArgv[0], "match")) {
	    i = item->numMatches++;
	    item->matches = realloc(item->matches, 
				    sizeof(*item->matches) * item->numMatches);

	    if (lineArgv[2])
		item->matches[i].descPattern = strdup(lineArgv[2]);
	    else 
		item->matches[i].descPattern = NULL;

	    if (!strcmp(lineArgv[1], "hd"))
		item->matches[i].type = CLASS_HD;
	    else if (!strcmp(lineArgv[1], "cdrom"))
		item->matches[i].type = CLASS_CDROM;
	    else if (!strcmp(lineArgv[1], "floppy"))
		item->matches[i].type = CLASS_FLOPPY;
	    /* CD-RW hack! */
	    else if (!strcmp(lineArgv[1], "cdwriter"))
	        item->matches[i].type = CLASS_RESERVED_HACK;
	    else {
		fprintf(stderr, "%s:%d unknown device class %s\n",
			path, line, lineArgv[1]);
		free(lineArgv);
		return NULL;
	    }
	} else if (!strcmp(lineArgv[0], "}")) {
	    item = defaults;
	}

	free(lineArgv);
	start = end + 1;
    }

    if (item != defaults) {
	fprintf(stderr, "%s:%d } expected for device %s\n",
		path, line, item->name);
	return NULL;
    }

    return types;

}

int main(int argc, const char * argv[]) {
    struct mountInfo * mountList = NULL;
    struct mountInfo * mountTable = NULL;
    struct probedDevice * devices = NULL;
    struct entryToAdd * addList = NULL;
    struct partitionEntry * partitionList;
    struct typeInfo * types = NULL;
    int test = 0, normalize = 0, skipprobe = 0;
    int rc;
    poptContext optCon;
    char * configFile = FSTAB_DEVICES;
    struct poptOption myOptions[] = {
	{ "config", 'c', POPT_ARG_STRING, &configFile, 0,
		"configuration file to use instead of " FSTAB_DEVICES,
		"path" },
	{ "normalize", 'n', 0, &normalize, 0, 
		"update fstab entries to match device probing order" },
	{ "skipprobe", 's', 0, &skipprobe, 0,
	  "skip probing for new devices and read stored configuration instead" },
	{ "test", 't',0, &test, 0,
		"don't update anything, display new fstab on stdout" },
	POPT_AUTOHELP
	{ NULL, '\0', 0, NULL },
    };

    optCon = poptGetContext(PROGRAM_NAME, argc, argv, myOptions, 0);
    poptReadDefaultConfig(optCon, 1);

    if ((rc = poptGetNextOpt(optCon)) < -1) {
	fprintf(stderr, "%s: bad argument %s: %s\n", PROGRAM_NAME,
		poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
		poptStrerror(rc));

    }

    poptFreeContext(optCon);

    if (access(FSTAB, W_OK) && !test) {
	perror("Unable to open /etc/fstab for writing");
	return 1;
    }

    types = calloc(sizeof(*types), 1);
    if (!(types = readConfigFile(configFile, types, NULL))) return 1;

    if (readMountTable(MTAB, &mountList)) return 1;
    stat(FSTAB,&fstabsb);
    if (readMountTable(FSTAB, &mountTable)) return 1;

    if (!(partitionList = readPartitionList())) return 1;
    if (probeAllDevices(partitionList, types, skipprobe, &devices)) return 1;

    if (removeDeadDevices(mountTable, devices)) return 1;
    if (buildAddList(mountList, mountTable, devices, types, &addList, 
		     normalize)) 
	return 1;

    if (updateFstab(FSTAB, mountTable, addList, test)) return 1;
    if (mountDevices(addList, test)) return 1;

    return 0;
}
