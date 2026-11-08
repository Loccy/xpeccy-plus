#pragma once

#include <string>
#include <vector>

#include "../libxpeccy/spectrum.h"

// C-like expression compiler/evaluator
// used by breakpoint conditions and by the watcher

// opcodes of the compiled (RPN) form
enum {
	XE_NUM = 0,	// push val
	XE_REG,		// push register named 'name'
	XE_LAB,		// push address of label 'name'
	XE_VAR,		// push pseudo-variable, val = XV_*
	// unary
	XE_NOT, XE_INV, XE_NEG,
	XE_MRDB,	// byte from memory
	XE_MRDW,	// word from memory (L-H)
	// binary
	XE_MUL, XE_DIV, XE_MOD,
	XE_ADD, XE_SUB,
	XE_SHL, XE_SHR,
	XE_LT, XE_GT, XE_LE, XE_GE,
	XE_EQ, XE_NE,
	XE_AND, XE_XOR, XE_OR,
	XE_LAND, XE_LOR,
	XE_RAYHIT	// RAY(x,y): the beam crossed that dot inside this instruction
};

// pseudo-variables: last memory/io event and machine state
enum {
	XV_RD = 0,	// address of last memory read
	XV_WR,		// address of last memory write
	XV_MDT,		// data of last memory read/write
	XV_IN,		// port of last 'in'
	XV_OUT,		// port of last 'out'
	XV_VAL,		// data of last 'in'/'out'
	XV_DOS,		// dos rom is on
	XV_SLOT0,	// page number in memory window 0..3
	XV_SLOT1,
	XV_SLOT2,
	XV_SLOT3,
	XV_FRAME,	// frames since reset
	XV_RAYX,	// beam position, the two numbers of the RAY panel
	XV_RAYY,
	XV_HITS,	// how many times this breakpoint was hit
	XV_DT		// T since the stretch it closes opened, or since its own hit before
};

typedef struct {
	int op;
	unsigned val;
	int base;		// XE_NUM: how the number was written, to read it back
	std::string name;
	// XE_LAB: the label's cell, looked up once while the labels stay the same
	mutable xAdr lab;
	mutable unsigned labGen = 0;
	mutable bool paged = false;	// a bare name of several systems: looked up again when the paging changes
	mutable unsigned long long pageKey = 0;
	mutable xAdr cpu;		// a ROM label's CPU address, while the paging stays as at cpuKey
	mutable unsigned long long cpuKey = 0;
} xExprItem;

typedef struct {
	std::vector<xExprItem> code;
	std::string src;	// source text as typed
	std::string err;	// error message, empty if compiled
	int errpos;		// position of error in src
} xExpr;

xExpr xexpr_compile(const char*);
// The last argument selects CPU addresses (0) or physical MEM_RAM/MEM_ROM for labels and reads;
// XE_CPU_HOME: CPU addresses, a ROM label paged out where its ROM runs (the assembler's)
#define XE_CPU_HOME	(-2)
// out given: a label that is there but not paged in now is XE_NOWHERE, which no
// address equals, and out is set; else that is an error
#define XE_NOWHERE	0x10000
unsigned xexpr_eval(const xExpr&, Computer*, bool* = nullptr, int = 0, unsigned = 0, int = 0, bool* out = nullptr);
bool xexpr_ok(const xExpr&);
// the expression as it was understood: hex numbers, priorities as brackets
std::string xexpr_text(const xExpr&);
// does the expression use this pseudo-variable (XV_*)
bool xexpr_uses_var(const xExpr&, int);
