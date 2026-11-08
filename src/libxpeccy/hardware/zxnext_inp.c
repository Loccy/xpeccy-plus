// The ZX Spectrum Next's keyboard, joysticks and mouse as the core reads them
// (zxnext.vhd 3425-3562): the 8x7 matrix the membrane scan makes of the membrane,
// the ps/2 keyboard and the key joysticks (input/membrane/membrane.vhd,
// membrane_stick.vhd, input/keyboard/ps2_keyb.vhd), the extended keys turned into
// 8x5 entries, the Kempston/MD ports and the Kempston mouse. The host gives the state:
// the membrane is the Keyboard's matrix, the ps/2 keyboard its nx* fields, the
// joysticks comp->joy (left) and comp->joyb (right), the mouse comp->mouse.
//
// The scan is modelled for the extended keys alone: their CAPS/SYM SHIFT comes one
// scan (164.6 us) before the key and goes two after it (nx_inp_sync). The rest is
// read as it stands; on the board a key shows up to one scan later, and host keys
// come at no finer a time.

#include <math.h>
#include <string.h>

#include "zxnext.h"

#define IN (NX.inp)

// keymaps.vhd: the US layout the core is configured with (TBBLUE.FW loads
// keymap.bin over it, the same but for Page Up)
static const unsigned char nxKeymap[512] = {
	0x07, 0x07, 0x07, 0x07, 0xc2, 0x07, 0x07, 0xc5, 0x07, 0x07, 0xc7, 0x07, 0xc3, 0x15, 0x1e, 0x07,
	0x07, 0x05, 0x47, 0x07, 0x87, 0x10, 0x18, 0x07, 0x07, 0x07, 0x01, 0x09, 0x08, 0x11, 0x19, 0x07,
	0x07, 0x03, 0x02, 0x0a, 0x12, 0x1b, 0x1a, 0x07, 0x07, 0x38, 0x04, 0x0b, 0x14, 0x13, 0x1c, 0x07,
	0x07, 0x3b, 0x3c, 0x34, 0x0c, 0x2c, 0x24, 0x07, 0x07, 0x07, 0x3a, 0x33, 0x2b, 0x23, 0x22, 0x07,
	0x07, 0x2d, 0x32, 0x2a, 0x29, 0x20, 0x21, 0x07, 0x07, 0x2e, 0x84, 0x31, 0x25, 0x28, 0xb3, 0x07,
	0x07, 0x07, 0x26, 0x07, 0x07, 0xb1, 0x07, 0x07, 0x0d, 0x47, 0x30, 0x07, 0x07, 0x16, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x35, 0x07, 0x07, 0x18, 0x07, 0x1b, 0x23, 0x07, 0x07, 0x07,
	0x20, 0x2e, 0x19, 0x1c, 0x24, 0x22, 0x1d, 0x07, 0xc4, 0xb2, 0x1a, 0xb3, 0xbc, 0x21, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x0e, 0x07, 0x07, 0x87, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x84, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x30, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x3d, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x3e, 0x07, 0x36, 0x06, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07
};

// ram/init/keyjoy_64_6.coe: Sinclair 1, Sinclair 2 and Cursor; the user's entries empty
static const unsigned char nxJoymap[64] = {
	0x23, 0x24, 0x22, 0x21, 0x20, 0x19, 0x18, 0x1a, 0x1b, 0x1c, 0x22, 0x1c, 0x24, 0x23, 0x20, 0x3f,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x23, 0x24, 0x22, 0x21, 0x20, 0x19, 0x18, 0x1a, 0x1b, 0x1c, 0x22, 0x1c, 0x24, 0x23, 0x20, 0x3f,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07
};

void nx_inp_power(Computer* comp) {
	memcpy(IN.keymap, nxKeymap, sizeof(IN.keymap));
	memcpy(IN.joymap, nxJoymap, sizeof(IN.joymap));
	IN.sel = 0;
	IN.adr = 0;
	IN.mx = IN.my = 0;
	IN.hx = comp->mouse->xpos;
	IN.hy = comp->mouse->ypos;
}

