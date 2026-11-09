#include <QHeaderView>
#include <QMouseEvent>
#include <QPainter>
#include <QSplitter>
#include <QVBoxLayout>

#include "dbg_next.h"
#include "../xgui.h"

// The register and field descriptions follow the core's nextreg.txt and ports.txt
// (3.02.03); the values come from the core, read the way a viewer may (next_nr_peek).

// HELPERS

static QString hx(int v) {return gethexbyte(v & 0xff);}
static QString hx3(int v) {return gethexword(v & 0xfff).right(3);}

static QRgb nx_qrgb(int c9) {
	unsigned int v = next_rgb(c9 & 0x1ff);
	return qRgb(v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff);
}

static QString nx_rgb_text(int c9) {
	QRgb c = nx_qrgb(c9);
	return "#" + gethexbyte(qRed(c)) + gethexbyte(qGreen(c)) + gethexbyte(qBlue(c));
}

// an 8-bit RRRGGGBB as the 9-bit colour it stands for: the low blue is B1 | B0
static int nx_rgb9(int v8) {
	return ((v8 & 0xff) << 1) | (((v8 >> 1) | v8) & 1);
}

// the words of the bits that are set (the panel's own bit table, words only)
static QString nx_bits(int v, const evoBit* t, const char* none) {
	QStringList s;
	for (; t->mask; t++)
		if ((v & t->mask) && t->words) s << t->words;
	return s.isEmpty() ? QString(none) : s.join(", ");
}

static QString nx_join(const QStringList& s, const char* none = "-") {
	QStringList k;
	foreach (QString x, s)
		if (!x.isEmpty()) k << x;
	return k.isEmpty() ? QString(none) : k.join(", ");
}

// the palettes as NR 43 numbers them: layer | second << 2
enum {NXP_ULA = 0, NXP_L2, NXP_SPR, NXP_TM};
static const char* const palLayer[4] = {"ULA", "Layer 2", "Sprites", "Tilemap"};

static QString pal_name(int p) {
	return QString("%0 %1").arg(palLayer[p & 3]).arg((p & 4) ? "second" : "first");
}

// the palette each layer takes now
static int pal_active(Computer* comp, int layer) {
	unsigned char* nr = comp->next->nr;
	switch (layer) {
		case NXP_ULA: return NXP_ULA | ((nr[0x43] & 2) ? 4 : 0);
		case NXP_L2: return NXP_L2 | ((nr[0x43] & 4) ? 4 : 0);
		case NXP_SPR: return NXP_SPR | ((nr[0x43] & 8) ? 4 : 0);
	}
	return NXP_TM | ((nr[0x6b] & 0x10) ? 4 : 0);
}

// a colour of palette p, 0 (transparent) when global transparency takes it
static QRgb pal_dot(Computer* comp, int p, int idx, bool global) {
	int c = comp->next->pal[p & 7][idx & 0xff] & 0x1ff;
	if (global && ((c >> 1) == comp->next->nr[0x14])) return 0;
	return nx_qrgb(c);
}

// an 8K RAM page and the 16K bank it is half of
static QString page_text(int pg) {
	return QString("page %0, bank %1 %2").arg(hx(pg)).arg(pg >> 1).arg((pg & 1) ? "high" : "low");
}

// the nextregs by their names in nextreg.txt
static const struct {
	int reg;
	const char* name;
} nrNames[] = {
	{0x00, "Machine ID"}, {0x01, "Core Version"}, {0x02, "Reset"}, {0x03, "Machine Type"},
	{0x04, "Config Mapping"}, {0x05, "Peripheral 1"}, {0x06, "Peripheral 2"}, {0x07, "CPU Speed"},
	{0x08, "Peripheral 3"}, {0x09, "Peripheral 4"}, {0x0a, "Peripheral 5"}, {0x0b, "Joystick I/O"},
	{0x0e, "Core Version sub"}, {0x0f, "Board ID"}, {0x10, "Core Boot"}, {0x11, "Video Timing"},
	{0x12, "L2 Active Bank"}, {0x13, "L2 Shadow Bank"}, {0x14, "Transparency"}, {0x15, "Sprites/Layers"},
	{0x16, "L2 X Scroll"}, {0x17, "L2 Y Scroll"}, {0x18, "Clip L2"}, {0x19, "Clip Sprites"},
	{0x1a, "Clip ULA/LoRes"}, {0x1b, "Clip Tilemap"}, {0x1c, "Clip Control"}, {0x1e, "Video Line MSB"},
	{0x1f, "Video Line LSB"}, {0x20, "Generate INT"}, {0x22, "Line INT Control"}, {0x23, "Line INT LSB"},
	{0x24, "Reserved"}, {0x26, "ULA X Scroll"}, {0x27, "ULA Y Scroll"}, {0x28, "Keymap Addr MSB"},
	{0x29, "Keymap Addr LSB"}, {0x2a, "Keymap Data MSB"}, {0x2b, "Keymap Data LSB"}, {0x2c, "DAC B"},
	{0x2d, "DAC A+D"}, {0x2e, "DAC C"}, {0x2f, "Tilemap X MSB"}, {0x30, "Tilemap X LSB"},
	{0x31, "Tilemap Y"}, {0x32, "LoRes X Scroll"}, {0x33, "LoRes Y Scroll"}, {0x34, "Sprite Number"},
	{0x35, "Sprite Attr 0"}, {0x36, "Sprite Attr 1"}, {0x37, "Sprite Attr 2"}, {0x38, "Sprite Attr 3"},
	{0x39, "Sprite Attr 4"}, {0x40, "Palette Index"}, {0x41, "Palette Value 8"}, {0x42, "ULANext Format"},
	{0x43, "Palette Control"}, {0x44, "Palette Value 9"}, {0x4a, "Fallback Colour"}, {0x4b, "Sprite Transp."},
	{0x4c, "Tilemap Transp."}, {0x50, "MMU 0"}, {0x51, "MMU 1"}, {0x52, "MMU 2"}, {0x53, "MMU 3"},
	{0x54, "MMU 4"}, {0x55, "MMU 5"}, {0x56, "MMU 6"}, {0x57, "MMU 7"}, {0x60, "Copper Data 8"},
	{0x61, "Copper Addr LSB"}, {0x62, "Copper Control"}, {0x63, "Copper Data 16"}, {0x64, "Line Offset"},
	{0x68, "ULA Control"}, {0x69, "Display Control 1"}, {0x6a, "LoRes Control"}, {0x6b, "Tilemap Control"},
	{0x6c, "Tile Attribute"}, {0x6e, "Tilemap Base"}, {0x6f, "Tile Defs Base"}, {0x70, "Layer 2 Control"},
	{0x71, "L2 X Scroll MSB"}, {0x75, "Sprite Attr 0+"}, {0x76, "Sprite Attr 1+"}, {0x77, "Sprite Attr 2+"},
	{0x78, "Sprite Attr 3+"}, {0x79, "Sprite Attr 4+"}, {0x7f, "User Register"}, {0x80, "Exp. Bus Enable"},
	{0x81, "Exp. Bus Control"}, {0x82, "Int. Port Dec. 0"}, {0x83, "Int. Port Dec. 1"}, {0x84, "Int. Port Dec. 2"},
	{0x85, "Int. Port Dec. 3"}, {0x86, "Exp. Port Dec. 0"}, {0x87, "Exp. Port Dec. 1"}, {0x88, "Exp. Port Dec. 2"},
	{0x89, "Exp. Port Dec. 3"}, {0x8a, "Exp. Bus IO Prop."}, {0x8c, "Alternate ROM"}, {0x8e, "128K Mapping"},
	{0x8f, "Mapping Mode"}, {0x90, "Pi GPIO OE 0"}, {0x91, "Pi GPIO OE 1"}, {0x92, "Pi GPIO OE 2"},
	{0x93, "Pi GPIO OE 3"}, {0x98, "Pi GPIO 0"}, {0x99, "Pi GPIO 1"}, {0x9a, "Pi GPIO 2"}, {0x9b, "Pi GPIO 3"},
	{0xa0, "Pi Peripherals"}, {0xa2, "Pi I2S Control"}, {0xa8, "ESP GPIO Enable"}, {0xa9, "ESP GPIO"},
	{0xb0, "Extended Keys 0"}, {0xb1, "Extended Keys 1"}, {0xb2, "MD Pad Buttons"}, {0xb8, "divMMC Entry 0"},
	{0xb9, "divMMC Valid 0"}, {0xba, "divMMC Timing 0"}, {0xbb, "divMMC Entry 1"}, {0xc0, "INT Control"},
	{0xc2, "NMI Return LSB"}, {0xc3, "NMI Return MSB"}, {0xc4, "INT EN 0"}, {0xc5, "INT EN 1"},
	{0xc6, "INT EN 2"}, {0xc8, "INT Status 0"}, {0xc9, "INT Status 1"}, {0xca, "INT Status 2"},
	{0xcc, "DMA INT EN 0"}, {0xcd, "DMA INT EN 1"}, {0xce, "DMA INT EN 2"}, {0xd8, "I/O Traps"},
	{0xd9, "I/O Trap Write"}, {0xda, "I/O Trap Cause"}, {0xf0, "XDEV CMD"}, {0xf8, "XADC REG"},
	{0xf9, "XADC D0"}, {0xfa, "XADC D1"}, {0xff, "Internal"},
	{-1, nullptr}
};

static const char* nr_name(int r) {
	for (int i = 0; nrNames[i].name; i++)
		if (nrNames[i].reg == r) return nrNames[i].name;
	return nullptr;
}

// PICTURE VIEW

xNxPicView::xNxPicView(QWidget* p):xEvoFitView(p) {
	setMouseTracking(true);
	setMinimumSize(128, 96);
}

QSize xNxPicView::logical() const {
	return shown.isEmpty() ? img.size() : shown;
}

