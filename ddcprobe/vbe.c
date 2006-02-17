#include <sys/types.h>
#include <sys/io.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <limits.h>
#include <ctype.h>
#include "common.h"
#include "lrmi.h"
#include "vbe.h"
#ident "$Id: vbe.c,v 1.11 2006/02/14 03:59:46 notting Exp $"

/* The *actual* vbe_info as pulled from the BIOS.
 * union {
 * 	struct {
 * 		u_int16_t x;
 * 		u_int16_t y;
 * 	} addr;
 * 	const char *z;
 * } __attribute__((packed))
 * 
 * does *not* do what you want on x86_64.
 */
struct __vbe_info {
        unsigned char signature[4];
        unsigned char version[2];
	struct {
		u_int16_t ofs;
		u_int16_t seg;
	} oem_name;
        u_int32_t capabilities;
	struct {
		u_int16_t ofs;
		u_int16_t seg;
	} mode_list;
        u_int16_t memory_size;
        /* VESA 3.0+ */
        u_int16_t vbe_revision;
	struct {
		u_int16_t ofs;
		u_int16_t seg;
	} vendor_name;
	struct {
		u_int16_t ofs;
		u_int16_t seg;
	} product_name;
	struct {
		u_int16_t ofs;
		u_int16_t seg;
	} product_revision;
        char reserved1[222];
        char reserved2[256];
} __attribute__ ((packed));

void vbecopy(struct vbe_info *dst, struct __vbe_info *src)
{
	memcpy(dst->signature,src->signature,4);
	memcpy(dst->version,src->version,2);
	dst->capabilities = src->capabilities;
	dst->mode_list.addr.ofs = src->mode_list.ofs;
	dst->mode_list.addr.seg = src->mode_list.seg;
	dst->vendor_name.addr.ofs = src->vendor_name.ofs;
	dst->vendor_name.addr.seg = src->vendor_name.seg;
	dst->product_name.addr.ofs = src->product_name.ofs;
	dst->product_name.addr.seg = src->product_name.seg;
	dst->product_revision.addr.ofs = src->product_revision.ofs;
	dst->product_revision.addr.seg = src->product_revision.seg;
	dst->memory_size = src->memory_size;
	dst->vbe_revision = src->vbe_revision;
	memcpy(dst->reserved1, src->reserved1, 222);
	memcpy(dst->reserved2, src->reserved2, 256);
}


/* srcurn information about a particular video mode. */
struct vbe_mode_info *vbe_get_mode_info(u_int16_t mode)
{
	struct LRMI_regs regs;
	char *mem;
	struct vbe_mode_info *ret = NULL;

	/* Initialize LRMI. */
	if(LRMI_init() == 0) {
		return NULL;
	}

	/* Allocate a chunk of memory. */
	mem = LRMI_alloc_real(sizeof(struct vbe_mode_info));
	if(mem == NULL) {
		return NULL;
	}
	memset(mem, 0, sizeof(struct vbe_mode_info));

	memset(&regs, 0, sizeof(regs));
	regs.eax = 0x4f01;
	regs.ecx = mode;
	regs.es = ((u_int32_t)mem) >> 4;
	regs.edi = ((u_int32_t)mem) & 0x0f;

	/* Do it. */
	iopl(3);
	ioperm(0, 0x400, 1);

	if(LRMI_int(0x10, &regs) == 0) {
		LRMI_free_real(mem);
		return NULL;
	}

	/* Check for successful return. */
	if((regs.eax & 0xffff) != 0x004f) {
		LRMI_free_real(mem);
		return NULL;
	}

	/* Get memory for return. */
	ret = malloc(sizeof(struct vbe_mode_info));
	if(ret == NULL) {
		LRMI_free_real(mem);
		return NULL;
	}

	/* Copy the buffer for return. */
	memcpy(ret, mem, sizeof(struct vbe_mode_info));

	/* Clean up and return. */
	LRMI_free_real(mem);
	return ret;
}

/* Get VBE info. */
struct vbe_info *vbe_get_vbe_info()
{
	struct LRMI_regs regs;
	unsigned char *mem;
	struct vbe_info *ret = NULL;
	struct __vbe_info *biosdata;
	char *tmp;
	int i;

	/* Initialize LRMI. */
	if(LRMI_init() == 0) {
		return NULL;
	}

	/* Allocate a chunk of memory. */
	mem = LRMI_alloc_real(sizeof(struct vbe_mode_info));
	if(mem == NULL) {
		return NULL;
	}
	memset(mem, 0, sizeof(struct vbe_mode_info));

	/* Set up registers for the interrupt call. */
	memset(&regs, 0, sizeof(regs));
	regs.eax = 0x4f00;
	regs.es = ((u_int32_t)mem) >> 4;
	regs.edi = ((u_int32_t)mem) & 0x0f;
	memcpy(mem, "VBE2", 4);

	/* Do it. */
	iopl(3);
	ioperm(0, 0x400, 1);

	if(LRMI_int(0x10, &regs) == 0) {
		LRMI_free_real(mem);
		return NULL;
	}