// NR 28, 29, 2B: the address, then a byte a write to the keymap or the joystick map
// (zxnext.vhd 6298-6324, membrane_stick.vhd 181)
void nx_inp_nr(Computer* comp, int r, int v) {
	switch (r) {
		case 0x28:
			IN.sel = (v >> 7) & 1;
			IN.adr = (IN.adr & 0xff) | ((v & 1) << 8);
			break;
		case 0x29:
			IN.adr = (IN.adr & 0x100) | (v & 0xff);
			break;
		case 0x2b:
			if (IN.sel)
				IN.joymap[((IN.adr & 0x10) << 1) | 0x10 | (IN.adr & 15)] = v & 0x3f;
			else
				IN.keymap[IN.adr] = v & 0xff;
			IN.adr = (IN.adr + 1) & 0x1ff;
			break;
	}
}

// == the ps/2 keyboard ==

// One code taken (ps2_keyb.vhd): the typematic repeat of a make is dropped, CAPS
// and SYM SHIFT are counted, the rest set or clear their place in the matrix.
// A function key (b7:6 = 11) is the board's, F1..F8 (resets, 50/60 Hz, speed); F9 and
// F10 are the NMI and DRIVE buttons whatever the keymap says (nx_board_key).
// A keyboard makes a key once and breaks it once; the host's input can make one twice
// (a modifier is read from two places), so a make of a key down or a break of one up
// is dropped here - counted, a CAPS or SYM SHIFT would stay down for good.
static void nx_ps2_code(Computer* comp, int code, int ext, int rel) {
	Keyboard* kbd = comp->keyb;
	int cur = (rel << 9) | (ext << 8) | code;
	if (!rel && (cur == kbd->nxLast)) return;
	kbd->nxLast = cur;
	int k = (ext << 8) | code;
	unsigned char* down = &kbd->nxDown[(k >> 3) & 63];
	int bit = 1 << (k & 7);
	if (rel ? !(*down & bit) : (*down & bit)) return;
	if (rel) *down &= ~bit;
	else *down |= bit;
	// F9 and F10 are the NMI and DRIVE buttons' lines (ps2_keyb.vhd 119-141; the Win keys
	// too, which no host key here sends). DRIVE goes at the press; NMI at the release of
	// a press under a second, and while it is down a digit is a function key instead,
	// 1..0 = F1..F10, with no NMI after (emu_fnkeys.vhd)
	if (k == 0x01) {
		if (!rel) {
			kbd->nxNmiDown = 1;
			kbd->nxNmiAt = nx_now(comp);
			kbd->nxNmiCut = 0;
		} else if (kbd->nxNmiDown) {
			kbd->nxNmiDown = 0;
			long long t = nx_now(comp) - kbd->nxNmiAt;
			if (!kbd->nxNmiCut && (t >= 0) && (t < NX_BUTTON_SHORT)) nx_board_key(comp, 8);
		}
	}
	if (!rel && (k == 0x09)) nx_board_key(comp, 9);
	int d = IN.keymap[k];
	int drow = (d >> 3) & 7;
	// (a key of the digits' rows, as the membrane scan sees it: CAPS + 5 is one too)
	if (!rel && kbd->nxNmiDown && (k != 0x01) && ((d & 0xc0) != 0xc0) && ((d & 7) < 5) && ((drow == 3) || (drow == 4))) {
		nx_board_key(comp, (drow == 3) ? (d & 7) : 9 - (d & 7));
		kbd->nxNmiCut = 1;
		*down &= ~bit;		// taken: its break is dropped as well
		return;
	}
	if ((d & 0xc0) == 0xc0) {
		if (!rel) nx_board_key(comp, d & 7);
		return;
	}
	if (d & 0x40) {
		if (!rel) kbd->nxCaps = (kbd->nxCaps + 1) & 7;
		else if (kbd->nxCaps) kbd->nxCaps--;
	}
	if (d & 0x80) {
		if (!rel) kbd->nxSym = (kbd->nxSym + 1) & 7;
		else if (kbd->nxSym) kbd->nxSym--;
	}
	if ((d & 7) != 7) {
		int row = (d >> 3) & 7;
		if (rel)
			kbd->nxKeys[row] &= ~(1 << (d & 7));
		else
			kbd->nxKeys[row] |= 1 << (d & 7);
		if (((d & 7) >= 5) && !kbd->nxScan)
			kbd->nxScan = 1;		// an extended key: the scan commits it at the next step
	}
}

// a host key's set 2 bytes, first in the low byte: E0 prefixes the code after it
static void nx_ps2_key(Computer* comp, int seq, int rel) {
	int ext = 0;
	for (; seq; seq >>= 8) {
		int b = seq & 0xff;
		if (b == 0xe0) {
			ext = 1;
		} else if (b != 0xe1) {		// Pause resets the matrix on the board: left alone
			nx_ps2_code(comp, b, ext, rel);
			ext = 0;
		}
	}
}

