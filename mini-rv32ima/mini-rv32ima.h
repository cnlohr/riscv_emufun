// Copyright 2022 Charles Lohr, you may use this file or any portions herein under any of the BSD, MIT, or CC0 licenses.

#ifndef _MINI_RV32IMAH_H
#define _MINI_RV32IMAH_H

/**
    To use mini-rv32ima.h for the bare minimum, the following:

	#define MINI_RV32_RAM_SIZE ram_amt
	#define MINIRV32_IMPLEMENTATION

	#include "mini-rv32ima.h"

	Though, that's not _that_ interesting. You probably want I/O!


	Notes:
		* There is a dedicated CLNT at 0x10000000.
		* There is free MMIO from there to 0x12000000.
		* You can put things like a UART, or whatever there.
		* Feel free to override any of the functionality with macros.
*/

#ifndef MINIRV32WARN
	#define MINIRV32WARN( x... );
#endif

#ifndef MINIRV32_DECORATE
	#define MINIRV32_DECORATE static
#endif

#ifndef MINIRV32_RAM_IMAGE_OFFSET
	#define MINIRV32_RAM_IMAGE_OFFSET  0x80000000
#endif

#ifndef MINIRV32_POSTEXEC
	#define MINIRV32_POSTEXEC(...);
#endif

#ifndef MINIRV32_HANDLE_MEM_STORE_CONTROL
	#define MINIRV32_HANDLE_MEM_STORE_CONTROL(...);
#endif

#ifndef MINIRV32_HANDLE_MEM_LOAD_CONTROL
	#define MINIRV32_HANDLE_MEM_LOAD_CONTROL(...);
#endif

#ifndef MINIRV32_OTHERCSR_WRITE
	#define MINIRV32_OTHERCSR_WRITE(...);
#endif

#ifndef MINIRV32_OTHERCSR_READ
	#define MINIRV32_OTHERCSR_READ(...);
#endif

#ifndef MINIRV32_CUSTOM_MEMORY_BUS
	#define MINIRV32_STORE4( ofs, val ) *(uint32_t*)(image + ofs) = val
	#define MINIRV32_STORE2( ofs, val ) *(uint16_t*)(image + ofs) = val
	#define MINIRV32_STORE1( ofs, val ) *(uint8_t*)(image + ofs) = val
	#define MINIRV32_LOAD4( ofs ) *(uint32_t*)(image + ofs)
	#define MINIRV32_LOAD2( ofs ) *(uint16_t*)(image + ofs)
	#define MINIRV32_LOAD1( ofs ) *(uint8_t*)(image + ofs)
	#define MINIRV32_LOAD2_SIGNED( ofs ) *(int16_t*)(image + ofs)
	#define MINIRV32_LOAD1_SIGNED( ofs ) *(int8_t*)(image + ofs)
#endif

// As a note: We quouple-ify these, because in HLSL, we will be operating with
// uint4's.  We are going to uint4 data to/from system RAM.
//
// We're going to try to keep the full processor state to 12 x uint4.
struct MiniRV32IMAState
{
	uint32_t regs[32];

	uint32_t pc;
	uint32_t mstatus;
	uint32_t cyclel;
	uint32_t cycleh;

	uint32_t timerl;
	uint32_t timerh;
	uint32_t timermatchl;
	uint32_t timermatchh;

	uint32_t mscratch;
	uint32_t mtvec;
	uint32_t mie;
	uint32_t mip;

	uint32_t mepc;
	uint32_t mtval;
	uint32_t mcause;

	// Note: only a few bits are used.  (Machine = 3, User = 0)
	// Bits 0..1 = privilege.
	// Bit 2 = WFI (Wait for interrupt)
	// Bit 3+ = Load/Store reservation LSBs.
	uint32_t extraflags;
};

#ifndef MINIRV32_STEPPROTO
MINIRV32_DECORATE int32_t MiniRV32IMAStep( struct MiniRV32IMAState * state, uint8_t * image, uint32_t vProcAddress, uint32_t elapsedUs, int count );
#endif

#ifdef MINIRV32_IMPLEMENTATION

#ifndef MINIRV32_CUSTOM_INTERNALS
#define CSR( x ) state->x
#define SETCSR( x, val ) { state->x = val; }
#define REG( x ) state->regs[x]
#define REGSET( x, val ) { state->regs[x] = val; }
#endif

