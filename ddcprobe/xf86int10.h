/* $XFree86: xc/programs/Xserver/hw/xfree86/int10/xf86int10.h,v 1.23 2002/04/04 14:05:51 eich Exp $ */

/*
 *                   XFree86 int10 module
 *   execute BIOS int 10h calls in x86 real mode environment
 *                 Copyright 1999 Egbert Eich
 */

#ifndef _XF86INT10_H
#define _XF86INT10_H


#define SEG_ADDR(x) (((x) >> 4) & 0x00F000)
#define SEG_OFF(x) ((x) & 0x0FFFF)

#define SET_BIOS_SCRATCH     0x1
#define RESTORE_BIOS_SCRATCH 0x2

#define CARD8  unsigned char
#define CARD16 unsigned short
#define CARD32 unsigned int
#define pointer void *
#define IOADDRESS void *
#define Bool int

#define X86_TF_MASK             0x00000100
#define X86_IF_MASK             0x00000200
#define X86_IOPL_MASK           0x00003000
#define X86_NT_MASK             0x00004000
#define X86_VM_MASK             0x00020000
#define X86_AC_MASK             0x00040000
#define X86_VIF_MASK            0x00080000      /* virtual interrupt flag */
#define X86_VIP_MASK            0x00100000      /* virtual interrupt pending */
#define X86_ID_MASK             0x00200000


/* int10 info structure */
typedef struct {
    int entityIndex;
    int scrnIndex;
    pointer cpuRegs;
    CARD16  BIOSseg;
    CARD16  inb40time;
    char * BIOSScratch;
    int Flags;
    pointer private;
    struct _int10Mem* mem;
    int num;
    int ax;
    int bx;
    int cx;
    int dx;
    int si;
    int di;
    int es;
    int bp;
    int flags;
    int stackseg;
    //PCITAG Tag;
    IOADDRESS ioBase;
} xf86Int10InfoRec, *xf86Int10InfoPtr;

typedef struct _int10Mem {
    CARD8(*rb)(xf86Int10InfoPtr, int);
    CARD16(*rw)(xf86Int10InfoPtr, int);
    CARD32(*rl)(xf86Int10InfoPtr, int);
    void(*wb)(xf86Int10InfoPtr, int, CARD8);
    void(*ww)(xf86Int10InfoPtr, int, CARD16);
    void(*wl)(xf86Int10InfoPtr, int, CARD32);
} int10MemRec, *int10MemPtr;

typedef struct {
    CARD8 save_msr;
    CARD8 save_pos102;
    CARD8 save_vse;
    CARD8 save_46e8;
} legacyVGARec, *legacyVGAPtr;

//typedef struct {
    //BusType bus;
    //union {
	//struct {
	    //int bus;
	    //int dev;
	    //int func;
	////} pci;
	//int legacy;
    //} location;
////} xf86int10BiosLocation, *xf86int10BiosLocationPtr;
    
/* OS dependent functions */
xf86Int10InfoPtr xf86InitInt10(int entityIndex);
xf86Int10InfoPtr xf86ExtendedInitInt10(int entityIndex, int Flags);
void xf86FreeInt10(xf86Int10InfoPtr pInt);
void *xf86Int10AllocPages(xf86Int10InfoPtr pInt, int num, int *off);
void xf86Int10FreePages(xf86Int10InfoPtr pInt, void *pbase, int num);
pointer xf86int10Addr(xf86Int10InfoPtr pInt, CARD32 addr);

/* x86 executor related functions */
void xf86ExecX86int10(xf86Int10InfoPtr pInt);

#ifdef _INT10_PRIVATE

#define I_S_DEFAULT_INT_VECT 0xFF065
#define SYS_SIZE 0x100000
#define SYS_BIOS 0xF0000
#if 1
#define BIOS_SIZE 0x10000
#else /* a bug in DGUX requires this - let's try it */
#define BIOS_SIZE (0x10000 - 1)
#endif
#define LOW_PAGE_SIZE 0x600
#define V_RAM 0xA0000
#define VRAM_SIZE 0x20000
#define V_BIOS_SIZE 0x10000
#define V_BIOS 0xC0000
#define BIOS_SCRATCH_OFF 0x449
#define BIOS_SCRATCH_END 0x466
#define BIOS_SCRATCH_LEN (BIOS_SCRATCH_END - BIOS_SCRATCH_OFF + 1)
#define HIGH_MEM V_BIOS
#define HIGH_MEM_SIZE (SYS_BIOS - HIGH_MEM)
#define SEG_ADR(type, seg, reg)  type((seg << 4) + (X86_##reg))
#define SEG_EADR(type, seg, reg) type((seg << 4) + (X86_E##reg))