	/* Check for successful return code. */
	if((regs.eax & 0xffff) != 0x004f) {
		LRMI_free_real(mem);
		return NULL;
	}

	/* Get memory to return the information. */
	ret = malloc(sizeof(struct vbe_info));
	if(ret == NULL) {
		LRMI_free_real(mem);
		return NULL;
	}
	biosdata = (struct __vbe_info *)mem;
	vbecopy(ret, biosdata);

	/* Set up pointers to usable memory. */
	ret->mode_list.list = (u_int16_t*) ((biosdata->mode_list.seg << 4) +
					    (biosdata->mode_list.ofs));
	ret->oem_name.string = (char*) ((biosdata->oem_name.seg << 4) +
					(biosdata->oem_name.ofs));

	/* Snip, snip. */
	tmp = strdup(ret->oem_name.string); /* leak */
	while(((i = strlen(tmp)) > 0) && isspace(tmp[i - 1])) {
		tmp[i - 1] = '\0';
	}
	ret->oem_name.string = tmp;

	/* Set up pointers for VESA 2.0+ strings. */
	if(ret->version[1] >= 2) {

		/* Vendor name. */
		ret->vendor_name.string = (char*)
			 ((biosdata->vendor_name.seg << 4)
			+ (biosdata->vendor_name.ofs));

		tmp = strdup(ret->vendor_name.string); /* leak */
		while(((i = strlen(tmp)) > 0) && isspace(tmp[i - 1])) {
			tmp[i - 1] = '\0';
		}
		ret->vendor_name.string = tmp;

		/* Product name. */
		ret->product_name.string = (char*)
			 ((biosdata->product_name.seg << 4)
			+ (biosdata->product_name.ofs));

		tmp = strdup(ret->product_name.string); /* leak */
		while(((i = strlen(tmp)) > 0) && isspace(tmp[i - 1])) {
			tmp[i - 1] = '\0';
		}
		ret->product_name.string = tmp;

		/* Product revision. */
		ret->product_revision.string = (char*)
			 ((biosdata->product_revision.seg << 4)
			+ (biosdata->product_revision.ofs));

		tmp = strdup(ret->product_revision.string); /* leak */
		while(((i = strlen(tmp)) > 0) && isspace(tmp[i - 1])) {
			tmp[i - 1] = '\0';
		}
		ret->product_revision.string = tmp;
	}

	/* Cleanup. */
	LRMI_free_real(mem);
	return ret;
}

/* Check if EDID queries are suorted. */
int get_edid_supported()
{
	struct LRMI_regs regs;
	int ret = 0;

	/* Initialize LRMI. */
	if(LRMI_init() == 0) {
		return 0;
	}

	memset(&regs, 0, sizeof(regs));
	regs.eax = 0x4f15;
	regs.ebx = 0x0000;
	regs.es = 0x3000;
	regs.edi = 0x3000;

	/* Do it. */
	iopl(3);
	ioperm(0, 0x400, 1);

	if (LRMI_int(0x10, &regs) == 0) {
		return 0;
	}

	/* Check for successful return. */
	if((regs.eax & 0xff) == 0x4f) {
		/* Supported. */
		ret = 1;
	} else {
		/* Not supported. */
		ret = 0;
	}

	/* Clean up and return. */
	return ret;
}

/* Get EDID info. */
struct edid1_info *get_edid_info()
{
	struct LRMI_regs regs;
	unsigned char *mem;
	struct edid1_info *ret = NULL;
	u_int16_t man;

	/* Initialize LRMI. */
	if(LRMI_init() == 0) {
		return NULL;
	}

	/* Allocate a chunk of memory. */
	mem = LRMI_alloc_real(sizeof(struct edid1_info));
	if(mem == NULL) {
		return NULL;
	}
	memset(mem, 0, sizeof(struct edid1_info));

	memset(&regs, 0, sizeof(regs));
	regs.eax = 0x4f15;
	regs.ebx = 0x0001;
	regs.es = ((u_int32_t)mem) >> 4;
	regs.edi = ((u_int32_t)mem) & 0x0f;

	/* Do it. */
	iopl(3);
	ioperm(0, 0x400, 1);

	if(LRMI_int(0x10, &regs) == 0) {
		LRMI_free_real(mem);
		return NULL;
	}

#if 0
	/* Check for successful return. */
	if((regs.eax & 0xffff) != 0x004f) {
		LRMI_free_real(mem);
		return NULL;
	}
#elseif
	/* Check for successful return. */
	if((regs.eax & 0xff) != 0x4f) {
		LRMI_free_real(mem);
		return NULL;
	}
#endif

	/* Get memory for return. */
	ret = malloc(sizeof(struct edid1_info));
	if(ret == NULL) {
		LRMI_free_real(mem);
		return NULL;
	}

	/* Copy the buffer for return. */
	memcpy(ret, mem, sizeof(struct edid1_info));

	memcpy(&man, &ret->manufacturer_name, 2);
	man = ntohs(man);
	memcpy(&ret->manufacturer_name, &man, 2);

	LRMI_free_real(mem);
	return ret;
}

