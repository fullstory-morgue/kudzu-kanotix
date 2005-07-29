/* Copyright 1999-2002 Red Hat, Inc.
 *
 * This software may be freely redistributed under the terms of the GNU
 * public license.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pciserial.h"

struct SerialPort
{
  int index;
  unsigned long port;
  unsigned long irq;
};

static struct SerialPort *serials;
static int num_serials = -1;

static void
InitSerials ()
{
  FILE *fp;
  char buffer [256];
  char *uart;
  char *port;
  char *irq;
  char *sp;
  int i;

  fp = fopen ("/proc/tty/driver/serial", "r");
  if (!fp)
    return;

  num_serials = 0;
  while (fgets (buffer, sizeof (buffer), fp))
    {
      if (strcasestr (buffer, "uart"))
        num_serials++;
    }

  if (!num_serials)
    {
      fclose (fp);
      return;
    }
  
  serials = malloc (sizeof (struct SerialPort) * num_serials);
  if (!serials)
    {
      perror ("malloc");
      num_serials = 0;
      fclose (fp);
      return;
    }

  i = 0;
  rewind (fp);
  while (i < num_serials && fgets (buffer, sizeof (buffer), fp))
    {
      uart = strcasestr (buffer, "uart");
      if (uart)
	{
	  port = strcasestr (buffer, "port");
	  irq = strcasestr (buffer, "irq");
	  uart = strchr (uart, ':');
	  uart++;
	  sp = strchr (uart, ' ');
	  *sp = '\0';
	  serials[i].index = serials[i].port = serials[i].irq = 0;
	  if (strcasecmp (uart, "unknown") && port && irq)
	    {
	      serials [i].index = strtol (buffer, NULL, 10);
	      serials [i].port = strtoul (port+5, NULL, 16);
	      serials [i].irq = strtoul (irq+4, NULL, 10);
	      i++;
	    }
	}
    }
  num_serials = i;
  fclose (fp);
}

void
checkPCISerial (struct pciDevice *pci, struct pci_dev *dev)
{
  unsigned long io_base [6];
  int i, j;
  char buffer [256];

  if (num_serials == -1)
    InitSerials ();

  if (num_serials == 0)
    return;

  /* Find out our io ports. */
  for (i = 0; i < 6; i++)
    {
      io_base [i] = dev->base_addr [i];
      if (io_base [i] & PCI_BASE_ADDRESS_SPACE_IO)
        io_base [i] &= PCI_BASE_ADDRESS_IO_MASK;
    }

  /* Match irq and io ports. */
  for (i = 0; i < num_serials; i++)
    {
      /* Check irq first. */
      if (serials [i].irq != dev->irq)
	continue;

      /* Check io ports. */
      for (j = 0; j < 6; j++)
	{
	  if (io_base [j] <= serials [i].port
	      && serials [i].port <= io_base [j] + 127)
	    {
	      snprintf (buffer, sizeof (buffer), "ttyS%d",
			serials [i].index);
	      pci->device = strdup (buffer);
	      return;
	    }
	}
    }
}