// A host key is the ps/2 keyboard; a key with no ps/2 code (the autostart's, the
// virtual keyboard's, a gamepad's Spectrum key) is pressed on the membrane
void nx_keyp(Computer* comp, keyEntry* ent) {
	if (ent->atCode) nx_ps2_key(comp, ent->atCode, 0);
	else kbd_press(comp->keyb, ent);
}

void nx_keyr(Computer* comp, keyEntry* ent) {
	if (ent->atCode) nx_ps2_key(comp, ent->atCode, 1);
	else kbd_release(comp->keyb, ent);
}

// == the matrix ==

// nr_05's joystick types: joy0 = b3 b7 b6, joy1 = b1 b5 b4
static int nx_joy_type(Computer* comp, int j) {
	int v = NX.nr[0x05];
	return j ? (((v >> 1) & 1) << 2) | ((v >> 4) & 3) : (((v >> 3) & 1) << 2) | ((v >> 6) & 3);
}

// what a joystick's MD connector gives, active high: MODE X Z Y START A C B U D L R
static int nx_joy_state(Computer* comp, int j) {
	return (j ? comp->joyb->state : comp->joy->state) & 0xfff;
}

// The key joysticks' columns on each row, 1 pressed (membrane_stick.vhd 113-196):
// the bits of a type, each looked up in the map from the type's first entry. I/O
// mode (nr_0B b7) turns them off.
static void nx_joy_keys(Computer* comp, unsigned char* m) {
	if (NX.nr[0x0b] & 0x80) return;
	for (int j = 0; j < 2; j++) {
		int st = nx_joy_state(comp, j);
		if (!st) continue;
		int a, b0, b1;
		switch (nx_joy_type(comp, j)) {
			case 3: a = 0x00; b0 = 0; b1 = 4; break;	// Sinclair 1
			case 0: a = 0x05; b0 = 0; b1 = 4; break;	// Sinclair 2
			case 2: a = 0x0a; b0 = 0; b1 = 4; break;	// Cursor
			case 7: a = 0x10; b0 = 0; b1 = 11; break;	// user defined
			case 1: case 4: a = 0x15; b0 = 5; b1 = 11; break;	// Kempston: the user's entries for 5-11
			default: a = 0x18; b0 = 8; b1 = 11; break;	// MD: for 8-11
		}
		for (int b = b0; b <= b1; b++, a++) {
			if (!((st >> b) & 1)) continue;
			int e = IN.joymap[(j << 5) | a];
			if ((e & 7) != 7)
				m[(e >> 3) & 7] |= 1 << (e & 7);
		}
	}
}

// The 8x7 matrix the scan stores, 1 pressed: the ps/2 keyboard (CAPS SHIFT on row 0
// and SYM SHIFT on row 7 while it counts any) and the key joysticks. The membrane is
// the Keyboard's own and comes in through xInFE. Returns 0 when nothing is down.
static int nx_matrix(Computer* comp, unsigned char* m) {
	Keyboard* kbd = comp->keyb;
	int any = 0;
	for (int r = 0; r < 8; r++) {
		m[r] = kbd->nxKeys[r];
		any |= m[r];
	}
	if (kbd->nxCaps) m[0] |= 1;
	if (kbd->nxSym) m[7] |= 2;
	any |= kbd->nxCaps | kbd->nxSym;
	if (comp->joy->state || comp->joyb->state) {
		nx_joy_keys(comp, m);
		for (int r = 0; r < 8; r++) any |= m[r];
	}
	return any;
}

// The extra columns of a matrix, row r's columns 6, 5 at bits 2r+1, 2r
static int nx_ext_cols(const unsigned char* m) {
	int w = 0;
	for (int r = 0; r < 8; r++)
		w |= ((m[r] >> 5) & 3) << (2 * r);
	return w;
}

