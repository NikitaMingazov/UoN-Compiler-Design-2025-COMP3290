
#ifndef AST_NODE
#define AST_NODE

#include "lib/defs.h"
#include "astree.h"

enum node_type {
	NPROG, NGLOB, NILIST, NINIT, NFUNCS, NMAIN, NSDLST, NTYPEL, NRTYPE, NATYPE,
	NFLIST, NSDECL, NALIST, NARRD, NFUND, NPLIST, NSIMP, NARRP, NARRC, NDLIST,
	NSTATS, NFORL, NREPT, NASGNS, NIFTH, NIFTE, NASGN, NPLEQ, NMNEQ, NSTEA /* typo lmao */,
	NDVEQ, NINPUT, NOUTP, NOUTL, NCALL, NRETN, NVLIST, NSIMV, NARRV, NAELT,
	NEXPL, NBOOL, NNOT, NAND, NOR, NXOR, NEQL, NNEQ, NGRT, NLSS,
	NLEQ, NGEQ, NADD, NSUB, NMUL, NDIV, NMOD, NPOW, NILIT, NFLIT,
	NTRUE, NFALS, NFCALL, NPRLST, NSTRG
};

static char NPRINT[65][8] = {
	"NPROG  ", "NGLOB  ", "NILIST ", "NINIT  ", "NFUNCS ", "NMAIN  ", "NSDLST ", "NTYPEL ", "NRTYPE ", "NATYPE ",
	"NFLIST ", "NSDECL ", "NALIST ", "NARRD  ", "NFUND  ", "NPLIST ", "NSIMP  ", "NARRP  ", "NARRC  ", "NDLIST ",
	"NSTATS ", "NFORL  ", "NREPT  ", "NASGNS ", "NIFTH  ", "NIFTE  ", "NASGN  ", "NPLEQ  ", "NMNEQ  ", "NSTEA  " /* typo lmao */,
	"NDVEQ  ", "NINPUT ", "NOUTP  ", "NOUTL  ", "NCALL  ", "NRETN  ", "NVLIST ", "NSIMV  ", "NARRV  ", "NAELT  ",
	"NEXPL  ", "NBOOL  ", "NNOT   ", "NAND   ", "NOR    ", "NXOR   ", "NEQL   ", "NNEQ   ", "NGRT   ", "NLSS   ",
	"NLEQ   ", "NGEQ   ", "NADD   ", "NSUB   ", "NMUL   ", "NDIV   ", "NMOD   ", "NPOW   ", "NILIT  ", "NFLIT  ",
	"NTRUE  ", "NFALS  ", "NFCALL ", "NPRLST ", "NSTRG  "
};

enum symbol_type {
	SNONE, SREAL, SINT, SBOOL, SVOID, SARRAY, SSTRUCT, SSTRING, SFIELDS, SERROR
};

static char SPRINT[10][7] = {
	"none", "real", "int", "bool", "void", "array", "struct", "string", "fields", "error"
};

typedef struct astnode {
	enum node_type type;
	u16 row;
	u16 col;
	enum symbol_type symbol_type;
	Symbol *symbol_value;
	struct astnode *left_child;
	struct astnode *middle_child;
	struct astnode *right_child;
} ASTNode;

#endif

