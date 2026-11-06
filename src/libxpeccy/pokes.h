#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

// Trainers from a .pok file (SGD's POK format, v1.20): N<name> starts a trainer,
// M bank adr val org is a poke with more to follow, Z its last one, Y the end.
// Bank bit 3 = the address as the cpu sees it, else bits 0-2 are a 128K page;
// val 256 = the user gives the value; org = the byte before the poke, 0 = not known.

#define POK_NAME_MAX	80
#define POK_FILE_MAX	(1 << 20)

// pok_parse
enum {
	POK_OK = 0,
	POK_ERR_EMPTY,		// no usable trainer: not a .pok
	POK_ERR_SIZE,		// over POK_FILE_MAX
	POK_ERR_MEM
};

// why a trainer is not usable (xTrainer.why)
enum {
	POK_BAD_NONE = 0,
	POK_BAD_LINE,		// a poke line that does not read as four numbers
	POK_BAD_RANGE,		// a number out of range
	POK_BAD_KEY,		// a line other than M/Z inside a trainer
	POK_BAD_OPEN,		// ended without a Z
	POK_BAD_EMPTY		// no pokes
};

// pok_check / pok_apply / pok_poke
enum {
	POK_FITS = 0,		// every known org is in memory (or the value already is)
	POK_UNCHECKED,		// no org known: nothing to compare
	POK_MISMATCH,		// a known org is not in memory: another release
	POK_NO_PAGE,		// a 128K page this machine does not have
	POK_NO_RAM		// a cpu address with no ram behind it now
};

// pok_where
enum {
	POK_AT_ON = 0,		// memory holds the trainer's values
	POK_AT_OFF,		// memory holds what pok_apply saved
	POK_AT_OTHER
};

typedef struct {
	int bank;
	int adr;
	int val;
	int org;
	int saved;		// the byte pok_apply put aside, -1 before that
} xPoke;

typedef struct {
	char name[POK_NAME_MAX + 1];
	int why;		// POK_BAD_*
	int line;		// the line it went bad at (1-based)
	int ask;		// a val of 256 in it
	int cnt;
	int cap;
	xPoke* pokes;
} xTrainer;

typedef struct {
	int cnt;
	int cap;
	xTrainer* list;
} xPokFile;

struct Computer;

int pok_parse(const char* buf, size_t len, xPokFile* pf);
void pok_free(xPokFile* pf);
void pok_trainer_free(xTrainer* tr);
// building one by hand, as the reader does: 0 = out of memory. The name is cut to
// POK_NAME_MAX, trailing blanks off; the new trainer is pf->list[pf->cnt - 1].
int pok_add_trainer(xPokFile* pf, const char* name, int len);
int pok_add_poke(xTrainer* tr, int bank, int adr, int val, int org);

// the byte a poke goes to, -1 if it has no place on this machine now
int pok_peek(struct Computer* comp, int bank, int adr);
int pok_check(struct Computer* comp, const xTrainer* tr);
// val stands for 256 in the file. Nothing is written unless every poke has a place.
int pok_apply(struct Computer* comp, xTrainer* tr, int val);
// the saved bytes back where the poke's value still is; val as for pok_apply
void pok_undo(struct Computer* comp, xTrainer* tr, int val);
int pok_where(struct Computer* comp, const xTrainer* tr, int val);
// one poke more for a trainer made by hand, written at once
int pok_poke(struct Computer* comp, xTrainer* tr, int bank, int adr, int val);

#ifdef __cplusplus
}
#endif