#ifndef MINIRV32_STEPPROTO
MINIRV32_DECORATE int32_t MiniRV32IMAStep( struct MiniRV32IMAState * state, uint8_t * image, uint32_t vProcAddress, uint32_t elapsedUs, int count )
#else
MINIRV32_STEPPROTO
#endif
{
	uint32_t new_timer = CSR( timerl ) + elapsedUs;
	if( new_timer < CSR( timerl ) ) CSR( timerh )++;
	CSR( timerl ) = new_timer;

	// Handle Timer interrupt.
	if( ( CSR( timerh ) > CSR( timermatchh ) || ( CSR( timerh ) == CSR( timermatchh ) && CSR( timerl ) > CSR( timermatchl ) ) ) && ( CSR( timermatchh ) || CSR( timermatchl ) ) )
	{
		CSR( extraflags ) &= ~4; // Clear WFI
		CSR( mip ) |= 1<<7; //MTIP of MIP // https://stackoverflow.com/a/61916199/2926815  Fire interrupt.
	}
	else
		CSR( mip ) &= ~(1<<7);

	// If WFI, don't run processor.
	if( CSR( extraflags ) & 4 )
		return 1;

	uint32_t trap = 0;
	uint32_t rval = 0;
	uint32_t pc = CSR( pc );
	uint32_t cycle = CSR( cyclel );

	if( ( CSR( mip ) & (1<<7) ) && ( CSR( mie ) & (1<<7) /*mtie*/ ) && ( CSR( mstatus ) & 0x8 /*mie*/) )
	{
		// Timer interrupt.
		trap = 0x80000007;
		pc -= 4;
		goto trapl;
	}


	uint32_t endcycle = cycle + count;
	uint32_t ir = 0;
	uint32_t rdid = 0;
	uint32_t ofs_pc;


	extern void * C0x37;
	extern void * C0x17;
	extern void * C0x6F;
	extern void * C0x67;
	extern void * C0x63;

	static const void * jumptable[] = {
		&&Cfail, &&Cfail, &&Cfail, &&C0x03, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&C0x0f,
		&&Cfail, &&Cfail, &&Cfail, &&C0x13, &&Cfail, &&Cfail, &&Cfail, &C0x17, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail,
		&&Cfail, &&Cfail, &&Cfail, &&C0x23, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&C0x2f,
		&&Cfail, &&Cfail, &&Cfail, &&C0x33, &&Cfail, &&Cfail, &&Cfail, &C0x37, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail,
		&&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail,
		&&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail,
		&&Cfail, &&Cfail, &&Cfail, &C0x63, &&Cfail, &&Cfail, &&Cfail, &C0x67, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &C0x6F,
		&&Cfail, &&Cfail, &&Cfail, &&C0x73, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail, &&Cfail,
	};

	uint64_t important_pointers[] = {
		(uint64_t)MINI_RV32_RAM_SIZE,
		(uint64_t)jumptable,
		(uint64_t)image,
	};

	goto next_instruction;


dowrite:
	if( rdid )
	{
		REGSET( rdid, rval ); // Write back register.
	}

nowrite:
	//MINIRV32_POSTEXEC( pc, ir, trap );
	pc += 4;
	cycle++;
	if( cycle == endcycle ) goto done;
//		(uint64_t)MINIRV32_RAM_IMAGE_OFFSET,

#define INTERNAL_DO_WRITE \
	"mov %[rdid], %%edx\n" \
	"mov $0, %%eax\n" \
	"test %%edx, %%edx\n" \
	"mov %%edx, %%edx\n" \
	"cmove %%eax, %[rval]\n" \
	"mov %[rval], (%[state], %%rdx, 4)\n"
/*
        xor     eax, eax
        test    edx, edx
        mov     edx, edx
        cmove   esi, eax
        mov     eax, esi
        mov     QWORD PTR jumptable[0+rdx*8], rax
*/

//        test    edx, edx
 //       mov     eax, esi
  //      mov     edx, 0
   //     cmovene   eax, edx

	/*"cmovne %[rval], (%[state], %[rdid], 4)\n"*/

#define INTERNAL_FINISH \
	"add $4, %[pc]\n" \
	"inc %[cycle]\n" \
	"cmp %[cycle], %[endcycle]\n" \
	"je %l[done]\n"

#define INTERNAL_NEXT_INSTRUCTION \
"			lea -2147483648(%[pc]), %%eax\n" \
"			mov %%eax, %[ofs_pc]\n" \
	/* if( ofs_pc >= MINI_RV32_RAM_SIZE ) */ \
"			cmp 0(%[important_pointers]), %%eax\n" \
"			jae %l[trap2]\n" \
	/*	if( ofs_pc & 3 ) */ \
"			and $3, %%eax\n" \
"			jne %l[trap1]\n" \
	/*	ir = MINIRV32_LOAD4( ofs_pc ); */ \
"			mov %[ofs_pc], %%edi\n" \
"			add %[image], %%rdi\n" \
"			mov (%%rdi), %%eax\n" \
"			mov %%eax, %[ir]\n" \
	/*	rdid = (ir >> 7) & 0x1f; */ \
"			shr $7, %%eax\n" \
"			and $31, %%eax\n" \
"			mov %%eax, %[rdid]\n" \
	/*  goto *jumptable[ir & 0x7f]; */ \
"			mov %[ir], %%eax\n" \
"			and $127, %%eax\n" \
"			mov 8(%[important_pointers]), %%rdi\n" /* TODO: OPTIMIZE ME! */ \
"			jmp *(%%rdi,%%rax,8)\n"

next_instruction:

	//ofs_pc = pc - MINIRV32_RAM_IMAGE_OFFSET;
//printf( "%08x %08x %08x\n", jumptable[0x37], jumptable[0x67], &&dowrite );
	asm volatile goto(
	/* eax = eax - MINIRV32_RAM_IMAGE_OFFSET (Assume is 0x80000000) */
//"			inc %[cycle]\n"
"next_instruction_internal:"
	INTERNAL_NEXT_INSTRUCTION
"C0x37:\n"
//	rval = ( ir & 0xfffff000 );
"			mov %[ir], %[rval]\n"
"			and $-4096, %[rval]\n"
/*"			jmp %l[dowrite]\n"*/
INTERNAL_DO_WRITE
INTERNAL_FINISH
INTERNAL_NEXT_INSTRUCTION


//	C0x17: // AUIPC (0b0010111)
"C0x17:\n"
"			mov %[ir], %[rval]\n"
"			and $-4096, %[rval]\n"
"			add %[pc], %[rval]\n"
/*"			jmp %l[dowrite]\n"*/
INTERNAL_DO_WRITE
INTERNAL_FINISH
INTERNAL_NEXT_INSTRUCTION


//		rval = pc + ( ir & 0xfffff000 );
//		goto dowrite;
"C0x6F:\n"
//		int32_t reladdy = ((ir & 0x80000000)>>11) | ((ir & 0x7fe00000)>>20) | ((ir & 0x00100000)>>9) | ((ir&0x000ff000));
//		if( reladdy & 0x00100000 ) reladdy |= 0xffe00000; // Sign extension.
//		rval = pc + 4;
//		pc = pc + reladdy - 4;
//		goto dowrite;
"			mov %[ir], %%eax\n"
"			mov %[ir], %%edi\n"
"			mov %[ir], %%ebx\n"
"			shr    $0xb,%%eax\n"
"			shr    $0x14,%%edi\n"
"			and    $0x7fe,%%edi\n"
"			and    $0x100000,%%eax\n"
"			or     %%edi,%%eax\n"
"			mov    %[ir],%%edi\n"
"			shr    $0x9,%%ebx\n"
"			and    $0xff000,%%edi\n"
"			and    $0x800,%%ebx\n"
"			or     %%edi,%%eax\n"
"			or     %%eax,%%ebx\n"
//		if( reladdy & 0x00100000 ) reladdy |= 0xffe00000; // Sign extension.
"			mov    %%ebx,%%edi\n"
"			or     $0xffe00000,%%edi\n"
"			test   $0x100000,%%eax\n"
"			cmovne %%edi,%%ebx\n"
//		rval = pc + 4;
"			lea 4(%[pc]), %[rval]\n"
//		pc = pc + reladdy - 4;
"			lea -4(%[pc],%%ebx), %[pc]\n" // Was (%rcx, %rdx,1), %r12d
//"			jmp %l[dowrite]\n"

INTERNAL_DO_WRITE
INTERNAL_FINISH
INTERNAL_NEXT_INSTRUCTION

"C0x67:\n"
//		rval = pc + 4;
"			lea    0x4(%[pc]),%[rval]\n"
//		uint32_t imm = ir >> 20;
"			mov    %[ir], %[pc]\n"
"			shr    $0x14,%[pc]\n"
//		int32_t imm_se = imm | (( imm & 0x800 )?0xfffff000:0);
"			mov    %[pc],%%eax\n"
"			or     $0xfffff000,%%eax\n"
"			test   $0x800,%[pc]\n"
"			cmovne %%eax,%[pc]\n"
//		pc = ( (REG( (ir >> 15) & 0x1f ) + imm_se) & ~1) - 4;
"			mov    %[ir], %%eax\n"
"			shr    $0xf, %%eax\n"
"			and    $0x1f, %%eax\n"
"			add    (%[state], %%rax, 4),%[pc]\n"
"			and    $0xfffffffe, %[pc]\n"
"			sub    $4, %[pc]\n"
/*"			jmp    %l[dowrite]\n"*/
INTERNAL_DO_WRITE
INTERNAL_FINISH
INTERNAL_NEXT_INSTRUCTION

"C0x63:\n"
//	uint32_t immm4 = ((ir & 0xf00)>>7) | ((ir & 0x7e000000)>>20) | ((ir & 0x80) << 4) | ((ir >> 31)<<12);
"			mov    %[ir],%%edi\n"
"			mov    %[ir],%%ebx\n"
"			shr    $0x14,%%edi\n"
"			shr    $0x7,%%ebx\n"
"			mov    %%edi,%%eax\n"
"			and    $0x1e,%%ebx\n"
"			and    $0x7e0,%%eax\n"
"			or     %%eax,%%ebx\n"
"			mov    %%edx,%%eax\n"
"			shl    $0x4,%%eax\n"
"			and    $0x800,%%eax\n"
"			or     %%eax,%%ebx\n"
"			mov    %%edx,%%eax\n"
"			shr    $0x1f,%%eax\n"
"			shl    $0xc,%%eax\n"
"			or     %%eax,%%ebx\n"
//		if( immm4 & 0x1000 ) immm4 |= 0xffffe000;
"			mov    %%ebx,%%eax\n"
"			or     $0xffffe000,%%eax\n"
"			test   %%eax,%%eax\n"
"			cmovne %%eax,%%ebx\n"  /* ebx = imm4 */
//		int32_t rs1 = REG((ir >> 15) & 0x1f);
"			mov    %[ir],%%edi\n"
"			shr    $15, %%edi\n"
"			and    $31, %%edi\n"
"			mov    (%[state], %%edi, 4), %%edi\n"
			 /* edi = rs1 */
//		int32_t rs2 = REG((ir >> 20) & 0x1f);
"			mov    %[ir],%%eax\n"
"			shr    $20, %%eax\n"
"			and    $31, %%eax\n"
"			mov    (%[state], %%eax, 4), %%eax\n"
			/* eax = rs2 */
//		immm4 = pc + immm4 - 4;
"			lea    -4(%[pc],%%ebx), %%ebx\n"
//		rdid = 0;  << Is this needed?
"			mov    $0, %[rdid]\n"
//		switch( ( ir >> 12 ) & 0x7 )
"			shr    $12, %[ir]\n"
"			and    $7, %[ir]\n"
//  Jump based on IR

XXX TODO PICK UP HERE

//		{
//			// BEQ, BNE, BLT, BGE, BLTU, BGEU
//			case 0: if( rs1 == rs2 ) pc = immm4; break;
//			case 1: if( rs1 != rs2 ) pc = immm4; break;
//			case 4: if( rs1 < rs2 ) pc = immm4; break;
//			case 5: if( rs1 >= rs2 ) pc = immm4; break; //BGE
//			case 6: if( (uint32_t)rs1 < (uint32_t)rs2 ) pc = immm4; break;   //BLTU
//			case 7: if( (uint32_t)rs1 >= (uint32_t)rs2 ) pc = immm4; break;  //BGEU
//			default: trap = (2+1); goto trapl;
//		}
//		goto nowrite;

/*
    369c:	4c 8d 15 25 1d 00 00 	lea    0x1d25(%rip),%r10        # 53c8 <_IO_stdin_used+0x3c8>
		int32_t rs2 = REG((ir >> 20) & 0x1f);
    36a3:	83 e7 1f             	and    $0x1f,%edi
		switch( ( ir >> 12 ) & 0x7 )
    36a6:	83 e2 07             	and    $0x7,%edx
		int32_t rs1 = REG((ir >> 15) & 0x1f);
    36a9:	41 c1 e9 0f          	shr    $0xf,%r9d
		int32_t rs2 = REG((ir >> 20) & 0x1f);
    36ad:	8b 7c bd 00          	mov    0x0(%rbp,%rdi,4),%edi
		switch( ( ir >> 12 ) & 0x7 )
    36b1:	49 63 14 92          	movslq (%r10,%rdx,4),%rdx
		int32_t rs1 = REG((ir >> 15) & 0x1f);
    36b5:	41 83 e1 1f          	and    $0x1f,%r9d
		immm4 = pc + immm4 - 4;
    36b9:	44 01 f1             	add    %r14d,%ecx
		int32_t rs1 = REG((ir >> 15) & 0x1f);
    36bc:	46 8b 4c 8d 00       	mov    0x0(%rbp,%r9,4),%r9d
		switch( ( ir >> 12 ) & 0x7 )
    36c1:	4c 01 d2             	add    %r10,%rdx
    36c4:	3e ff e2             	notrack jmp *%rdx
    36c7:	66 0f 1f 84 00 00 00 	nopw   0x0(%rax,%rax,1)
    36ce:	00 00 
*/

: [ofs_pc]"=r"(ofs_pc), [ir]"=r"(ir), [pc]"+r"(pc), [trap]"+r"(trap), [rdid]"=r"(rdid), [rval]"=r"(rval), [cycle]"+r"(cycle)
: [important_pointers]"r"(important_pointers), [image]"r"(image), [state]"r"(state), [endcycle]"r"(endcycle)
: "rax", "rbx", "rdi", "memory" 
: trap2, trap1, C0x03, C0x23, C0x13, C0x33, C0x0f, C0x73, dowrite, done );

//	printf( "*** %08x  %08x [%08x] %08x [%08x]\n", ir, image, ofs_pc, important_pointers[0], ir );

//	if( ofs_pc >= MINI_RV32_RAM_SIZE )
//	{
//		trap = 1 + 1;  // Handle access violation on instruction read.
//		goto trap;
//	}
//	if( ofs_pc & 3 )
//	{
//		trap = 1 + 0;  //Handle PC-misaligned access
//		goto trapl;
//	}

//	ir = MINIRV32_LOAD4( ofs_pc );

//	rdid = (ir >> 7) & 0x1f;


	goto *jumptable[ir & 0x7f];

//	C0x37: // LUI (0b0110111)
//		//rval = ( ir & 0xfffff000 );
//		goto dowrite;
//	C0x17: // AUIPC (0b0010111)
//		rval = pc + ( ir & 0xfffff000 );
//		goto dowrite;
//	C0x6F: // JAL (0b1101111)
//	{
//		int32_t reladdy = ((ir & 0x80000000)>>11) | ((ir & 0x7fe00000)>>20) | ((ir & 0x00100000)>>9) | ((ir&0x000ff000));
//		if( reladdy & 0x00100000 ) reladdy |= 0xffe00000; // Sign extension.
//		rval = pc + 4;
//		pc = pc + reladdy - 4;
//		goto dowrite;
//	}
//	C0x67: // JALR (0b1100111)
//	{
//		uint32_t imm = ir >> 20;
//		int32_t imm_se = imm | (( imm & 0x800 )?0xfffff000:0);
//		rval = pc + 4;
//		pc = ( (REG( (ir >> 15) & 0x1f ) + imm_se) & ~1) - 4;
//		goto dowrite;
//	}
/* 
	C0x63: // Branch (0b1100011)
	{
		uint32_t immm4 = ((ir & 0xf00)>>7) | ((ir & 0x7e000000)>>20) | ((ir & 0x80) << 4) | ((ir >> 31)<<12);
		if( immm4 & 0x1000 ) immm4 |= 0xffffe000;
		int32_t rs1 = REG((ir >> 15) & 0x1f);
		int32_t rs2 = REG((ir >> 20) & 0x1f);
		immm4 = pc + immm4 - 4;
		rdid = 0;
		switch( ( ir >> 12 ) & 0x7 )
		{
			// BEQ, BNE, BLT, BGE, BLTU, BGEU
			case 0: if( rs1 == rs2 ) pc = immm4; break;
			case 1: if( rs1 != rs2 ) pc = immm4; break;
			case 4: if( rs1 < rs2 ) pc = immm4; break;
			case 5: if( rs1 >= rs2 ) pc = immm4; break; //BGE
			case 6: if( (uint32_t)rs1 < (uint32_t)rs2 ) pc = immm4; break;   //BLTU
			case 7: if( (uint32_t)rs1 >= (uint32_t)rs2 ) pc = immm4; break;  //BGEU
			default: trap = (2+1); goto trapl;
		}
		goto nowrite;
	}
*/

	C0x03: // Load (0b0000011)
	{
		uint32_t rs1 = REG((ir >> 15) & 0x1f);
		uint32_t imm = ir >> 20;
		int32_t imm_se = imm | (( imm & 0x800 )?0xfffff000:0);
		uint32_t rsval = rs1 + imm_se;

		rsval -= MINIRV32_RAM_IMAGE_OFFSET;
		if( rsval >= MINI_RV32_RAM_SIZE-3 )
		{
			rsval += MINIRV32_RAM_IMAGE_OFFSET;
			if( rsval >= 0x10000000 && rsval < 0x12000000 )  // UART, CLNT
			{
				if( rsval == 0x1100bffc ) // https://chromitem-soc.readthedocs.io/en/latest/clint.html
					rval = CSR( timerh );
				else if( rsval == 0x1100bff8 )
					rval = CSR( timerl );
				else
					MINIRV32_HANDLE_MEM_LOAD_CONTROL( rsval, rval );
			}
			else
			{
				trap = (5+1);
				rval = rsval;
				goto trapl;
			}
		}
		else
		{
			switch( ( ir >> 12 ) & 0x7 )
			{
				//LB, LH, LW, LBU, LHU
				case 0: rval = MINIRV32_LOAD1_SIGNED( rsval ); break;
				case 1: rval = MINIRV32_LOAD2_SIGNED( rsval ); break;
				case 2: rval = MINIRV32_LOAD4( rsval ); break;
				case 4: rval = MINIRV32_LOAD1( rsval ); break;
				case 5: rval = MINIRV32_LOAD2( rsval ); break;
				default: trap = (2+1); goto trapl;
			}
		}
		goto dowrite;
	}
	C0x23: // Store 0b0100011
	{
		uint32_t rs1 = REG((ir >> 15) & 0x1f);
		uint32_t rs2 = REG((ir >> 20) & 0x1f);
		uint32_t addy = ( ( ir >> 7 ) & 0x1f ) | ( ( ir & 0xfe000000 ) >> 20 );
		if( addy & 0x800 ) addy |= 0xfffff000;
		addy += rs1 - MINIRV32_RAM_IMAGE_OFFSET;
		rdid = 0;

		if( addy >= MINI_RV32_RAM_SIZE-3 )
		{
			addy += MINIRV32_RAM_IMAGE_OFFSET;
			if( addy >= 0x10000000 && addy < 0x12000000 )
			{
				// Should be stuff like SYSCON, 8250, CLNT
				if( addy == 0x11004004 ) //CLNT
					CSR( timermatchh ) = rs2;
				else if( addy == 0x11004000 ) //CLNT
					CSR( timermatchl ) = rs2;
				else if( addy == 0x11100000 ) //SYSCON (reboot, poweroff, etc.)
				{
					SETCSR( pc, pc + 4 );
					return rs2; // NOTE: PC will be PC of Syscon.
				}
				else
					MINIRV32_HANDLE_MEM_STORE_CONTROL( addy, rs2 );
			}
			else
			{
				trap = (7+1); // Store access fault.
				rval = addy;
				goto trapl;
			}
		}
		else
		{
			switch( ( ir >> 12 ) & 0x7 )
			{
				//SB, SH, SW
				case 0: MINIRV32_STORE1( addy, rs2 ); break;
				case 1: MINIRV32_STORE2( addy, rs2 ); break;
				case 2: MINIRV32_STORE4( addy, rs2 ); break;
				default: trap = (2+1); goto trapl;
			}
		}
		goto nowrite;
	}
	C0x13: // Op-immediate 0b0010011
	C0x33: // Op           0b0110011
	{
		uint32_t imm = ir >> 20;
		imm = imm | (( imm & 0x800 )?0xfffff000:0);
		uint32_t rs1 = REG((ir >> 15) & 0x1f);
		uint32_t is_reg = !!( ir & 0x20 );
		uint32_t rs2 = is_reg ? REG(imm & 0x1f) : imm;

		if( is_reg && ( ir & 0x02000000 ) )
		{
			switch( (ir>>12)&7 ) //0x02000000 = RV32M
			{
				case 0: rval = rs1 * rs2; break; // MUL
#ifndef CUSTOM_MULH // If compiling on a system that doesn't natively, or via libgcc support 64-bit math.
				case 1: rval = ((int64_t)((int32_t)rs1) * (int64_t)((int32_t)rs2)) >> 32; break; // MULH
				case 2: rval = ((int64_t)((int32_t)rs1) * (uint64_t)rs2) >> 32; break; // MULHSU
				case 3: rval = ((uint64_t)rs1 * (uint64_t)rs2) >> 32; break; // MULHU
#else
				CUSTOM_MULH
#endif
				case 4: if( rs2 == 0 ) rval = -1; else rval = ((int32_t)rs1 == INT32_MIN && (int32_t)rs2 == -1) ? rs1 : ((int32_t)rs1 / (int32_t)rs2); break; // DIV
				case 5: if( rs2 == 0 ) rval = 0xffffffff; else rval = rs1 / rs2; break; // DIVU
				case 6: if( rs2 == 0 ) rval = rs1; else rval = ((int32_t)rs1 == INT32_MIN && (int32_t)rs2 == -1) ? 0 : ((uint32_t)((int32_t)rs1 % (int32_t)rs2)); break; // REM
				case 7: if( rs2 == 0 ) rval = rs1; else rval = rs1 % rs2; break; // REMU
			}
		}
		else
		{
			switch( (ir>>12)&7 ) // These could be either op-immediate or op commands.  Be careful.
			{
				case 0: rval = (is_reg && (ir & 0x40000000) ) ? ( rs1 - rs2 ) : ( rs1 + rs2 ); break; 
				case 1: rval = rs1 << (rs2 & 0x1F); break;
				case 2: rval = (int32_t)rs1 < (int32_t)rs2; break;
				case 3: rval = rs1 < rs2; break;
				case 4: rval = rs1 ^ rs2; break;
				case 5: rval = (ir & 0x40000000 ) ? ( ((int32_t)rs1) >> (rs2 & 0x1F) ) : ( rs1 >> (rs2 & 0x1F) ); break;
				case 6: rval = rs1 | rs2; break;
				case 7: rval = rs1 & rs2; break;
			}
		}
		goto dowrite;
	}
	C0x0f: // 0b0001111
		rdid = 0;   // fencetype = (ir >> 12) & 0b111; We ignore fences in this impl.
		goto nowrite;
	C0x73: // Zifencei+Zicsr  (0b1110011)
	{
		uint32_t csrno = ir >> 20;
		uint32_t microop = ( ir >> 12 ) & 0x7;
		if( (microop & 3) ) // It's a Zicsr function.
		{
			int rs1imm = (ir >> 15) & 0x1f;
			uint32_t rs1 = REG(rs1imm);
			uint32_t writeval = rs1;

			// https://raw.githubusercontent.com/riscv/virtual-memory/main/specs/663-Svpbmt.pdf
			// Generally, support for Zicsr
			switch( csrno )
			{
			case 0x340: rval = CSR( mscratch ); break;
			case 0x305: rval = CSR( mtvec ); break;
			case 0x304: rval = CSR( mie ); break;
			case 0xC00: rval = cycle; break;
			case 0x344: rval = CSR( mip ); break;
			case 0x341: rval = CSR( mepc ); break;
			case 0x300: rval = CSR( mstatus ); break; //mstatus
			case 0x342: rval = CSR( mcause ); break;
			case 0x343: rval = CSR( mtval ); break;
			case 0xf11: rval = 0xff0ff0ff; break; //mvendorid
			case 0x301: rval = 0x40401101; break; //misa (XLEN=32, IMA+X)
			//case 0x3B0: rval = 0; break; //pmpaddr0
			//case 0x3a0: rval = 0; break; //pmpcfg0
			//case 0xf12: rval = 0x00000000; break; //marchid
			//case 0xf13: rval = 0x00000000; break; //mimpid
			//case 0xf14: rval = 0x00000000; break; //mhartid
			default:
				MINIRV32_OTHERCSR_READ( csrno, rval );
				break;
			}

			switch( microop )
			{
				case 1: writeval = rs1; break;  			//CSRRW
				case 2: writeval = rval | rs1; break;		//CSRRS
				case 3: writeval = rval & ~rs1; break;		//CSRRC
				case 5: writeval = rs1imm; break;			//CSRRWI
				case 6: writeval = rval | rs1imm; break;	//CSRRSI
				case 7: writeval = rval & ~rs1imm; break;	//CSRRCI
			}

			switch( csrno )
			{
			case 0x340: SETCSR( mscratch, writeval ); break;
			case 0x305: SETCSR( mtvec, writeval ); break;
			case 0x304: SETCSR( mie, writeval ); break;
			case 0x344: SETCSR( mip, writeval ); break;
			case 0x341: SETCSR( mepc, writeval ); break;
			case 0x300: SETCSR( mstatus, writeval ); break; //mstatus
			case 0x342: SETCSR( mcause, writeval ); break;
			case 0x343: SETCSR( mtval, writeval ); break;
			//case 0x3a0: break; //pmpcfg0
			//case 0x3B0: break; //pmpaddr0
			//case 0xf11: break; //mvendorid
			//case 0xf12: break; //marchid
			//case 0xf13: break; //mimpid
			//case 0xf14: break; //mhartid
			//case 0x301: break; //misa
			default:
				MINIRV32_OTHERCSR_WRITE( csrno, writeval );
				break;
			}
		}
		else if( microop == 0x0 ) // "SYSTEM" 0b000
		{
			rdid = 0;
			if( csrno == 0x105 ) //WFI (Wait for interrupts)
			{
				CSR( mstatus ) |= 8;    //Enable interrupts
				CSR( extraflags ) |= 4; //Infor environment we want to go to sleep.
				SETCSR( pc, pc + 4 );
				return 1;
			}
			else if( ( ( csrno & 0xff ) == 0x02 ) )  // MRET
			{
				//https://raw.githubusercontent.com/riscv/virtual-memory/main/specs/663-Svpbmt.pdf
				//Table 7.6. MRET then in mstatus/mstatush sets MPV=0, MPP=0, MIE=MPIE, and MPIE=1. La
				// Should also update mstatus to reflect correct mode.
				uint32_t startmstatus = CSR( mstatus );
				uint32_t startextraflags = CSR( extraflags );
				SETCSR( mstatus , (( startmstatus & 0x80) >> 4) | ((startextraflags&3) << 11) | 0x80 );
				SETCSR( extraflags, (startextraflags & ~3) | ((startmstatus >> 11) & 3) );
				pc = CSR( mepc ) -4;
			}
			else
			{
				switch( csrno )
				{
				case 0: trap = ( CSR( extraflags ) & 3) ? (11+1) : (8+1); goto trapl; break; // ECALL; 8 = "Environment call from U-mode"; 11 = "Environment call from M-mode"
				case 1:	trap = (3+1); goto trapl; break; // EBREAK 3 = "Breakpoint"
				default: trap = (2+1); goto trapl; break; // Illegal opcode.
				}
			}
		}
		else
		{
			trap = (2+1); 				// Note micrrop 0b100 == undefined.
			goto trapl;
		}
		goto dowrite;
	}
	C0x2f: // RV32A (0b00101111)
	{
		uint32_t rs1 = REG((ir >> 15) & 0x1f);
		uint32_t rs2 = REG((ir >> 20) & 0x1f);
		uint32_t irmid = ( ir>>27 ) & 0x1f;

		rs1 -= MINIRV32_RAM_IMAGE_OFFSET;

		// We don't implement load/store from UART or CLNT with RV32A here.

		if( rs1 >= MINI_RV32_RAM_SIZE-3 )
		{
			trap = (7+1); //Store/AMO access fault
			rval = rs1 + MINIRV32_RAM_IMAGE_OFFSET;
			goto trapl;
		}
		else
		{
			rval = MINIRV32_LOAD4( rs1 );

			// Referenced a little bit of https://github.com/franzflasch/riscv_em/blob/master/src/core/core.c
			uint32_t dowrite = 1;
			switch( irmid )
			{
				case 2: //LR.W (0b00010)
					dowrite = 0;
					CSR( extraflags ) = (CSR( extraflags ) & 0x07) | (rs1<<3);
					break;
				case 3:  //SC.W (0b00011) (Make sure we have a slot, and, it's valid)
					rval = ( CSR( extraflags ) >> 3 != ( rs1 & 0x1fffffff ) );  // Validate that our reservation slot is OK.
					dowrite = !rval; // Only write if slot is valid.
					break;
				case 1: break; //AMOSWAP.W (0b00001)
				case 0: rs2 += rval; break; //AMOADD.W (0b00000)
				case 4: rs2 ^= rval; break; //AMOXOR.W (0b00100)
				case 12: rs2 &= rval; break; //AMOAND.W (0b01100)
				case 8: rs2 |= rval; break; //AMOOR.W (0b01000)
				case 16: rs2 = ((int32_t)rs2<(int32_t)rval)?rs2:rval; break; //AMOMIN.W (0b10000)
				case 20: rs2 = ((int32_t)rs2>(int32_t)rval)?rs2:rval; break; //AMOMAX.W (0b10100)
				case 24: rs2 = (rs2<rval)?rs2:rval; break; //AMOMINU.W (0b11000)
				case 28: rs2 = (rs2>rval)?rs2:rval; break; //AMOMAXU.W (0b11100)
				default: trap = (2+1); dowrite = 0; goto trapl; break; //Not supported.
			}
			if( dowrite ) MINIRV32_STORE4( rs1, rs2 );
		}
		goto dowrite;
	}

	Cfail: trap = (2+1); goto trapl; // Fault: Invalid opcode.

trap1:	trap = 1; goto trapl;
trap2: printf( "TRAP2: ofs: %08x/%08x\n", ofs_pc, important_pointers[0] ); trap = 2; goto trap2;

trapl:

	// Handle traps and interrupts.

	if( trap & 0x80000000 ) // If prefixed with 1 in MSB, it's an interrupt, not a trap.
	{
		SETCSR( mcause, trap );
		SETCSR( mtval, 0 );
		pc += 4; // PC needs to point to where the PC will return to.
	}
	else
	{
		SETCSR( mcause,  trap - 1 );
		SETCSR( mtval, (trap > 5 && trap <= 8)? rval : pc );
	}
	SETCSR( mepc, pc ); //TRICKY: The kernel advances mepc automatically.
	//CSR( mstatus ) & 8 = MIE, & 0x80 = MPIE
	// On an interrupt, the system moves current MIE into MPIE
	SETCSR( mstatus, (( CSR( mstatus ) & 0x08) << 4) | (( CSR( extraflags ) & 3 ) << 11) );
	pc = (CSR( mtvec ) - 4);

	// If trapping, always enter machine mode.
	CSR( extraflags ) |= 3;

	trap = 0;
	pc += 4;


done:
	if( CSR( cyclel ) > cycle ) CSR( cycleh )++;
	SETCSR( cyclel, cycle );
	SETCSR( pc, pc );
	return 0;
}

#endif

#endif


