/*
Linux Real Mode Interface - A library of DPMI-like functions for Linux.

Copyright (C) 1998 by Josh Vanderhoof

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL JOSH VANDERHOOF BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef LRMI_H
#define LRMI_H

struct LRMI_regs {
	unsigned int edi;
	unsigned int esi;
	unsigned int ebp;
	unsigned int reserved;
	unsigned int ebx;
	unsigned int edx;
	unsigned int ecx;
	unsigned int eax;
	unsigned short int flags;
	unsigned short int es;
	unsigned short int ds;
	unsigned short int fs;
	unsigned short int gs;
	unsigned short int ip;
	unsigned short int cs;
	unsigned short int sp;
	unsigned short int ss;
};


struct LRMIfuncs {
	/* Initialize: returns 1 if successful, 0 for failure */
	int (*init)(void);
	/* Simulate a 16 bit interrupt: returns 1 if sucessful, 0 for failure */
	int (*interrupt)(int, struct LRMI_regs *);
	/* Allocate real mode memory; The returned block is paragraph (16 byte) aligned */
	void *(*alloc_real)(int);
	/* Free real mode memory */
	void (*free_real)(void *);
	/* Get the base address of the real memory address space block. */
	size_t (*base_addr)(void);
};


struct LRMIfuncs *LRMI_get_implementation(void);

#endif