// as big as fits in w x h, in whole steps once it is bigger than 1:1, at the top left
QRect xNxPicView::targetIn(int w, int h) const {
	QSize s = logical();
	if (img.isNull() || s.isEmpty()) return QRect();
	double k = qMin((double)w / s.width(), (double)h / s.height());
	if (k >= 1) k = (int)k;
	return QRect(0, 0, qMax(1, (int)(s.width() * k)), qMax(1, (int)(s.height() * k)));
}

int xNxPicView::widthFor(int w, int h) const {
	QRect t = targetIn(w, h);
	return t.isEmpty() ? w : t.width();
}

void xNxPicView::paintEvent(QPaintEvent*) {
	QPainter pnt(this);
	QRect t = targetIn(width(), height());
	if (t.isEmpty()) return;
	pnt.fillRect(t, evo_checker(this));
	pnt.drawImage(t, img);
	double kx = (double)t.width() / img.width();
	double ky = (double)t.height() / img.height();
	QColor col = conf.pal.value("dbg.header.bg");
	if (!col.isValid()) col = Qt::yellow;
	pnt.setPen(QPen(col, 2));
	pnt.setBrush(Qt::NoBrush);
	foreach (QRect r, marks)
		pnt.drawRect(QRectF(t.x() + r.x() * kx, t.y() + r.y() * ky, r.width() * kx, r.height() * ky));
}

void xNxPicView::mouseMoveEvent(QMouseEvent* ev) {
	QRect t = targetIn(width(), height());
	if (t.isEmpty() || !t.contains(ev->pos())) {
		if (onHover) onHover(-1, -1);
		return;
	}
	if (onHover) onHover((ev->pos().x() - t.x()) * img.width() / t.width(), (ev->pos().y() - t.y()) * img.height() / t.height());
}

void xNxPicView::leaveEvent(QEvent*) {
	if (onHover) onHover(-1, -1);
}

// REGISTERS PAGE

xNxRegsPage::xNxRegsPage(QWidget* p):xEvoPage(HW_ZXNEXT, p) {
	QVBoxLayout* lay = new QVBoxLayout(this);
	lay->setContentsMargins(0, 0, 0, 0);
	cbRaw = new QCheckBox("All 256, as a read gives them");
	cbRaw->setObjectName("nxRaw");
	lay->addWidget(cbRaw);
	list = new xEvoList({"Reg", "Name", "Value", "Means"});
	list->wordsOnly();
	lay->addWidget(list, 1);
	// the rows of the other set: the draw drops the ones it no longer puts
	connect(cbRaw, &QCheckBox::toggled, this, [this]() {if (conf.zx) draw(conf.zx);});
}

static const char* const joyNames[8] = {"Sinclair 2", "Kempston 1", "Cursor", "Sinclair 1", "Kempston 2", "MD 1", "MD 2", "user keys"};
static const char* const machNames[8] = {"config mode", "48K", "128K/+2", "+2A/+3", "Pentagon", "?", "?", "?"};
static const char* const timNames[4] = {"48K", "128K/+2", "+2A/+3", "Pentagon"};
static const char* const speedNames[4] = {"3.5", "7", "14", "28"};
static const char* const orderNames[8] = {"S L U", "L S U", "S U L", "L U S", "U S L", "U L S",
	"(U|T) S (T|U) (B+L)", "(U|T) S (T|U) (B+L-5)"};
static const char* const ctcStates[5] = {"reset", "reset, constant next", "waiting for a trigger", "running", "running, constant next"};

static const evoBit p06Bits[] = {
	{0x80, nullptr, "F8, F5, F6 keys"}, {0x40, nullptr, "BEEP to the speaker only"}, {0x20, nullptr, "F3 key"},
	{0x10, nullptr, "DRIVE button NMI"}, {0x08, nullptr, "M1 button NMI"}, {0x04, nullptr, "PS/2 mouse first"},
	{0, nullptr, nullptr}
};
static const evoBit p08Bits[] = {
	{0x40, nullptr, "no contention"}, {0x10, nullptr, "speaker"}, {0x08, nullptr, "DACs"},
	{0x04, nullptr, "port FF readable"}, {0x02, nullptr, "TurboSound"}, {0x01, nullptr, "issue 2 keyboard"},
	{0, nullptr, nullptr}
};
static const evoBit p09Bits[] = {
	{0x80, nullptr, "AY 2 mono"}, {0x40, nullptr, "AY 1 mono"}, {0x20, nullptr, "AY 0 mono"},
	{0x10, nullptr, "sprite number lockstep"}, {0x04, nullptr, "HDMI sound off"},
	{0, nullptr, nullptr}
};
static const evoBit p0aBits[] = {
	{0x20, nullptr, "SD cards swapped"}, {0x10, nullptr, "divMMC automap"}, {0x08, nullptr, "mouse buttons swapped"},
	{0, nullptr, nullptr}
};
static const evoBit p15Bits[] = {
	{0x01, nullptr, "sprites"}, {0x02, nullptr, "over the border"}, {0x20, nullptr, "clipped over the border"},
	{0x40, nullptr, "sprite 0 on top"}, {0x80, nullptr, "LoRes"},
	{0, nullptr, nullptr}
};
static const evoBit p68Bits[] = {
	{0x80, nullptr, "ULA off"}, {0x10, nullptr, "extended keys out of the matrix"}, {0x08, nullptr, "ULA+"},
	{0x04, nullptr, "half pixel scroll"}, {0x01, nullptr, "stencil"},
	{0, nullptr, nullptr}
};
static const evoBit p6bBits[] = {
	{0x20, nullptr, "no attributes"}, {0x10, nullptr, "second palette"}, {0x08, nullptr, "text mode"},
	{0x02, nullptr, "512 tiles"}, {0x01, nullptr, "over the ULA"},
	{0, nullptr, nullptr}
};
static const evoBit intEn0Bits[] = {{0x80, nullptr, "expansion bus"}, {0x02, nullptr, "line"}, {0x01, nullptr, "ULA"}, {0, nullptr, nullptr}};
static const evoBit intSt0Bits[] = {{0x02, nullptr, "line"}, {0x01, nullptr, "ULA"}, {0, nullptr, nullptr}};
static const evoBit dmaEn0Bits[] = {{0x80, nullptr, "NMI"}, {0x02, nullptr, "line"}, {0x01, nullptr, "ULA"}, {0, nullptr, nullptr}};
static const evoBit uartBits[] = {
	{0x40, nullptr, "Pi Tx empty"}, {0x20, nullptr, "Pi Rx near full"}, {0x10, nullptr, "Pi Rx"},
	{0x04, nullptr, "ESP Tx empty"}, {0x02, nullptr, "ESP Rx near full"}, {0x01, nullptr, "ESP Rx"},
	{0, nullptr, nullptr}
};

static QString ctc_bits(int v) {
	QStringList s;
	for (int i = 0; i < 8; i++)
		if (v & (1 << i)) s << QString::number(i);
	return s.isEmpty() ? "none" : "CTC " + s.join(" ");
}

static QString clip_text(Computer* comp, int w) {
	unsigned char* c = comp->next->clip[w];
	static const char* const next[4] = {"X1", "X2", "Y1", "Y2"};
	return QString("x %0..%1, y %2..%3; %4 next").arg(c[0]).arg(c[1]).arg(c[2]).arg(c[3]).arg(next[comp->next->clipIdx[w] & 3]);
}

// the address in bank 5 or 7 NR 6E/6F point at, as the cpu sees it in its usual map
static QString tm_base(int v) {
	if (v & 0x80) return QString("bank 7, #%0").arg(gethexword(0xc000 | ((v & 0x1f) << 8)));
	return QString("bank 5, #%0").arg(gethexword(0x4000 | ((v & 0x3f) << 8)));
}

static QString mmu_text(Computer* comp, int s) {
	int pg = comp->next->mmu[s];
	QString area = QString("#%0: ").arg(gethexword(s << 13));
	if (pg == 0xff && (s < 2)) return area + QString("ROM %0").arg(next_rom(comp));
	// no SRAM cycle: the byte of the last SRAM read stays (the GHDL run of the core shows it)
	if (pg >= 0xe0) return area + "nothing (the last SRAM byte read)";
	if (pg >= next_ram_pages(comp)) return area + page_text(pg) + ", no chip (1 MB board)";
	return area + page_text(pg);
}