#define X86_TF_MASK		0x00000100
#define X86_IF_MASK		0x00000200
#define X86_IOPL_MASK		0x00003000
#define X86_NT_MASK		0x00004000
#define X86_VM_MASK		0x00020000
#define X86_AC_MASK		0x00040000
#define X86_VIF_MASK		0x00080000	/* virtual interrupt flag */
#define X86_VIP_MASK		0x00100000	/* virtual interrupt pending */
#define X86_ID_MASK		0x00200000

#define MEM_RB(name, addr)      (*name->mem->rb)(name, addr)
#define MEM_RW(name, addr)      (*name->mem->rw)(name, addr)
#define MEM_RL(name, addr)      (*name->mem->rl)(name, addr)
#define MEM_WB(name, addr, val) (*name->mem->wb)(name, addr, val)
#define MEM_WW(addr, val) wrw(addr, val)
#define MEM_WL(name, addr, val) (*name->mem->wl)(name, addr, val)

/* OS dependent functions */
Bool MapCurrentInt10(xf86Int10InfoPtr pInt);
/* x86 executor related functions */
Bool xf86Int10ExecSetup(xf86Int10InfoPtr pInt);

/* int.c */
extern xf86Int10InfoPtr Int10Current;
int int_handler(xf86Int10InfoPtr pInt);

/* helper_exec.c */
int setup_int(xf86Int10InfoPtr pInt);
void finish_int(xf86Int10InfoPtr, int sig);
CARD32 getIntVect(xf86Int10InfoPtr pInt, int num);
void pushw(CARD16 val);
int run_bios_int(int num, xf86Int10InfoPtr pInt);
void dump_code(xf86Int10InfoPtr pInt);
void dump_registers(xf86Int10InfoPtr pInt);
void stack_trace(xf86Int10InfoPtr pInt);
xf86Int10InfoPtr getInt10Rec(int entityIndex);
CARD8 bios_checksum(CARD8 *start, int size);
void LockLegacyVGA(xf86Int10InfoPtr pInt, legacyVGAPtr vga);
void UnlockLegacyVGA(xf86Int10InfoPtr pInt, legacyVGAPtr vga);
#if defined (_PC)
void xf86Int10SaveRestoreBIOSVars(xf86Int10InfoPtr pInt, Bool save);
#endif
int port_rep_inb(xf86Int10InfoPtr pInt,
		 CARD16 port, CARD32 base, int d_f, CARD32 count);
int port_rep_inw(xf86Int10InfoPtr pInt,
		 CARD16 port, CARD32 base, int d_f, CARD32 count);
int port_rep_inl(xf86Int10InfoPtr pInt,
		 CARD16 port, CARD32 base, int d_f, CARD32 count);
int port_rep_outb(xf86Int10InfoPtr pInt,
		  CARD16 port, CARD32 base, int d_f, CARD32 count);
int port_rep_outw(xf86Int10InfoPtr pInt,
		  CARD16 port, CARD32 base, int d_f, CARD32 count);
int port_rep_outl(xf86Int10InfoPtr pInt,
		  CARD16 port, CARD32 base, int d_f, CARD32 count);

CARD8 x_inb(CARD16 port);
CARD16 x_inw(CARD16 port);
void x_outb(CARD16 port, CARD8 val);
void x_outw(CARD16 port, CARD16 val);
CARD32 x_inl(CARD16 port);
void x_outl(CARD16 port, CARD32 val);

CARD8 Mem_rb(CARD32 addr);
CARD16 Mem_rw(CARD32 addr);
CARD32 Mem_rl(CARD32 addr);
void Mem_wb(CARD32 addr, CARD8 val);
void Mem_ww(CARD32 addr, CARD16 val);
void Mem_wl(CARD32 addr, CARD32 val);

/* helper_mem.c */
void setup_int_vect(xf86Int10InfoPtr pInt);
int setup_system_bios(void *base_addr);
void reset_int_vect(xf86Int10InfoPtr pInt);
void set_return_trap(xf86Int10InfoPtr pInt);
//void * xf86HandleInt10Options(ScrnInfoPtr pScrn, int entityIndex);
Bool int10skip(void* options);
Bool int10_check_bios(int scrnIndex, int codeSeg, unsigned char* vbiosMem);
Bool initPrimary(void* options);
//void xf86int10ParseBiosLocation(void* options, 
				//xf86int10BiosLocationPtr bios);
#ifdef DEBUG
void dprint(unsigned long start, unsigned long size);
#endif

/* pci.c */
int mapPciRom(char *filename, unsigned char *address);

#endif /* _INT10_PRIVATE */
#endif /* _XF86INT10_H */