// The extra columns as 8x5 entries (membrane.vhd 193-226), 1 pressed, per row: CAPS
// SHIFT for UP, EXTEND, GRAPH, CAPS LOCK, the VIDEOs, EDIT, BREAK and the arrows and
// DELETE, SYM SHIFT for EXTEND and the four punctuation keys - those from sh - and
// the key of each, from k
static void nx_ext_entries(int k, int sh, unsigned char* e) {
	int c6[8], c5[8];
	int cs = 0, ss = 0;
	for (int r = 0; r < 8; r++) {
		c6[r] = (k >> (2 * r + 1)) & 1;
		c5[r] = (k >> (2 * r)) & 1;
		int s = (sh >> (2 * r)) & 3;
		if ((r != 4) && (r != 5)) cs |= s;
		if (r == 0) ss |= s & 1;
		if ((r == 4) || (r == 5)) ss |= s;
	}
	memset(e, 0, 8);
	e[0] = cs ? 1 : 0;						// A8: CAPS SHIFT
	e[3] = c6[3] | (c5[1] << 1) | (c5[2] << 2) | (c6[2] << 3) | (c5[7] << 4);	// 1 2 3 4 5
	e[4] = c5[6] | (c6[1] << 1) | (c6[6] << 2) | (c6[0] << 3) | (c6[7] << 4);	// 0 9 8 7 6
	e[5] = c6[4] | (c5[4] << 1);					// P O
	e[7] = c5[3] | ((ss ? 1 : 0) << 1) | (c6[5] << 2) | (c5[5] << 3);	// SPACE, SYM SHIFT, M, N
}

// == the scan of the extended keys ==
// membrane.vhd 177-232: at each commit of the scan the extra columns W(n) move on a
// stage; their 8x5 entries come out of W(n-1), the CAPS/SYM SHIFT they make out of
// W(n) | W(n-1) | W(n-2) | W(n-3): the shift one scan before the key and two after
// it, "to make it harder for naive software to incorrectly read composite keys".
// A change commits at once - a ps/2 key at the step after it, anything else (the
// virtual keyboard, a key joystick) when a read finds it - and the scan then steps
// every 164.6 us (KBD_SCAN_NS) until it settles.

static void nx_scan_commit(Keyboard* kbd, int w) {
	memmove(kbd->nxW + 1, kbd->nxW, 3 * sizeof(kbd->nxW[0]));
	kbd->nxW[0] = w;
}

static int nx_scan_settled(Keyboard* kbd) {
	return (kbd->nxW[1] == kbd->nxW[0]) && (kbd->nxW[2] == kbd->nxW[0]) && (kbd->nxW[3] == kbd->nxW[0]);
}

void nx_inp_sync(Computer* comp, int ns) {
	Keyboard* kbd = comp->keyb;
	unsigned char m[8];
	kbd->nxScan -= ns;
	while (kbd->nxScan <= 0) {
		nx_matrix(comp, m);
		nx_scan_commit(kbd, nx_ext_cols(m));
		if (nx_scan_settled(kbd)) {
			kbd->nxScan = 0;
			break;
		}
		kbd->nxScan += KBD_SCAN_NS;
	}
}

// Port FE bits 4:0 the ps/2 keyboard and the key joysticks pull low on the rows
// the address selects, the extended keys' entries with them through the scan unless
// nr_68 b4 cancels them (the membrane's own part is ANDed in by xInFE)
int nx_inp_fe(Computer* comp, int port) {
	Keyboard* kbd = comp->keyb;
	unsigned char m[8], e[8];
	int any = nx_matrix(comp, m);
	int w = nx_ext_cols(m);
	if (!kbd->nxScan && (w != kbd->nxW[0])) {
		nx_scan_commit(kbd, w);
		kbd->nxScan = KBD_SCAN_NS;
	}
	int sh = kbd->nxW[0] | kbd->nxW[1] | kbd->nxW[2] | kbd->nxW[3];
	if (!any && !sh) return 0x1f;
	if (NX.nr[0x68] & 0x10) memset(e, 0, 8);
	else nx_ext_entries(kbd->nxW[1], sh, e);
	int res = 0x1f;
	for (int r = 0; r < 8; r++) {
		if (!((port >> (8 + r)) & 1))
			res &= ~(m[r] | e[r]);
	}
	return res & 0x1f;
}

// the extended keys (membrane.vhd 253-254), 1 pressed: row r's columns 6, 5 at bits
// 2r+1, 2r - DOWN LEFT RIGHT DELETE . , " ; EDIT BREAK INV TRU GRAPH CAPSLOCK UP EXTEND
static int nx_ext_keys(Computer* comp) {
	unsigned char m[8];
	int res = 0;
	nx_matrix(comp, m);
	for (int r = 0; r < 8; r++)
		res |= ((m[r] >> 5) & 3) << (2 * r);
	return res;
}