void xNxRegsPage::draw(Computer* comp) {
	xNextState& N = *comp->next;
	unsigned char* nr = N.nr;
	auto rd = [comp](int r) {return next_nr_peek(comp, r);};
	list->begin();
	if (cbRaw->isChecked()) {
		for (int r = 0; r < 256; r++) {
			const char* n = nr_name(r);
			QString g = QString("%0..%1").arg(hx(r & 0xc0)).arg(hx(r | 0x3f));
			list->put(g, hx(r), {hx(r), n ? n : "-", hx(rd(r)), ""});
		}
		list->finish();
		return;
	}
	QString g = "Machine";
	int v;
	v = rd(0x00);
	list->put(g, "00", {"00", "Machine ID", hx(v), (v == 0x0a) ? "ZX Spectrum Next" : (v == 0x08) ? "emulator" : "other board"});
	v = rd(0x01);
	list->put(g, "01", {"01/0E", "Core version", hx(v), QString("%0.%1.%2").arg(v >> 4).arg(v & 15, 2, 10, QChar('0')).arg(rd(0x0e), 2, 10, QChar('0'))});
	v = rd(0x03);
	list->put(g, "03", {"03", "Machine type", hx(v), nx_join({QString("%0, %1 timing").arg(machNames[v & 7]).arg(timNames[N.timing & 3]),
		(v & 8) ? "timing locked" : "", N.configMode ? "config mode" : ""})});
	v = rd(0x05);
	list->put(g, "05", {"05", "Peripheral 1", hx(v), QString("joysticks %0 and %1, %2 Hz%3")
		.arg(joyNames[(((v >> 3) & 1) << 2) | ((v >> 6) & 3)]).arg(joyNames[(((v >> 1) & 1) << 2) | ((v >> 4) & 3)])
		.arg(N.hz60 ? 60 : 50).arg((v & 1) ? ", scandoubler" : "")});
	v = rd(0x06);
	static const char* const chips[4] = {"YM", "AY", "ZXN-8950", "AYs held in reset"};
	list->put(g, "06", {"06", "Peripheral 2", hx(v), nx_join({chips[v & 3], nx_bits(v, p06Bits, "")})});
	v = rd(0x07);
	list->put(g, "07", {"07", "CPU speed", hx(v), QString("%0 MHz%1").arg(speedNames[(v >> 4) & 3])
		.arg(((v >> 4) & 3) != (v & 3) ? QString(", %0 asked").arg(speedNames[v & 3]) : QString())});
	v = rd(0x08);
	list->put(g, "08", {"08", "Peripheral 3", hx(v), nx_join({(v & 0x80) ? "7FFD open" : "7FFD locked", (v & 0x20) ? "ACB" : "ABC",
		nx_bits(v, p08Bits, "")})});
	v = rd(0x09);
	static const char* const scan[4] = {"", "scanlines 50%", "scanlines 25%", "scanlines 12.5%"};
	list->put(g, "09", {"09", "Peripheral 4", hx(v), nx_join({nx_bits(v, p09Bits, ""), scan[v & 3]})});
	v = rd(0x0a);
	static const char* const mfs[4] = {"Multiface +3", "Multiface 128 v87.2", "Multiface 128 v87.12", "Multiface 1"};
	static const char* const dpi[4] = {"low", "default", "medium", "high"};
	list->put(g, "0a", {"0A", "Peripheral 5", hx(v), nx_join({mfs[(v >> 6) & 3], nx_bits(v, p0aBits, ""), QString("mouse %0 dpi").arg(dpi[v & 3])})});

	g = "Memory";
	for (int s = 0; s < 8; s++)
		list->put(g, QString("mmu%0").arg(s), {hx(0x50 + s), QString("MMU %0").arg(s), hx(N.mmu[s]), mmu_text(comp, s)});
	v = N.p7ffd;
	list->put(g, "7ffd", {"7FFD", "128K paging", hx(v), nx_join({QString("bank %0 at #C000").arg((v & 7) | ((N.pdffd & 0x0f) << 3)),
		QString("screen %0").arg((v & 8) ? 7 : 5), QString("ROM bit %0").arg((v >> 4) & 1), (v & 0x20) ? "locked" : ""})});
	list->put(g, "dffd", {"DFFD", "Extra banks", hx(N.pdffd), QString("bank bits 6:3 = %0").arg(N.pdffd & 0x0f)});
	v = N.p1ffd;
	static const char* const allRam[4] = {"0 1 2 3", "4 5 6 7", "4 5 6 3", "4 7 6 3"};
	list->put(g, "1ffd", {"1FFD", "+3 paging", hx(v), (v & 1) ? QString("all RAM: banks %0").arg(allRam[(v >> 1) & 3])
		: nx_join({QString("ROM high bit %0").arg((v >> 2) & 1), (v & 8) ? "disk motor" : ""})});
	list->put(g, "eff7", {"EFF7", "Pentagon 1024", hx(N.peff7), nx_join({(N.peff7 & 8) ? "RAM 0 at #0000" : "", (N.peff7 & 4) ? "7FFD as 128K" : ""})});
	static const char* const maps[4] = {"ZX 128K/+3", "reserved", "Pentagon 512K", "Pentagon 1024K"};
	list->put(g, "8f", {"8F", "Mapping mode", hx(nr[0x8f]), maps[nr[0x8f] & 3]});
	v = nr[0x8c];
	list->put(g, "8c", {"8C", "Alternate ROM", hx(v), nx_join({(v & 0x80) ? ((v & 0x40) ? "alt ROM for writes" : "alt ROM") : "",
		(v & 0x20) ? "48K ROM locked" : "", (v & 0x10) ? "128K ROM locked" : ""}, "off")});
	v = N.p123b;
	static const char* const segs[4] = {"#0000-#3FFF", "#4000-#7FFF", "#8000-#BFFF", "#0000-#BFFF"};
	QString l2m = "not mapped";
	if (v & 5) {
		l2m = QString("%0 at %1, %2 bank").arg((v & 5) == 5 ? "reads and writes" : (v & 1) ? "writes" : "reads")
			.arg(segs[(v >> 6) & 3]).arg((v & 8) ? "shadow" : "active");
		if (N.l2ofs) l2m += QString(" + %0").arg(N.l2ofs);
	}
	list->put(g, "123b", {"123B", "Layer 2 port", hx(v), l2m});
	v = N.pe3;
	list->put(g, "e3", {"E3", "divMMC", hx(v), nx_join({QString("bank %0").arg(v & 15), (v & 0x80) ? "CONMEM" : "", (v & 0x40) ? "MAPRAM" : "",
		(N.dmHeld || N.dmMap) ? "mapped now" : ""})});
	list->put(g, "b8", {"B8..BB", "divMMC entries", QString("%0 %1 %2 %3").arg(hx(nr[0xb8]), hx(nr[0xb9]), hx(nr[0xba]), hx(nr[0xbb])), ""});
	list->put(g, "mf", {"", "Multiface", "", nx_join({N.irq.mfEnable ? "paged in" : "", N.irq.mfNmi ? "in its NMI" : "",
		N.irq.mfInvisible ? "hidden" : ""})});

	g = "Video";
	v = nr[0x15];
	list->put(g, "15", {"15", "Sprites, layers", hx(v), nx_join({QString("order %0").arg(orderNames[(v >> 2) & 7]), nx_bits(v, p15Bits, "")})});
	v = rd(0x69);
	list->put(g, "69", {"69", "Display control", hx(v), nx_join({(v & 0x80) ? "layer 2 on" : "layer 2 off",
		QString("ULA screen in bank %0").arg((v & 0x40) ? 7 : 5), QString("Timex mode %0").arg(hx(v & 0x3f))})});
	v = nr[0x68];
	static const char* const blend[4] = {"ULA blends", "no blending", "ULA and tilemap blend", "tilemap blends"};
	list->put(g, "68", {"68", "ULA control", hx(v), nx_join({blend[(v >> 5) & 3], nx_bits(v, p68Bits, "")})});
	list->put(g, "ff", {"FF", "Timex port", hx(N.pff), QString("screen mode %0%1").arg(N.pff & 7).arg((N.pff & 0x40) ? ", INT off" : "")});
	list->put(g, "14", {"14", "Transparency", hx(nr[0x14]), nx_rgb_text(nx_rgb9(nr[0x14]))});
	list->put(g, "4a", {"4A", "Fallback colour", hx(nr[0x4a]), nx_rgb_text(nx_rgb9(nr[0x4a]))});
	list->put(g, "4b", {"4B", "Sprite transp.", hx(nr[0x4b]), "index"});
	list->put(g, "4c", {"4C", "Tilemap transp.", hx(nr[0x4c]), "index"});
	v = nr[0x43];
	list->put(g, "43", {"43", "Palette control", hx(v), nx_join({"writes " + pal_name((v >> 4) & 7),
		QString("ULA %0, layer 2 %1, sprites %2").arg((v & 2) ? 2 : 1).arg((v & 4) ? 2 : 1).arg((v & 8) ? 2 : 1),
		(v & 1) ? "ULANext" : "", (v & 0x80) ? "no auto-increment" : ""})});
	list->put(g, "40", {"40", "Palette index", hx(nr[0x40]), ""});
	v = nr[0x42];
	list->put(g, "42", {"42", "ULANext format", hx(v), (v == 0xff) ? "full ink" : QString("ink mask %0").arg(hx(v))});
	list->put(g, "12", {"12", "L2 active bank", hx(nr[0x12]), QString("bank %0, pages %1..").arg(nr[0x12] & 0x7f).arg(hx((nr[0x12] & 0x7f) << 1))});
	list->put(g, "13", {"13", "L2 shadow bank", hx(nr[0x13]), QString("bank %0, pages %1..").arg(nr[0x13] & 0x7f).arg(hx((nr[0x13] & 0x7f) << 1))});
	v = nr[0x70];
	static const char* const l2res[4] = {"256x192, 8 bit", "320x256, 8 bit", "640x256, 4 bit", "?"};
	list->put(g, "70", {"70", "Layer 2 control", hx(v), QString("%0, palette offset %1").arg(l2res[(v >> 4) & 3]).arg(v & 15)});
	v = nr[0x6a];
	list->put(g, "6a", {"6A", "LoRes control", hx(v), nx_join({(v & 0x20) ? "Radastan" : "128x96", (v & 0x10) ? "Timex xor" : "",
		QString("palette offset %0").arg(v & 15)})});
	v = nr[0x6b];
	list->put(g, "6b", {"6B", "Tilemap control", hx(v), nx_join({(v & 0x80) ? "on" : "off", (v & 0x40) ? "80x32" : "40x32", nx_bits(v, p6bBits, "")})});
	v = nr[0x6c];
	list->put(g, "6c", {"6C", "Tile attribute", hx(v), nx_join({QString("palette %0").arg(v >> 4), (v & 8) ? "X mirror" : "",
		(v & 4) ? "Y mirror" : "", (v & 2) ? "rotated" : "", (v & 1) ? "ULA over" : ""})});
	list->put(g, "6e", {"6E", "Tilemap base", hx(nr[0x6e]), tm_base(nr[0x6e])});
	list->put(g, "6f", {"6F", "Tile defs base", hx(nr[0x6f]), tm_base(nr[0x6f])});
	list->put(g, "64", {"64", "Line offset", hx(nr[0x64]), QString::number(nr[0x64])});
	int hc, vc, cvc;
	next_counters(comp, &hc, &vc, &cvc);
	list->put(g, "ray", {"1E/1F", "Video line", QString::number(cvc), QString("ULA line %0, dot %1").arg(vc).arg(hc)});

	g = "Clip and scroll";
	list->put(g, "s2", {"16/17/71", "Layer 2 scroll", "", QString("x %0, y %1").arg(((nr[0x71] & 1) << 8) | nr[0x16]).arg(nr[0x17])});
	list->put(g, "su", {"26/27", "ULA scroll", "", QString("x %0, y %1").arg(nr[0x26]).arg(nr[0x27])});
	list->put(g, "sl", {"32/33", "LoRes scroll", "", QString("x %0, y %1").arg(nr[0x32]).arg(nr[0x33])});
	list->put(g, "st", {"2F/30/31", "Tilemap scroll", "", QString("x %0, y %1").arg(((nr[0x2f] & 3) << 8) | nr[0x30]).arg(nr[0x31])});
	static const char* const clipNames[4] = {"Clip layer 2", "Clip sprites", "Clip ULA/LoRes", "Clip tilemap"};
	for (int w = 0; w < 4; w++)
		list->put(g, QString("c%0").arg(w), {hx(0x18 + w), clipNames[w], hx(N.clip[w][N.clipIdx[w] & 3]), clip_text(comp, w)});

	g = "Interrupts";
	v = rd(0xc0);
	list->put(g, "c0", {"C0", "INT control", hx(v), nx_join({(v & 1) ? QString("hardware IM2, vector %0").arg(hx(v & 0xe0)) : "pulse",
		(v & 8) ? "stackless NMI" : "", QString("Z80 in IM %0").arg((v >> 1) & 3)})});
	v = rd(0x22);
	list->put(g, "22", {"22/23", "Line INT", hx(v), nx_join({(v & 2) ? QString("line %0").arg(((v & 1) << 8) | nr[0x23]) : "line INT off",
		(v & 4) ? "ULA INT off" : "", (v & 0x80) ? "INT up" : ""})});
	list->put(g, "c4", {"C4", "INT EN 0", hx(rd(0xc4)), nx_bits(rd(0xc4), intEn0Bits, "none")});
	list->put(g, "c5", {"C5", "INT EN 1", hx(rd(0xc5)), ctc_bits(rd(0xc5))});
	list->put(g, "c6", {"C6", "INT EN 2", hx(rd(0xc6)), nx_bits(rd(0xc6), uartBits, "none")});
	list->put(g, "c8", {"C8", "INT status 0", hx(rd(0xc8)), nx_bits(rd(0xc8), intSt0Bits, "none")});
	list->put(g, "c9", {"C9", "INT status 1", hx(rd(0xc9)), ctc_bits(rd(0xc9))});
	list->put(g, "ca", {"CA", "INT status 2", hx(rd(0xca)), nx_bits(rd(0xca), uartBits, "none")});
	list->put(g, "cc", {"CC..CE", "DMA INT EN", QString("%0 %1 %2").arg(hx(nr[0xcc]), hx(nr[0xcd]), hx(nr[0xce])),
		nx_join({nx_bits(nr[0xcc], dmaEn0Bits, ""), nr[0xcd] ? ctc_bits(nr[0xcd]) : "", nx_bits(nr[0xce], uartBits, "")}, "none")});
	list->put(g, "c2", {"C2/C3", "NMI return", gethexword((nr[0xc3] << 8) | nr[0xc2]), ""});
	for (int i = 0; i < 4; i++) {
		nxCtcChan* c = &N.ctc.ch[i];
		list->put(g, QString("ctc%0").arg(i), {"", QString("CTC %0").arg(i), hx(c->cr << 3),
			nx_join({ctcStates[qMin((int)c->state, 4)], (c->cr & 8) ? "counter" : QString("timer / %0").arg((c->cr & 4) ? 256 : 16),
			QString("constant %0, count %1").arg(c->tc).arg(c->cnt), (c->cr & 0x10) ? "INT on" : ""})});
	}

	g = "Copper";
	static const char* const copModes[4] = {"stopped", "from 0, loops", "from where it stopped, loops", "from 0, again at each frame"};
	list->put(g, "62", {"62", "Copper control", hx(nr[0x62]), copModes[nr[0x62] >> 6]});
	list->put(g, "pc", {"", "Copper PC", hx3(N.copPc), ""});
	list->put(g, "61", {"61/62", "Write address", hx3(N.coppAdr), QString("instruction %0, %1 byte").arg(hx3(N.coppAdr >> 1)).arg((N.coppAdr & 1) ? "low" : "high")});

	g = "Sound";
	v = nr[0x08];
	list->put(g, "ay", {"FFFD", "AY select", hx(N.snd.sel), nx_join({(v & 2) ? "TurboSound" : "one AY", QString("AY %0").arg(N.snd.sel),
		QString("pan %0%1").arg((N.snd.pan[N.snd.sel] & 2) ? "L" : "").arg((N.snd.pan[N.snd.sel] & 1) ? "R" : "")})});
	list->put(g, "dac", {"", "DAC A B C D", QString("%0 %1 %2 %3").arg(hx(N.snd.dac[0]), hx(N.snd.dac[1]), hx(N.snd.dac[2]), hx(N.snd.dac[3])),
		(v & 8) ? "on" : "off"});

	g = "UART";
	double clk = nx_clk28_hz(comp);
	for (int u = 0; u < 2; u++) {
		long pr = N.uart[u].presc;
		list->put(g, QString("u%0").arg(u), {"", u ? "Pi" : "ESP", QString::number(pr), nx_join({pr ? QString("%0 baud").arg((int)(clk / pr + 0.5)) : "",
			(N.uartSel & 1) == u ? "selected" : "", N.uart[u].fifo ? QString("%0 to send").arg(N.uart[u].fifo) : ""})});
	}
	list->finish();
}

