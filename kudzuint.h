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


#ifndef _KUDZU_INTERNAL_H_
#define _KUDZU_INTERNAL_H_

char *__bufFromFd(int fd);
char *__readString(char *name);
int __readHex(char *name);
int __readInt(char *name);
int __getNetworkDevAndAddr(struct device *dev, char *path);
#endif