// NR B0, B1, B2 (zxnext.vhd 6203-6215)
int nx_inp_nr_read(Computer* comp, int r) {
	int x = (r == 0xb2) ? 0 : nx_ext_keys(comp);
	int jl = nx_joy_state(comp, 0);
	int jr = nx_joy_state(comp, 1);
	switch (r) {
		case 0xb0:	// ; " , . UP DOWN LEFT RIGHT
			return (((x >> 8) & 1) << 7) | (((x >> 9) & 1) << 6) | (((x >> 10) & 1) << 5) | (((x >> 11) & 1) << 4)
				| (((x >> 1) & 1) << 3) | ((x >> 13) & 7);
		case 0xb1:	// DELETE EDIT BREAK INV TRU GRAPH CAPSLOCK EXTEND
			return (((x >> 12) & 1) << 7) | (((x >> 2) & 0x3f) << 1) | (x & 1);
		case 0xb2:	// right X Z Y MODE, left X Z Y MODE
			return (((jr >> 8) & 7) << 5) | (((jr >> 11) & 1) << 4) | (((jl >> 8) & 7) << 1) | ((jl >> 11) & 1);
	}
	return 0;
}

// == the ports ==

// Kempston / MD 1F and 37, the 1F alias on DF, the Kempston mouse (zxnext.vhd 2407-2455,
// 2668-2675, 3472-3562). A port with no joystick in its mode is not decoded at all; a
// read answered here sets *any.
int nx_inp_in(Computer* comp, int port, int* any) {
	unsigned char* nr = NX.nr;
	int lo = port & 0xff;
	if ((lo != 0x1f) && (lo != 0x37) && (lo != 0xdf)) return 0;
	int mouse = nr[0x83] & 0x20;
	if ((lo == 0xdf) && mouse) {
		Mouse* ms = comp->mouse;
		int res;
		ms->used = 1;
		switch ((port >> 8) & 15) {
			case 0x0a: {
				// wheel, 1, not M, not L, not R (nr_0A b3 swaps the two in the ps/2 module)
				int l = ms->swapButtons ? ms->rmb : ms->lmb;
				int r = ms->swapButtons ? ms->lmb : ms->rmb;
				if (nr[0x0a] & 8) {int t = l; l = r; r = t;}
				res = ((ms->wheel & 15) << 4) | 0x08 | ((!ms->mmb) << 2) | ((!l) << 1) | !r;
				break;
			}
			case 0x0b: case 0x0f: {
				// the counters add the moves scaled by the dpi nr_0A has as they come:
				// x2, x1, /2, /4 (ps2_mouse.v 165-203)
				static const double dpi[4] = {2.0, 1.0, 0.5, 0.25};
				double k = ms->sensitivity * dpi[nr[0x0a] & 3];
				IN.mx += (ms->xpos - IN.hx) * k;
				IN.my += (ms->ypos - IN.hy) * k;
				IN.hx = ms->xpos;
				IN.hy = ms->ypos;
				res = (int)floor((((port >> 8) & 15) == 0x0b) ? IN.mx : IN.my) & 0xff;
				break;
			}
			default:
				return 0;	// unclaimed
		}
		*any = 1;
		return res;
	}
	int t0 = nx_joy_type(comp, 0);
	int t1 = nx_joy_type(comp, 1);
	int res = 0;
	if (lo == 0x37) {
		if (!(nr[0x82] & 0x80) || !((t0 == 4) || (t0 == 6) || (t1 == 4) || (t1 == 6))) return 0;
		if ((t0 == 4) || (t0 == 6)) res |= nx_joy_state(comp, 0) & ((t0 == 6) ? 0xff : 0x3f);
		if ((t1 == 4) || (t1 == 6)) res |= nx_joy_state(comp, 1) & ((t1 == 6) ? 0xff : 0x3f);
	} else {
		if ((lo == 0xdf) && !(nr[0x84] & 0x80)) return 0;
		if (!(nr[0x82] & 0x40) || !((t0 == 1) || (t0 == 5) || (t1 == 1) || (t1 == 5))) return 0;
		if ((t0 == 1) || (t0 == 5)) res |= nx_joy_state(comp, 0) & ((t0 == 5) ? 0xff : 0x3f);
		if ((t1 == 1) || (t1 == 5)) res |= nx_joy_state(comp, 1) & ((t1 == 5) ? 0xff : 0x3f);
	}
	comp->joy->used = 1;
	*any = 1;
	return res;
}