// SPRITES PAGE

#define NXS_PIC		192	// the picked sprite is drawn this big at most

xNxSprPage::xNxSprPage(QWidget* p):xEvoPage(HW_ZXNEXT, p) {
	sel = -1;
	fitted = false;
	QVBoxLayout* lay = new QVBoxLayout(this);
	lay->setContentsMargins(0, 0, 0, 0);
	cbVisible = new QCheckBox("Visible only");
	cbVisible->setChecked(true);
	lay->addWidget(cbVisible);
	list = new QTreeWidget;
	list->setObjectName("nxSprites");
	evo_tree_setup(list, {"#", "X", "Y", "Pattern", "Pal", "Scale", "Flags", "Anchor"});
	list->setSelectionMode(QAbstractItemView::SingleSelection);
	list->setFocusPolicy(Qt::StrongFocus);
	QSplitter* split = new QSplitter(Qt::Vertical);
	split->setChildrenCollapsible(false);
	split->addWidget(list);
	QWidget* bot = new QWidget;
	QHBoxLayout* blay = new QHBoxLayout(bot);
	blay->setContentsMargins(0, 0, 0, 0);
	labPic = new QLabel;
	labPic->setMinimumSize(64, 64);
	labPic->setMaximumWidth(NXS_PIC);
	labPic->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	labPic->setAlignment(Qt::AlignCenter);
	labInfo = new QLabel;
	labInfo->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	labInfo->setWordWrap(true);
	blay->addWidget(labPic);
	blay->addWidget(labInfo, 1);
	split->addWidget(bot);
	split->setStretchFactor(0, 2);
	split->setStretchFactor(1, 1);
	lay->addWidget(split, 1);
	connect(cbVisible, &QCheckBox::toggled, this, [this]() {if (conf.zx) draw(conf.zx);});
	connect(list, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* it) {
		sel = it ? it->data(0, Qt::UserRole).toInt() : -1;
		draw_pick();
	});
}

void xNxSprPage::changeEvent(QEvent* ev) {
	xEvoPage::changeEvent(ev);
	if (ev->type() == QEvent::FontChange) fitted = false;
}

void xNxSprPage::draw(Computer* comp) {
	if (list->topLevelItemCount() == 0) {
		for (int i = 0; i < 128; i++) {
			QTreeWidgetItem* it = new QTreeWidgetItem(list);
			it->setData(0, Qt::UserRole, i);
			for (int c = 0; c < 5; c++)
				it->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
		}
	}
	nxSpriteView sp[128];
	next_sprites(comp, sp);
	bool on = comp->next->nr[0x15] & 1;
	QColor dim = palette().color(QPalette::Disabled, QPalette::Text);
	for (int i = 0; i < 128; i++) {
		nxSpriteView* s = &sp[i];
		QTreeWidgetItem* it = list->topLevelItem(i);
		QStringList fl;
		if (s->bits4) fl << "4-bit";
		if (s->xm) fl << "XM";
		if (s->ym) fl << "YM";
		if (s->rot) fl << "ROT";
		QString anc = s->rel ? QString("of %0, %1").arg(s->anchor).arg(s->unified ? "unified" : "composite") : "-";
		QString pat = s->bits4 ? QString("%0%1").arg(s->pat >> 1).arg((s->pat & 1) ? "b" : "a") : QString::number(s->pat >> 1);
		QStringList cols = {QString::number(i), QString::number(s->x), QString::number(s->y), pat, QString::number(s->pal),
			QString("%0x%1").arg(1 << s->xs).arg(1 << s->ys), fl.join(" "), anc};
		bool drawn = on && s->vis;
		for (int c = 0; c < cols.size(); c++) {
			it->setText(c, cols[c]);
			if (drawn) it->setData(c, Qt::ForegroundRole, QVariant());
			else it->setForeground(c, dim);
		}
		it->setHidden(cbVisible->isChecked() && !s->vis);
	}
	if (!fitted) {
		static const QStringList widest[7] = {{"127"}, {"511"}, {"511"}, {"Pattern", "63b"}, {"Pal", "15"}, {"Scale", "8x8"}, {"4-bit XM YM ROT"}};
		for (int i = 0; i < 7; i++)
			evo_fit_column(list, i, widest[i]);
		fitted = true;
	}
	draw_pick();
}

// the pattern's index at x, y of its 16x16, 0..255, or -1 where it is transparent
static int spr_pixel(Computer* comp, const nxSpriteView* s, int x, int y) {
	unsigned char* pat = comp->next->spr.pat;
	int tr = comp->next->nr[0x4b];
	int base = (s->pat >> 1) << 8;
	if (s->bits4) {
		int b = pat[(base + ((s->pat & 1) << 7) + (y << 3) + (x >> 1)) & 0x3fff];
		int nib = (x & 1) ? (b & 15) : (b >> 4);
		return (nib == (tr & 15)) ? -1 : ((s->pal << 4) | nib);
	}
	int b = pat[(base + (y << 4) + x) & 0x3fff];
	return (b == tr) ? -1 : (((((b >> 4) + s->pal) & 15) << 4) | (b & 15));
}

// the picked sprite as the pass draws it, mirrored and rotated, unscaled; transparent as a checker
void xNxSprPage::draw_pick() {
	Computer* comp = conf.zx;
	if (!comp || (comp->hw->id != HW_ZXNEXT) || (sel < 0)) {
		labPic->clear();
		labInfo->setText(sel < 0 ? "Pick a sprite to see it" : "");
		return;
	}
	nxSpriteView sp[128];
	next_sprites(comp, sp);
	nxSpriteView* s = &sp[sel];
	int p = pal_active(comp, NXP_SPR);
	QImage img(16, 16, QImage::Format_ARGB32_Premultiplied);
	for (int dy = 0; dy < 16; dy++) {
		for (int dx = 0; dx < 16; dx++) {
			int yi = s->ym ? 15 - dy : dy;
			int xi = (s->xm ^ s->rot) ? 15 - dx : dx;
			int idx = s->rot ? spr_pixel(comp, s, yi, xi) : spr_pixel(comp, s, xi, yi);
			img.setPixel(dx, dy, (idx < 0) ? 0 : pal_dot(comp, p, idx, false));
		}
	}
	int zoom = qMax(1, qMin(qMin(labPic->width(), NXS_PIC), labPic->height()) / 16);
	QPixmap pic(16 * zoom, 16 * zoom);
	QPainter pnt(&pic);
	pnt.fillRect(pic.rect(), evo_checker(labPic));
	pnt.drawImage(pic.rect(), img);
	pnt.end();
	labPic->setPixmap(pic);
	int adr = ((s->pat >> 1) << 8) | (s->bits4 ? ((s->pat & 1) << 7) : 0);
	QString kind = s->rel ? QString("relative to sprite %0 (%1)").arg(s->anchor).arg(s->unified ? "unified" : "composite") : "an anchor";
	labInfo->setText(QString("Sprite %0, %1, %2\nAt %3,%4, scale %5x%6\nPattern %7 at #%8, %9-bit\nPalette offset %10 of %11")
		.arg(sel).arg(kind).arg(s->vis ? "visible" : "hidden")
		.arg(s->x).arg(s->y).arg(1 << s->xs).arg(1 << s->ys)
		.arg(s->pat >> 1).arg(gethexword(adr)).arg(s->bits4 ? 4 : 8)
		.arg(s->pal).arg(pal_name(p)));
}

// COPPER PAGE

xNxCopPage::xNxCopPage(QWidget* p):xEvoPage(HW_ZXNEXT, p) {
	pcRow = -1;
	filled = fitted = false;
	QVBoxLayout* lay = new QVBoxLayout(this);
	lay->setContentsMargins(0, 0, 0, 0);
	QHBoxLayout* top = new QHBoxLayout;
	labState = new QLabel;
	cbFollow = new QCheckBox("Follow PC");
	cbFollow->setChecked(true);
	top->addWidget(labState, 1);
	top->addWidget(cbFollow);
	lay->addLayout(top);
	list = new QTreeWidget;
	evo_tree_setup(list, {"Addr", "Word", "Instruction"});
	lay->addWidget(list, 1);
	connect(cbFollow, &QCheckBox::toggled, this, [this]() {if (conf.zx) draw(conf.zx);});
}

void xNxCopPage::changeEvent(QEvent* ev) {
	xEvoPage::changeEvent(ev);
	if (ev->type() == QEvent::FontChange) fitted = false;
}

// one instruction as copper.vhd runs it
static QString cop_text(int w) {
	if (w == 0) return "NOOP";
	if (w == 0xffff) return "HALT";
	if (w & 0x8000) return QString("WAIT line %0, x %1").arg(w & 0x1ff).arg(((w >> 9) & 0x3f) * 8);
	int r = (w >> 8) & 0x7f;
	const char* n = nr_name(r);
	return QString("MOVE %0%1 = %2").arg(hx(r)).arg(n ? QString(" %0").arg(n) : QString()).arg(hx(w & 0xff));
}

void xNxCopPage::draw(Computer* comp) {
	xNextState& N = *comp->next;
	static const char* const modes[4] = {"stopped", "runs from 0, loops", "runs on from where it stopped, loops", "runs from 0, again at each frame"};
	labState->setText(QString("The copper %0. PC %1, writes go to %2.").arg(modes[N.nr[0x62] >> 6]).arg(hx3(N.copPc)).arg(hx3(N.coppAdr >> 1)));
	if (!filled) {
		for (int i = 0; i < 1024; i++) {
			QTreeWidgetItem* it = new QTreeWidgetItem(list, {hx3(i), "", ""});
			it->setTextAlignment(0, Qt::AlignRight | Qt::AlignVCenter);
			words[i] = 0xaaaa;		// none shows that: every row is filled below
		}
		filled = true;
	}
	for (int i = 0; i < 1024; i++) {
		unsigned short w = (N.coppMem[i << 1] << 8) | N.coppMem[(i << 1) | 1];
		if ((w == words[i]) && list->topLevelItem(i)->text(1).size()) continue;
		words[i] = w;
		QTreeWidgetItem* it = list->topLevelItem(i);
		it->setText(1, gethexword(w));
		it->setText(2, cop_text(w));
	}
	int pc = N.copPc & 0x3ff;
	if (pc != pcRow) {
		if (pcRow >= 0) {
			QTreeWidgetItem* it = list->topLevelItem(pcRow);
			for (int c = 0; c < 3; c++) {
				it->setData(c, Qt::BackgroundRole, QVariant());
				it->setData(c, Qt::ForegroundRole, QVariant());
			}
		}
		QColor bg = conf.pal.value(DBG_PAL_CHG_BG);
		QColor fg = conf.pal.value(DBG_PAL_CHG_TXT);
		QTreeWidgetItem* it = list->topLevelItem(pc);
		for (int c = 0; c < 3; c++) {
			if (bg.isValid()) it->setBackground(c, bg);
			if (fg.isValid()) it->setForeground(c, fg);
		}
		pcRow = pc;
		if (cbFollow->isChecked()) list->scrollToItem(it, QAbstractItemView::PositionAtCenter);
	}
	if (!fitted) {
		evo_fit_column(list, 0, {"Addr", "3FF"});
		evo_fit_column(list, 1, {"FFFF"});
		fitted = true;
	}
}

// LAYERS PAGE

enum {LV_L2 = 0, LV_L2S, LV_ULA5, LV_ULA7, LV_LORES, LV_PAT8, LV_PAT4};

static const QStringList lvL2Names = {"Shown", "Resolution", "Bank", "Scroll", "Clip", "Palette", "Dot", "Address", "Index", "Colour"};
static const QStringList lvUlaNames = {"Shown", "Screen", "Scroll", "Clip", "Palette", "Dot", "Address", "Attribute", "Colour"};
static const QStringList lvLoNames = {"Shown", "Mode", "Scroll", "Clip", "Palette", "Dot", "Address", "Index", "Colour"};
static const QStringList lvPatNames = {"Patterns", "Palette", "Pattern", "Address", "Used by", "Dot", "Index", "Colour"};

xNxLayerPage::xNxLayerPage(QWidget* p):xEvoInfoPage(HW_ZXNEXT, 10, p) {
	mx = my = -1;
	view = new xNxPicView;
	setPicture(view);
	QHBoxLayout* top = new QHBoxLayout;
	cbLayer = new QComboBox;
	cbLayer->setObjectName("nxLayer");
	cbLayer->addItem("Layer 2, active bank", LV_L2);
	cbLayer->addItem("Layer 2, shadow bank", LV_L2S);
	cbLayer->addItem("ULA, bank 5", LV_ULA5);
	cbLayer->addItem("ULA, bank 7", LV_ULA7);
	cbLayer->addItem("LoRes", LV_LORES);
	cbLayer->addItem("Sprite patterns, 8 bit", LV_PAT8);
	cbLayer->addItem("Sprite patterns, 4 bit", LV_PAT4);
	cbLayer->setSizeAdjustPolicy(QComboBox::AdjustToContents);
	top->addWidget(cbLayer);
	top->addStretch(1);
	lay->insertLayout(0, top);
	labLegend = new QLabel;
	labLegend->setWordWrap(true);
	lay->addWidget(labLegend);
	view->onHover = [this](int x, int y) {
		mx = x;
		my = y;
		info();
	};
	connect(cbLayer, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {if (conf.zx && (conf.zx->hw->id == HW_ZXNEXT)) draw(conf.zx);});
	setup(LV_L2);
}

void xNxLayerPage::setup(int mode) {
	shown = mode;
	const QStringList& nl = (mode <= LV_L2S) ? lvL2Names : (mode <= LV_ULA7) ? lvUlaNames : (mode == LV_LORES) ? lvLoNames : lvPatNames;
	setNames(nl, lvL2Names + lvUlaNames + lvLoNames + lvPatNames, {"pages 10..19, bank 5", "x 255..255, y 191..191", "Layer 2 second + 15"});
	static const char* const legends[7] = {
		"The memory as it stands, unscrolled; checker: the transparent colour.",
		"The memory as it stands, unscrolled; checker: the transparent colour.",
		"Bitmap and attributes as they stand, without FLASH; checker: the transparent colour.",
		"Bitmap and attributes as they stand, without FLASH; checker: the transparent colour.",
		"The memory as it stands, unscrolled; checker: the transparent colour.",
		"With palette offset 0; outlined: the patterns visible sprites use.",
		"With palette offset 0; outlined: the patterns visible sprites use."
	};
	labLegend->setText(legends[mode]);
}

int xNxLayerPage::fitWidth(int w, int h) {
	return view->widthFor(w, h);
}

// layer 2's byte at x, y of its own layout (320 and 640 run down the columns)
static int l2_byte(Computer* comp, int bank, int res, int x, int y, int* adr) {
	int a = (res == 0) ? ((y << 8) | x) : ((x << 8) | y);
	*adr = a;
	return next_l2ram(comp, ((bank & 0x7f) << 1) + (a >> 13), a & 0x1fff);
}

static int l2_index(int b, int res, int x, int off) {
	if (res == 2) return (off << 4) | ((x & 1) ? (b & 15) : (b >> 4));
	return ((((b >> 4) + off) & 15) << 4) | (b & 15);
}

// the ULA's ink or paper index for a pixel: plain, or ULANext's
static int ula_index(Computer* comp, int attr, int pen) {
	unsigned char* nr = comp->next->nr;
	if (!(nr[0x43] & 1)) {
		int br = (attr & 0x40) ? 8 : 0;
		return pen ? ((attr & 7) | br) : (16 | ((attr >> 3) & 7) | br);
	}
	int fmt = nr[0x42];
	if (pen) return attr & fmt;
	static const int shift[8] = {1, 2, 3, 4, 5, 6, 7, 8};
	for (int k = 0; k < 7; k++)
		if (fmt == (1 << shift[k]) - 1) return 0x80 | (attr >> shift[k]);
	return -1;			// the fallback colour
}

static int ula_addr(int x, int y) {
	return ((y & 0xc0) << 5) | ((y & 7) << 8) | ((y & 0x38) << 2) | (x >> 3);
}

void xNxLayerPage::draw(Computer* comp) {
	xNextState& N = *comp->next;
	unsigned char* nr = N.nr;
	int mode = cbLayer->currentData().toInt();
	if (mode != shown) setup(mode);
	view->marks.clear();
	view->shown = QSize();
	if (mode <= LV_L2S) {
		int res = (nr[0x70] >> 4) & 3;
		if (res == 3) res = 2;
		int bank = (mode == LV_L2) ? nr[0x12] : nr[0x13];
		int w = (res == 0) ? 256 : (res == 1) ? 320 : 640;
		int h = (res == 0) ? 192 : 256;
		if (view->img.size() != QSize(w, h)) view->img = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
		if (res == 2) view->shown = QSize(320, 256);
		int p = pal_active(comp, NXP_L2);
		int off = nr[0x70] & 15;
		for (int y = 0; y < h; y++) {
			QRgb* line = (QRgb*)view->img.scanLine(y);
			for (int x = 0; x < w; x++) {
				int a;
				int b = l2_byte(comp, bank, res, (res == 2) ? x >> 1 : x, y, &a);
				line[x] = pal_dot(comp, p, l2_index(b, res, x, off), true);
			}
		}
	} else if (mode <= LV_ULA7) {
		int pg = (mode == LV_ULA5) ? 0x0a : 0x0e;
		if (view->img.size() != QSize(256, 192)) view->img = QImage(256, 192, QImage::Format_ARGB32_Premultiplied);
		int p = pal_active(comp, NXP_ULA);
		QRgb fb = nx_qrgb(nx_rgb9(nr[0x4a]));
		for (int y = 0; y < 192; y++) {
			QRgb* line = (QRgb*)view->img.scanLine(y);
			for (int x = 0; x < 256; x++) {
				int b = next_ram(comp, pg, ula_addr(x, y));
				int attr = next_ram(comp, pg, 0x1800 + ((y >> 3) << 5) + (x >> 3));
				int idx = ula_index(comp, attr, (b >> (7 - (x & 7))) & 1);
				line[x] = (idx < 0) ? fb : pal_dot(comp, p, idx, true);
			}
		}
	} else if (mode == LV_LORES) {
		if (view->img.size() != QSize(128, 96)) view->img = QImage(128, 96, QImage::Format_ARGB32_Premultiplied);
		view->shown = QSize(256, 192);
		int p = pal_active(comp, NXP_ULA);
		int off = nr[0x6a] & 15;
		bool rad = nr[0x6a] & 0x20;
		for (int y = 0; y < 96; y++) {
			QRgb* line = (QRgb*)view->img.scanLine(y);
			for (int x = 0; x < 128; x++) {
				int idx;
				if (rad) {
					int b = next_ram(comp, 0x0a, (y << 6) | (x >> 1));
					idx = (off << 4) | ((x & 1) ? (b & 15) : (b >> 4));
				} else {
					int b = next_ram(comp, 0x0a + (y >= 48), ((y % 48) << 7) | x);
					idx = ((((b >> 4) + off) & 15) << 4) | (b & 15);
				}
				line[x] = pal_dot(comp, p, idx, true);
			}
		}
	} else {
		bool b4 = (mode == LV_PAT4);
		int n = b4 ? 128 : 64;
		QSize sz(256, (n / 16) * 16);
		if (view->img.size() != sz) view->img = QImage(sz, QImage::Format_ARGB32_Premultiplied);
		int p = pal_active(comp, NXP_SPR);
		nxSpriteView one;
		memset(&one, 0, sizeof(one));
		one.bits4 = b4;
		for (int k = 0; k < n; k++) {
			one.pat = b4 ? k : (k << 1);
			int ox = (k & 15) << 4;
			int oy = (k >> 4) << 4;
			for (int y = 0; y < 16; y++) {
				QRgb* line = (QRgb*)view->img.scanLine(oy + y) + ox;
				for (int x = 0; x < 16; x++) {
					int idx = spr_pixel(comp, &one, x, y);
					line[x] = (idx < 0) ? 0 : pal_dot(comp, p, idx, false);
				}
			}
		}
		nxSpriteView sp[128];
		next_sprites(comp, sp);
		QSet<int> used;
		for (int i = 0; i < 128; i++)
			if (sp[i].vis && (sp[i].bits4 == b4)) used.insert(b4 ? sp[i].pat : (sp[i].pat >> 1));
		foreach (int k, used)
			view->marks << QRect((k & 15) << 4, (k >> 4) << 4, 16, 16);
	}
	view->update();
	fit();
	info();
}

void xNxLayerPage::info() {
	Computer* comp = conf.zx;
	if (!comp || (comp->hw->id != HW_ZXNEXT)) return;
	xNextState& N = *comp->next;
	unsigned char* nr = N.nr;
	int mode = shown;
	bool in = (mx >= 0) && (my >= 0);
	QStringList vl;
	if (mode <= LV_L2S) {
		int res = (nr[0x70] >> 4) & 3;
		if (res == 3) res = 2;
		int bank = ((mode == LV_L2) ? nr[0x12] : nr[0x13]) & 0x7f;
		int p = pal_active(comp, NXP_L2);
		bool on = (mode == LV_L2) && (next_nr_peek(comp, 0x69) & 0x80);
		static const char* const rn[3] = {"256x192, 8 bit", "320x256, 8 bit", "640x256, 4 bit"};
		vl << (on ? "yes" : "no") << rn[res] << QString("%0, pages %1..%2").arg(bank).arg(hx(bank << 1)).arg(hx((bank << 1) + ((res == 0) ? 5 : 9)))
			<< QString("x %0, y %1").arg(((nr[0x71] & 1) << 8) | nr[0x16]).arg(nr[0x17]) << clip_text(comp, 0).section(';', 0, 0)
			<< QString("%0 + %1").arg(pal_name(p)).arg(nr[0x70] & 15);
		if (in) {
			int x = (res == 2) ? mx >> 1 : mx;
			int a;
			int b = l2_byte(comp, bank, res, x, my, &a);
			int idx = l2_index(b, res, mx, nr[0x70] & 15);
			int c = N.pal[p][idx] & 0x1ff;
			vl << QString("%0, %1").arg(mx).arg(my) << QString("%0:%1").arg(hx((bank << 1) + (a >> 13))).arg(gethexword(a & 0x1fff))
				<< hx(idx) << QString("%0%1").arg(nx_rgb_text(c)).arg(((c >> 1) == nr[0x14]) ? ", transparent" : "");
		}
	} else if (mode <= LV_ULA7) {
		int scr = next_nr_peek(comp, 0x69) & 0x40 ? 7 : 5;
		bool on = !(nr[0x68] & 0x80) && (scr == ((mode == LV_ULA7) ? 7 : 5));
		vl << (on ? "yes" : "no") << QString("bank %0, page %1").arg((mode == LV_ULA7) ? 7 : 5).arg((mode == LV_ULA7) ? "0E" : "0A")
			<< QString("x %0, y %1").arg(nr[0x26]).arg(nr[0x27]) << clip_text(comp, 2).section(';', 0, 0)
			<< QString("%0%1").arg(pal_name(pal_active(comp, NXP_ULA))).arg((nr[0x43] & 1) ? ", ULANext" : "");
		if (in) {
			int pg = (mode == LV_ULA5) ? 0x0a : 0x0e;
			int a = ula_addr(mx, my);
			int attr = next_ram(comp, pg, 0x1800 + ((my >> 3) << 5) + (mx >> 3));
			int b = next_ram(comp, pg, a);
			int idx = ula_index(comp, attr, (b >> (7 - (mx & 7))) & 1);
			vl << QString("%0, %1").arg(mx).arg(my) << QString("#%0").arg(gethexword(0x4000 + a)) << hx(attr)
				<< ((idx < 0) ? QString("fallback") : QString("%0 (index %1)").arg(nx_rgb_text(N.pal[pal_active(comp, NXP_ULA)][idx])).arg(hx(idx)));
		}
	} else if (mode == LV_LORES) {
		bool rad = nr[0x6a] & 0x20;
		vl << ((nr[0x15] & 0x80) ? "yes" : "no") << (rad ? "Radastan, 128x96, 4 bit" : "128x96, 8 bit")
			<< QString("x %0, y %1").arg(nr[0x32]).arg(nr[0x33]) << clip_text(comp, 2).section(';', 0, 0)
			<< QString("%0 + %1").arg(pal_name(pal_active(comp, NXP_ULA))).arg(nr[0x6a] & 15);
		if (in) {
			int pg = rad ? 0x0a : 0x0a + (my >= 48);
			int a = rad ? ((my << 6) | (mx >> 1)) : (((my % 48) << 7) | mx);
			int b = next_ram(comp, pg, a);
			int off = nr[0x6a] & 15;
			int idx = rad ? ((off << 4) | ((mx & 1) ? (b & 15) : (b >> 4))) : (((((b >> 4) + off) & 15) << 4) | (b & 15));
			vl << QString("%0, %1").arg(mx).arg(my) << QString("#%0").arg(gethexword(((pg == 0x0b) ? 0x6000 : 0x4000) + a)) << hx(idx)
				<< nx_rgb_text(N.pal[pal_active(comp, NXP_ULA)][idx]);
		}
	} else {
		bool b4 = (mode == LV_PAT4);
		vl << (b4 ? "128 of 128 bytes" : "64 of 256 bytes") << pal_name(pal_active(comp, NXP_SPR));
		if (in) {
			int k = ((my >> 4) << 4) | (mx >> 4);
			int adr = b4 ? (k << 7) : (k << 8);
			nxSpriteView sp[128];
			next_sprites(comp, sp);
			QStringList who;
			for (int i = 0; i < 128; i++)
				if ((sp[i].bits4 == b4) && ((b4 ? sp[i].pat : (sp[i].pat >> 1)) == k)) who << QString::number(i);
			nxSpriteView one;
			memset(&one, 0, sizeof(one));
			one.bits4 = b4;
			one.pat = b4 ? k : (k << 1);
			int idx = spr_pixel(comp, &one, mx & 15, my & 15);
			vl << QString::number(k) << QString("#%0").arg(gethexword(adr)) << (who.isEmpty() ? QString("-") : who.mid(0, 8).join(" "))
				<< QString("%0, %1").arg(mx & 15).arg(my & 15) << ((idx < 0) ? QString("transparent") : hx(idx))
				<< ((idx < 0) ? QString("-") : nx_rgb_text(N.pal[pal_active(comp, NXP_SPR)][idx]));
		}
	}
	setValues(vl);
}

// TILEMAP PAGE

enum {TV_MAP = 0, TV_TILES};

static const QStringList tmMapNames = {"Shown", "Mode", "Map at", "Tiles at", "Scroll", "Clip", "Palette", "Cell", "Address", "Tile", "Attribute", "Colours", "Flip", "ULA over"};
static const QStringList tmTileNames = {"Shown", "Mode", "Map at", "Tiles at", "Scroll", "Clip", "Palette", "Tile", "Address", "Used"};

// a byte of the tilemap's memory: base in 256-byte units, bank 7 in b6 (tm_map_base)
static int tm_byte(Computer* comp, int base, int sub) {
	int a = ((((sub >> 8) + base) & 63) << 8) | (sub & 0xff);
	if (base & 0x40) return next_ram(comp, 0x0e, a & 0x1fff);
	return next_ram(comp, 0x0a + ((a >> 13) & 1), a & 0x1fff);
}

static int tm_base_of(int v) {
	return ((v >> 1) & 0x40) | (v & 0x3f);
}

// the map entry of cell c: tile and attribute
static void tm_cell(Computer* comp, int c, int* tile, int* attr, int* adr) {
	unsigned char* nr = comp->next->nr;
	int mapb = tm_base_of(nr[0x6e]);
	bool strip = nr[0x6b] & 0x20;
	int sub = strip ? c : c * 2;
	*adr = sub;
	*tile = tm_byte(comp, mapb, sub);
	*attr = strip ? nr[0x6c] : tm_byte(comp, mapb, sub + 1);
}

// tile t9's dot at x, y under attribute attr, as the tilemap's fetch makes it: the index,
// or -1 for the transparent nibble
static int tm_dot(Computer* comp, int t9, int attr, int x, int y) {
	unsigned char* nr = comp->next->nr;
	int tileb = tm_base_of(nr[0x6f]);
	if (nr[0x6b] & 0x08) {
		int b = tm_byte(comp, tileb, (t9 << 3) | y);
		return (attr & 0xfe) | (((b << x) >> 7) & 1);
	}
	int mx = ((attr >> 3) ^ (attr >> 1)) & 1;
	int ex = mx ? 7 - x : x;
	int ey = (attr & 4) ? 7 - y : y;
	int u = (attr & 2) ? ey : ex;
	int v = (attr & 2) ? ex : ey;
	int b = tm_byte(comp, tileb, (t9 << 5) | (v << 2) | (u >> 1));
	int nib = (u & 1) ? (b & 15) : (b >> 4);
	if (nib == (nr[0x4c] & 15)) return -1;
	return (attr & 0xf0) | nib;
}

xNxTilePage::xNxTilePage(QWidget* p):xEvoInfoPage(HW_ZXNEXT, qMax(tmMapNames.size(), tmTileNames.size()), p) {
	mx = my = -1;
	view = new xNxPicView;
	setPicture(view);
	QHBoxLayout* top = new QHBoxLayout;
	cbView = new QComboBox;
	cbView->setObjectName("nxTileView");
	cbView->addItem("Map", TV_MAP);
	cbView->addItem("Tiles", TV_TILES);
	cbView->setSizeAdjustPolicy(QComboBox::AdjustToContents);
	top->addWidget(cbView);
	top->addWidget(new QLabel("Palette offset"));
	sbPal = new QSpinBox;
	sbPal->setRange(0, 15);
	top->addWidget(sbPal);
	top->addStretch(1);
	lay->insertLayout(0, top);
	QLabel* legend = new QLabel("The whole map, unscrolled; checker: the transparent index.");
	legend->setWordWrap(true);
	lay->addWidget(legend);
	view->onHover = [this](int x, int y) {
		mx = x;
		my = y;
		info();
	};
	auto redraw = [this]() {if (conf.zx && (conf.zx->hw->id == HW_ZXNEXT)) draw(conf.zx);};
	connect(cbView, QOverload<int>::of(&QComboBox::currentIndexChanged), this, redraw);
	connect(sbPal, QOverload<int>::of(&QSpinBox::valueChanged), this, redraw);
	setup(TV_MAP);
}

void xNxTilePage::setup(int mode) {
	shown = mode;
	setNames((mode == TV_TILES) ? tmTileNames : tmMapNames, tmMapNames + tmTileNames, {"bank 5, #7F00", "40x32, 512 tiles, text", "x 255..255, y 255..255"});
}

int xNxTilePage::fitWidth(int w, int h) {
	return view->widthFor(w, h);
}

void xNxTilePage::draw(Computer* comp) {
	unsigned char* nr = comp->next->nr;
	int mode = cbView->currentData().toInt();
	if (mode != shown) setup(mode);
	sbPal->setEnabled(mode == TV_TILES);
	int p = pal_active(comp, NXP_TM);
	bool text = nr[0x6b] & 0x08;
	bool m512 = nr[0x6b] & 0x02;
	view->marks.clear();
	if (mode == TV_MAP) {
		int cols = (nr[0x6b] & 0x40) ? 80 : 40;
		QSize sz(cols * 8, 256);
		if (view->img.size() != sz) view->img = QImage(sz, QImage::Format_ARGB32_Premultiplied);
		view->shown = QSize(320, 256);
		for (int cy = 0; cy < 32; cy++) {
			for (int cx = 0; cx < cols; cx++) {
				int tile, attr, adr;
				tm_cell(comp, cy * cols + cx, &tile, &attr, &adr);
				int t9 = ((m512 ? attr & 1 : 0) << 8) | tile;
				for (int y = 0; y < 8; y++) {
					QRgb* line = (QRgb*)view->img.scanLine((cy << 3) + y) + (cx << 3);
					for (int x = 0; x < 8; x++) {
						int idx = tm_dot(comp, t9, attr, x, y);
						line[x] = (idx < 0) ? 0 : pal_dot(comp, p, idx, text);
					}
				}
			}
		}
		if ((mx >= 0) && (my >= 0)) view->marks << QRect(mx & ~7, my & ~7, 8, 8);
	} else {
		int n = m512 ? 512 : 256;
		QSize sz(128, (n / 16) * 8);
		if (view->img.size() != sz) view->img = QImage(sz, QImage::Format_ARGB32_Premultiplied);
		view->shown = QSize();
		int attr = sbPal->value() << 4;
		for (int t = 0; t < n; t++) {
			for (int y = 0; y < 8; y++) {
				QRgb* line = (QRgb*)view->img.scanLine(((t >> 4) << 3) + y) + ((t & 15) << 3);
				for (int x = 0; x < 8; x++) {
					int idx = tm_dot(comp, t, attr, x, y);
					line[x] = (idx < 0) ? 0 : pal_dot(comp, p, idx, text);
				}
			}
		}
	}
	view->update();
	fit();
	info();
}

void xNxTilePage::info() {
	Computer* comp = conf.zx;
	if (!comp || (comp->hw->id != HW_ZXNEXT)) return;
	unsigned char* nr = comp->next->nr;
	int mode = shown;
	int v = nr[0x6b];
	int cols = (v & 0x40) ? 80 : 40;
	QStringList vl;
	vl << ((v & 0x80) ? "yes" : "no")
		<< nx_join({(v & 0x40) ? "80x32" : "40x32", (v & 2) ? "512 tiles" : "", (v & 8) ? "text" : "", (v & 0x20) ? "no attributes" : ""})
		<< tm_base(nr[0x6e]) << tm_base(nr[0x6f])
		<< QString("x %0, y %1").arg(((nr[0x2f] & 3) << 8) | nr[0x30]).arg(nr[0x31]) << clip_text(comp, 3).section(';', 0, 0)
		<< pal_name(pal_active(comp, NXP_TM));
	bool in = (mx >= 0) && (my >= 0);
	if (mode == TV_MAP) {
		if (in) {
			int c = (my >> 3) * cols + (mx >> 3);
			int tile, attr, adr;
			tm_cell(comp, c, &tile, &attr, &adr);
			int t9 = (((v & 2) ? attr & 1 : 0) << 8) | tile;
			int mapb = tm_base_of(nr[0x6e]);
			int a = ((((adr >> 8) + mapb) & 63) << 8) | (adr & 0xff);
			QStringList fl;
			if (attr & 8) fl << "XM";
			if (attr & 4) fl << "YM";
			if (attr & 2) fl << "ROT";
			vl << QString("%0, %1").arg(mx >> 3).arg(my >> 3)
				<< QString("#%0").arg(gethexword(((mapb & 0x40) ? 0xc000 + (a & 0x1fff) : 0x4000 + (a & 0x3fff))))
				<< QString::number(t9) << hx(attr)
				<< ((v & 8) ? QString("%0, %1").arg(hx(attr & 0xfe)).arg(hx(attr | 1)) : QString("%0..%1").arg(hx(attr & 0xf0)).arg(hx(attr | 0x0f)))
				<< (fl.isEmpty() ? "-" : fl.join(" ")) << (((attr & 1) && !(v & 2)) ? "yes" : "no");
		}
		// the cell outline follows the pointer
		view->marks.clear();
		if (in) view->marks << QRect(mx & ~7, my & ~7, 8, 8);
		view->update();
	} else if (in) {
		int t = ((my >> 3) << 4) | (mx >> 3);
		int tileb = tm_base_of(nr[0x6f]);
		int sub = (v & 8) ? (t << 3) : (t << 5);
		int a = ((((sub >> 8) + tileb) & 63) << 8) | (sub & 0xff);
		int used = 0;
		for (int c = 0; c < cols * 32; c++) {
			int tile, attr, adr;
			tm_cell(comp, c, &tile, &attr, &adr);
			if (((((v & 2) ? attr & 1 : 0) << 8) | tile) == t) used++;
		}
		vl << QString::number(t) << QString("#%0").arg(gethexword(((tileb & 0x40) ? 0xc000 + (a & 0x1fff) : 0x4000 + (a & 0x3fff))))
			<< QString("%0 cells").arg(used);
	}
	setValues(vl);
}

// PALETTE PAGE

static const QStringList palNames = {"Index", "Value", "R G B", "RGB", "Priority", "Used by", "NR 43"};

xNxPalPage::xNxPalPage(QWidget* p):xEvoInfoPage(HW_ZXNEXT, palNames.size(), p) {
	hov = -1;
	view = new xEvoPalView;
	setPicture(view);
	QHBoxLayout* top = new QHBoxLayout;
	cbPal = new QComboBox;
	cbPal->setObjectName("nxPal");
	static const int order[8] = {0, 4, 1, 5, 2, 6, 3, 7};
	for (int i = 0; i < 8; i++)
		cbPal->addItem(pal_name(order[i]), order[i]);
	cbPal->setSizeAdjustPolicy(QComboBox::AdjustToContents);
	top->addWidget(cbPal);
	top->addStretch(1);
	lay->insertLayout(0, top);
	labLegend = new QLabel("Dashed: the index NR 40 points at, where NR 43 writes.");
	labLegend->setWordWrap(true);
	lay->addWidget(labLegend);
	view->cols = view->rows = 16;
	for (int i = 0; i < 16; i++) {
		view->rowNames << hx(i << 4);
		view->colNames << QString::number(i, 16).toUpper();
	}
	view->onHover = [this](int idx) {
		hov = idx;
		info();
	};
	view->onPick = [this](int idx) {
		view->pick = idx;
		view->update();
		info();
	};
	connect(cbPal, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {if (conf.zx && (conf.zx->hw->id == HW_ZXNEXT)) draw(conf.zx);});
	setup(0);
}

void xNxPalPage::setup(int what) {
	shown = what;
	setNames(palNames, palNames, {"#FFFFFF", "Layer 2 second", "1FF"});
}

int xNxPalPage::fitWidth(int w, int h) {
	return view->widthFor(w, h);
}

void xNxPalPage::draw(Computer* comp) {
	xNextState& N = *comp->next;
	int p = cbPal->currentData().toInt();
	// which palettes are in use, written on the list
	for (int i = 0; i < cbPal->count(); i++) {
		int q = cbPal->itemData(i).toInt();
		QString t = pal_name(q);
		if (pal_active(comp, q & 3) == q) t += " (in use)";
		if (cbPal->itemText(i) != t) cbPal->setItemText(i, t);
	}
	view->cells.clear();
	for (int i = 0; i < 256; i++)
		view->cells << nx_qrgb(N.pal[p][i]);
	view->border = (((N.nr[0x43] >> 4) & 7) == p) ? N.nr[0x40] : -1;
	view->update();
	info();
}

void xNxPalPage::info() {
	Computer* comp = conf.zx;
	if (!comp || (comp->hw->id != HW_ZXNEXT)) return;
	xNextState& N = *comp->next;
	int p = cbPal->currentData().toInt();
	int i = (hov >= 0) ? hov : view->pick;
	QStringList vl;
	if ((i >= 0) && (i < 256)) {
		int c = N.pal[p][i];
		int c9 = c & 0x1ff;
		QStringList use;
		if (pal_active(comp, p & 3) == p) use << palLayer[p & 3];
		if (c9 >> 1 == N.nr[0x14]) use << "transparent";
		vl << hx(i) << hx3(c9) << QString("%0 %1 %2").arg(c9 >> 6).arg((c9 >> 3) & 7).arg(c9 & 7) << nx_rgb_text(c9)
			<< (((p & 3) == NXP_L2) ? ((c & 0x400) ? "yes" : "no") : "-") << nx_join(use)
			<< ((((N.nr[0x43] >> 4) & 7) == p) && (N.nr[0x40] == i) ? "writes here" : "-");
	}
	setValues(vl);
}

// DMA PAGE

xNxDmaPage::xNxDmaPage(QWidget* p):xEvoPage(HW_ZXNEXT, p) {
	QVBoxLayout* lay = new QVBoxLayout(this);
	lay->setContentsMargins(0, 0, 0, 0);
	list = new xEvoList({"Reg", "Name", "Value", "Means"});
	list->wordsOnly();
	lay->addWidget(list, 1);
}

static QString dma_port(int adr, int io, int mode, int len) {
	static const char* const modes[4] = {"decrements", "increments", "fixed", "fixed"};
	static const char* const cyc[4] = {"4", "3", "2", "4"};
	return QString("%0, %1, %2 cycles").arg(io ? "i/o" : "memory").arg(modes[mode & 3]).arg(cyc[len & 3]);
}

void xNxDmaPage::draw(Computer* comp) {
	nxDma& D = comp->next->dma;
	list->begin();
	QString g = "Set up";
	list->put(g, "port", {D.zmode ? "0B" : "6B", "Port", "", D.zmode ? "Z80 DMA" : "zxnDMA"});
	list->put(g, "dir", {"WR0", "Direction", "", D.dirAB ? "A > B" : "B > A"});
	list->put(g, "a", {"WR0/1", "Port A", gethexword(D.startA), dma_port(D.startA, D.ioA, D.modeA, D.lenA)});
	list->put(g, "b", {"WR4/2", "Port B", gethexword(D.startB), dma_port(D.startB, D.ioB, D.modeB, D.lenB)});
	list->put(g, "len", {"WR0", "Length", gethexword(D.blen), QString("%0 bytes").arg(D.blen)});
	static const char* const r4[4] = {"byte", "continuous", "burst", "?"};
	list->put(g, "mode", {"WR4/5", "Mode", "", nx_join({r4[D.r4mode & 3], D.restart ? "restarts" : "", D.ceWait ? "CE/WAIT" : ""})});
	list->put(g, "pre", {"WR2", "Prescaler", hx(D.presc), D.presc ? QString::number(D.presc) : "none"});
	g = "State";
	list->put(g, "run", {"", "Running", "", (D.seq != 0) ? (D.busak ? "yes, holds the bus" : "yes, asks for the bus") : "no"});
	list->put(g, "st", {"RR0", "Status", "", nx_join({D.T ? "a byte moved" : "", D.E ? "" : "block ended"}, "-")});
	list->put(g, "src", {"", "Source", gethexword(D.src), ""});
	list->put(g, "dst", {"", "Destination", gethexword(D.dst), ""});
	list->put(g, "cnt", {"", "Counter", gethexword(D.counter), ""});
	list->put(g, "wr", {"", "Writes", "", D.wrSeq ? "waiting for the rest of a register" : "-"});
	list->finish();
}

// THE PAGES

QList<xEvoPage*> next_panel_pages(QStringList* names) {
	*names = QStringList({"Registers", "Sprites", "Copper", "Layers", "Tilemap", "Palette", "DMA"});
	return QList<xEvoPage*>({new xNxRegsPage, new xNxSprPage, new xNxCopPage, new xNxLayerPage, new xNxTilePage,
		new xNxPalPage, new xNxDmaPage});
}
