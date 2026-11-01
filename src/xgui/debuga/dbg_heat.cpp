#include "dbg_heat.h"

#include <cmath>

#include <QElapsedTimer>
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QStyleOption>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFileDialog>

#define	HEAT_PAD	2	// margin around the picture
#define	HEAT_CELLMAX	8	// biggest square one byte is drawn as
#define	HEAT_GRIDMIN	3	// tile size the separator grid starts being drawn at
#define	HEAT_SWATCH	9	// legend swatch, in pixels
#define	HEAT_RAMP	27	// legend ramp of "by count", in pixels
#define	HEAT_LEGGAP	8	// between two legend entries

// one set of counter colors for every theme - they read on a light ground and
// on a dark one alike. only the grey of an untouched cell has to follow the
// theme, and the yellow between the slots is the memory map panel's own
#define	HEAT_COL_RD	qRgb(46, 207, 46)
#define	HEAT_COL_WR	qRgb(255, 57, 57)
#define	HEAT_COL_EX	qRgb(79, 150, 237)
// the mixes of "mixed": read+write, read+exec, write+exec, all three
#define	HEAT_COL_RW	qRgb(236, 204, 32)
#define	HEAT_COL_RX	qRgb(38, 204, 214)
#define	HEAT_COL_WX	qRgb(214, 84, 226)
#define	HEAT_COL_RWX	qRgb(255, 255, 255)
#define	HEAT_COL_SEP	QColor(224, 224, 0)
#define	HEAT_NONE_LT	qRgb(214, 214, 214)	// untouched, on a light ground
#define	HEAT_NONE_DK	qRgb(72, 72, 72)	// and on a dark one
#define	HEAT_COL_BOUND	QColor(0x60, 0x60, 0x60)	// 16K window boundaries
#define	HEAT_COL_HINT	QColor(0x80, 0x80, 0x80)	// "collecting is off"

#define	HEAT_LOG_FLOOR	64	// "by count": the level of a single hit, so it still shows

#define	HEAT_BLKGAP	1	// separator between two blocks, in pixels
#define	HEAT_BLKMAX	8	// biggest square one group is drawn as
#define	HEAT_HDRPAD	2	// above and below a heading
#define	HEAT_STRIPW	256	// cells across one strip of the pages view
#define	HEAT_STRIPMAX	4	// biggest square one of its cells is drawn as
#define	HEAT_STRIPGAP	4	// between two strips

// the areas, in the order the view box and the pages view list them
static const struct {int area; const char* name;} heat_areas[] = {
	{CELL_RAM, "RAM"}, {CELL_ROM, "ROM"}, {CELL_ALTROM, "ALTROM"}, {CELL_DIVROM, "DIVROM"},
	{CELL_DIV, "DIV"}, {CELL_MFROM, "MFROM"}, {CELL_MFRAM, "MFRAM"}
};

// the grey an untouched cell is drawn in, by how light the ground under the
// panel is. the style sheet reaches the palette, so that is where it comes
// from - xStackView reads its own ground the same way
static QRgb heat_none_for(const QColor& gnd) {
	return (gnd.lightness() < 128) ? HEAT_NONE_DK : HEAT_NONE_LT;
}

static bool heat_is_next(Computer* comp) {
	return comp->hw && (comp->hw->id == HW_ZXNEXT);
}

// a + b, stopping where a cell's counter stops
static unsigned int heat_add(unsigned int a, unsigned int b) {
	return (a > 0xffffffffU - b) ? 0xffffffffU : (a + b);
}

// counters of one group of bytes, summed. the page is already resolved, so the
// whole group is one bank lookup - a group never straddles a page. a sum that
// reaches the limit stays there, as the counters themselves do
static xHeatCell heat_sum(Computer* comp, int type, int base) {
	xHeatCell sum = {0, 0, 0, 0};
	xHeatCell one;
	for (int i = 0; i < HEAT_GROUP; i++) {
		one = comp_heat_phys(comp, type, base + i);
		sum.rd = heat_add(sum.rd, one.rd);
		sum.wr = heat_add(sum.wr, one.wr);
		sum.ex = heat_add(sum.ex, one.ex);
		sum.valid |= one.valid;
	}
	return sum;
}

static void heat_max_add(xHeatMax* m, const xHeatCell& c) {
	if (c.rd > m->rd) m->rd = c.rd;
	if (c.wr > m->wr) m->wr = c.wr;
	if (c.ex > m->ex) m->ex = c.ex;
}

// "by count": one over the log of each counter's top, so a count's level is
// a multiplication away
struct xHeatScale {
	float rd, wr, ex;
};

static xHeatScale heat_scale(const xHeatMax& m) {
	xHeatScale s;
	s.rd = m.rd ? 1.0f / log1pf((float)m.rd) : 0.0f;
	s.wr = m.wr ? 1.0f / log1pf((float)m.wr) : 0.0f;
	s.ex = m.ex ? 1.0f / log1pf((float)m.ex) : 0.0f;
	return s;
}

static int heat_level(unsigned int v, float inv) {
	if (!v) return 0;
	int lv = HEAT_LOG_FLOOR + (int)((255 - HEAT_LOG_FLOOR) * log1pf((float)v) * inv + 0.5f);
	return (lv > 255) ? 255 : lv;
}

// which color one cell gets. counters are summed over whatever the cell
// stands for. All: exec outweighs everything else, then write against read,
// and any read left over makes it green - the strongest one wins outright.
// Mixed: every counter that is not 0 adds its own, so read and write together
// are yellow and all three white. By count: red, green and blue are write,
// read and exec, each as bright as its count against its top in the view.
// a cell nothing touched is grey, as is one the single channel on show never
// touched
static QRgb heat_cell_color(const xHeatCell& c, int chan, QRgb none, const xHeatScale& sc) {
	static const QRgb mix[8] = {0, HEAT_COL_WR, HEAT_COL_RD, HEAT_COL_RW, HEAT_COL_EX, HEAT_COL_WX, HEAT_COL_RX, HEAT_COL_RWX};
	switch (chan) {
		case HEATCH_RD: return c.rd ? HEAT_COL_RD : none;
		case HEATCH_WR: return c.wr ? HEAT_COL_WR : none;
		case HEATCH_EX: return c.ex ? HEAT_COL_EX : none;
	}
	if (!c.rd && !c.wr && !c.ex) return none;
	switch (chan) {
		case HEATCH_MIX:
			return mix[(c.wr ? 1 : 0) | (c.rd ? 2 : 0) | (c.ex ? 4 : 0)];
		case HEATCH_COUNT:
			return qRgb(heat_level(c.wr, sc.wr), heat_level(c.rd, sc.rd), heat_level(c.ex, sc.ex));
	}
	if (c.ex > c.wr + c.rd) return HEAT_COL_EX;
	if (c.wr > c.rd) return HEAT_COL_WR;
	return HEAT_COL_RD;
}

// the readout and the legend show all three counters unless one is picked
static bool heat_shown(int chan, int which) {
	return (chan < HEATCH_RD) || (chan == which);
}

// counts above six digits lose their tail rather than the field its width
static QString heat_count(unsigned long long v) {
	if (v < 1000000ULL) return QString::number(v);
	if (v < 1000000000ULL) return QString("%0M").arg(v / 1000000.0, 0, 'f', 1);
	if (v < 1000000000000ULL) return QString("%0G").arg(v / 1000000000.0, 0, 'f', 1);
	return QString("%0G").arg(v / 1000000000ULL);
}

// VIEW

xHeatView::xHeatView(QWidget* par):QWidget(par) {
	view = HEATV_CPU;
	page = 0;
	chan = HEATCH_ALL;
	untouched = false;
	top = 0;
	vmax.rd = vmax.wr = vmax.ex = 0;
	vmax.valid = 0;
	paintMs = 0;
	setMouseTracking(true);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

// the cpu space is shown as blocks when it really is 16K or 8K slots: a
// machine with a wider bus keeps the plain raster
bool xHeatView::blockView() const {
	return (view == HEATV_CPU) && (conf.zx->mem->busmask == 0xffff);
}

xHeatMax xHeatView::lastMax() const {
	return vmax;
}

double xHeatView::lastPaintMs() const {
	return paintMs;
}

QSize xHeatView::minimumSizeHint() const {
	if (blockView()) {
		xBlkGeom g = blkGeom();
		return QSize(g.blocks * g.bw + (g.blocks - 1) * HEAT_BLKGAP + 2 * HEAT_PAD,
			HEAT_BLKH + blkHead() + 2 * HEAT_PAD);
	}
	return QSize(HEAT_BPR / 2, 64);
}

void xHeatView::setSource(int v, int pg) {
	if ((v == view) && (pg == page)) return;
	if (v != view) top = 0;
	view = v;
	page = pg;
	if (v > 0) top = 0;
	updateGeometry();		// the block view asks for a different minimum
	update();
}

void xHeatView::setChannel(int c) {
	chan = c;
	update();
}

void xHeatView::setUntouched(bool on) {
	untouched = on;
	top = 0;
	update();
	emit s_geom();
}

void xHeatView::setTop(int r) {
	int max = maxTop();
	if (r < 0) r = 0;
	if (r > max) r = max;
	if (r == top) return;
	top = r;
	update();
}

int xHeatView::getTop() const {
	return top;
}

// page size of the area shown
int xHeatView::unit() const {
	return (view > 0) ? comp_cell_unit(conf.zx, view) : MEM_16K;
}

int xHeatView::rowsTotal() const {
	if (view == HEATV_PAGES) return strips().size();
	if (view == HEATV_CPU) return (conf.zx->mem->busmask + 1) / HEAT_BPR;
	return unit() / HEAT_BPR;
}

int xHeatView::maxTop() const {
	if (blockView()) return 0;		// the blocks always fit
	if (view == HEATV_PAGES) {
		// as many strips as fit above the bottom, counted from the last one
		QList<xHeatStrip> list = strips();
		xStripGeom g = stripGeom();
		int room = height() - 2 * HEAT_PAD;
		int n = list.size();
		while (n > 0) {
			room -= stripHeight(list[n - 1].area, g);
			if (room < 0) break;
			n--;
		}
		return (n < list.size()) ? n : (list.size() ? list.size() - 1 : 0);
	}
	int max = rowsTotal() - rowsFit();
	return (max < 0) ? 0 : max;
}

// a byte is one cell wide, as wide as the panel allows: 256 of them fit in a
// dock 512 px across, and 1 px each below that. taken for a given width, not
// only the current one - see fits()
int xHeatView::cellWFor(int wid) const {
	int cw = (wid - 2 * HEAT_PAD) / HEAT_BPR;
	if (cw < 1) cw = 1;
	if (cw > HEAT_CELLMAX) cw = HEAT_CELLMAX;
	return cw;
}

// a cell may be taller than it is wide, up to 4:1: a 16K page is 64 rows and
// would otherwise sit as a thin strip in an empty panel. never shorter, and
// never taller than the source needs
int xHeatView::cellHFor(int cw) const {
	int total = rowsTotal();
	int ch = total ? ((height() - 2 * HEAT_PAD) / total) : cw;
	if (ch < cw) ch = cw;
	if (ch > cw * 4) ch = cw * 4;
	return ch;
}

int xHeatView::rowsFitFor(int ch) const {
	int rows = (height() - 2 * HEAT_PAD) / ch;
	if (rows < 1) rows = 1;
	int total = rowsTotal();
	return (rows > total) ? total : rows;
}

int xHeatView::rowsFit() const {
	if (view == HEATV_PAGES) {
		QList<xHeatStrip> list = strips();
		xStripGeom g = stripGeom();
		int room = height() - 2 * HEAT_PAD;
		int n = 0;
		for (int i = top; i < list.size(); i++) {
			room -= stripHeight(list[i].area, g);
			if (room < 0) break;
			n++;
		}
		return (n < 1) ? 1 : n;
	}
	return rowsFitFor(cellHFor(cellWFor(width())));
}

// does the whole source fit with no scrolling, measured as if the view were
// `extra` pixels wider - the width it takes back when the scrollbar goes. the
// answer has to be free of the scrollbar, or hiding one would widen the view,
// flip the answer, and set the layout swinging
bool xHeatView::fits(int extra) const {
	if (blockView()) return true;		// the blocks are laid out to fit
	if (view == HEATV_PAGES) {
		xStripGeom g = stripGeom();
		int cs = (width() + extra - 2 * HEAT_PAD) / HEAT_STRIPW;
		g.cs = (cs < 1) ? 1 : (cs > HEAT_STRIPMAX) ? HEAT_STRIPMAX : cs;
		int need = 0;
		foreach (const xHeatStrip& st, strips())
			need += stripHeight(st.area, g);
		return need <= height() - 2 * HEAT_PAD;
	}
	int ch = cellHFor(cellWFor(width() + extra));
	return ((height() - 2 * HEAT_PAD) / ch) >= rowsTotal();
}

// counters of one cell, by its offset inside the current source. a block cell
// stands for a group of bytes and answers with their sum
xHeatCell xHeatView::cellData(int off) const {
	Computer* comp = conf.zx;
	if (blockView()) {
		xAdr xadr = comp_get_xadr(comp, off, MEM_VIEW_READ);
		return heat_sum(comp, xadr.type, xadr.abs);
	}
	if (view == HEATV_CPU) return comp_heat_cpu(comp, off);
	return comp_heat_area(comp, view, page, off);
}

// where one raster row's bytes live, as a bank and an offset into it. a row
// never straddles two memory pages - mem->map is indexed by adr >> pgshift and
// pgshift is never below 8 - so one page lookup serves all 256 of them, which
// is what keeps the whole-source scan below off the page-lookup path. the same
// holds for a block row (128 or 256 bytes) and a strip row (1K of one page)
void xHeatView::rowSource(int row, int* type, int* base) const {
	Computer* comp = conf.zx;
	int off = row * HEAT_BPR;
	if (view == HEATV_CPU) {
		xAdr xadr = comp_get_xadr(comp, off, MEM_VIEW_READ);
		*type = xadr.type;
		*base = xadr.abs;
	} else if (!comp_cell_xadr(comp, view, page, off, type, base)) {
		*type = MEM_EXT;
		*base = -1;
	}
}

// the name people know a cell by - RAM5, ROM0, DIV3, DIVROM - and, asked for,
// its offset in that page. the Next's layer 2 over a BRAM page is that page
QString xHeatView::cellName(int type, int abs, int withOfs) const {
	Computer* comp = conf.zx;
	char buf[32];
	if (heat_is_next(comp)) {
		if ((type == MEM_EXT) && (next_shadow_sram(abs) >= 0)) {
			abs = next_shadow_sram(abs);
			type = MEM_RAM;
		}
	} else if (type == MEM_RAM) {
		abs &= comp->mem->ramMask;
	} else if (type == MEM_ROM) {
		abs &= comp->mem->romMask;
	}
	xCell c = comp_cell(comp, type, abs);
	if (c.area == CELL_NONE) return "---";
	QString name = (c.area == CELL_RAM) ? QString("RAM%0").arg(c.page) : QString(cell_token(c, buf));
	if (withOfs) name += QString(" #%0").arg(gethexword(c.ofs));
	return name;
}

// one cell per byte, colored the same way the block view colors a group
void xHeatView::paintRaster(QPainter& pnt, QRgb none, const QColor& gnd) {
	Computer* comp = conf.zx;
	int cw = cellWFor(width());
	int ch = cellHFor(cw);
	int rows = rowsFitFor(ch);
	QVector<xHeatCell> cells(HEAT_BPR * rows);
	int type, base, x, y;
	xHeatMax m = {0, 0, 0, 1};
	for (y = 0; y < rows; y++) {
		rowSource(top + y, &type, &base);
		for (x = 0; x < HEAT_BPR; x++) {
			xHeatCell& c = cells[y * HEAT_BPR + x];
			c = comp_heat_phys(comp, type, base + x);
			heat_max_add(&m, c);
		}
	}
	vmax = m;
	xHeatScale sc = heat_scale(m);
	QImage img(HEAT_BPR, rows, QImage::Format_RGB32);
	for (y = 0; y < rows; y++) {
		QRgb* line = (QRgb*)img.scanLine(y);
		for (x = 0; x < HEAT_BPR; x++)
			line[x] = heat_cell_color(cells[y * HEAT_BPR + x], chan, none, sc);
	}
	int wid = HEAT_BPR * cw;
	QRect dst((width() - wid) / 2, HEAT_PAD, wid, rows * ch);
	pnt.setPen(palette().color(QPalette::Mid));
	pnt.drawRect(dst.adjusted(-1, -1, 0, 0));
	pnt.drawImage(dst, img);
	paintGrid(pnt, dst, cw, ch, gnd);
	// the 16K window boundaries, the same marks the memory map panel draws
	if (view == HEATV_CPU) {
		pnt.setPen(HEAT_COL_BOUND);
		int step = MEM_16K / HEAT_BPR;
		for (int r = top + step - (top % step); r < top + rows; r += step) {
			int yy = HEAT_PAD + (r - top) * ch - 1;
			pnt.drawLine(dst.left(), yy, dst.right(), yy);
		}
	}
	paintOffHint(pnt, dst);
}

// once a tile is big enough to have an inside, leave a line of the style's
// own ground along its right and bottom: that is the grid
void xHeatView::paintGrid(QPainter& pnt, const QRect& dst, int cw, int ch, const QColor& gnd) {
	if ((cw < HEAT_GRIDMIN) || (ch < HEAT_GRIDMIN)) return;
	pnt.setPen(gnd);
	for (int x = dst.left() + cw - 1; x <= dst.right(); x += cw)
		pnt.drawLine(x, dst.top(), x, dst.bottom());
	for (int y = dst.top() + ch - 1; y <= dst.bottom(); y += ch)
		pnt.drawLine(dst.left(), y, dst.right(), y);
}

// BLOCK VIEW

// the heading of a block: where it starts and the page in it. a 16K slot
// whose halves hold two pages (divMMC over the ROM) names both. a narrow block
// gets the page alone, or nothing at all rather than a clipped word
QString xHeatView::blkName(int blk, int bytes) const {
	Computer* comp = conf.zx;
	int adr = blk * bytes;
	xAdr xa = comp_get_xadr(comp, adr, MEM_VIEW_READ);
	QString name = cellName(xa.type, xa.abs, 0);
	if (bytes == MEM_16K) {
		xAdr xb = comp_get_xadr(comp, adr + MEM_8K, MEM_VIEW_READ);
		QString other = cellName(xb.type, xb.abs, 0);
		if (other != name) name += " / " + other;
	}
	return name;
}

// the heading is a strip of the same color the dock titles use
int xHeatView::blkHead() const {
	return fontMetrics().height() + 2 * HEAT_HDRPAD;
}

// blocks stay square cells, so the shorter side of the panel sets the cell,
// and everything else follows from it
xHeatView::xBlkGeom xHeatView::blkGeom() const {
	xBlkGeom g;
	// a machine that pages its RAM by 8K gets eight slots of half the width
	int k8 = comp_cell_unit(conf.zx, CELL_RAM) == MEM_8K;
	g.blocks = k8 ? 8 : 4;
	g.bw = k8 ? 32 : 64;
	g.hh = blkHead();
	int w = (width() - 2 * HEAT_PAD - (g.blocks - 1) * HEAT_BLKGAP) / (g.blocks * g.bw);
	int h = (height() - 2 * HEAT_PAD - g.hh) / HEAT_BLKH;
	g.cs = (w < h) ? w : h;
	if (g.cs < 1) g.cs = 1;
	if (g.cs > HEAT_BLKMAX) g.cs = HEAT_BLKMAX;
	g.span = g.bw * g.cs + HEAT_BLKGAP;
	int wid = g.blocks * g.span - HEAT_BLKGAP;		// no gap after the last block
	int hei = HEAT_BLKH * g.cs;
	// a panel too short to hold the whole thing loses the bottom, not the
	// headings: the cells stay readable without them, the other way round not
	int y = (height() - hei - g.hh) / 2 + g.hh;
	if (y < g.hh + HEAT_PAD) y = g.hh + HEAT_PAD;
	g.all = QRect((width() - wid) / 2, y, wid, hei);
	return g;
}

// cpu address of the group under the point, -1 outside the blocks or in a gap
int xHeatView::blkAt(const QPoint& pos) const {
	xBlkGeom g = blkGeom();
	if (!g.all.contains(pos)) return -1;
	int x = pos.x() - g.all.left();
	int blk = x / g.span;
	int inx = (x % g.span) / g.cs;
	int iny = (pos.y() - g.all.top()) / g.cs;
	if ((blk >= g.blocks) || (inx >= g.bw) || (iny >= HEAT_BLKH)) return -1;
	return (blk * g.bw * HEAT_BLKH * HEAT_GROUP) + (iny * g.bw + inx) * HEAT_GROUP;
}

void xHeatView::paintBlocks(QPainter& pnt, QRgb none, const QColor& gnd) {
	Computer* comp = conf.zx;
	xBlkGeom g = blkGeom();
	int bytes = g.bw * HEAT_BLKH * HEAT_GROUP;		// one block: 16K or 8K
	int bw = g.bw * g.cs;
	int across = g.blocks * g.bw;
	int blk, x, y, type, base;
	// the counters first: "by count" needs their tops before any color
	QVector<xHeatCell> cells(across * HEAT_BLKH);
	xHeatMax m = {0, 0, 0, 1};
	for (blk = 0; blk < g.blocks; blk++) {
		for (y = 0; y < HEAT_BLKH; y++) {
			// one block row never straddles a page, so the map is looked up
			// once for the whole of it
			xAdr xadr = comp_get_xadr(comp, blk * bytes + y * g.bw * HEAT_GROUP, MEM_VIEW_READ);
			type = xadr.type;
			base = xadr.abs;
			for (x = 0; x < g.bw; x++) {
				xHeatCell& c = cells[y * across + blk * g.bw + x];
				c = heat_sum(comp, type, base + x * HEAT_GROUP);
				heat_max_add(&m, c);
			}
		}
	}
	vmax = m;
	xHeatScale sc = heat_scale(m);
	QImage img(across, HEAT_BLKH, QImage::Format_RGB32);
	for (y = 0; y < HEAT_BLKH; y++) {
		QRgb* line = (QRgb*)img.scanLine(y);
		for (x = 0; x < across; x++)
			line[x] = heat_cell_color(cells[y * across + x], chan, none, sc);
	}
	QColor hbg = conf.pal.value("dbg.header.bg");
	QColor htx = conf.pal.value("dbg.header.txt");
	QFontMetrics fm = fontMetrics();
	QString name;
	for (blk = 0; blk < g.blocks; blk++) {
		int bx = g.all.left() + blk * g.span;
		// the heading, in the dock titles' own colors
		QRect head(bx, g.all.top() - g.hh, bw, g.hh);
		pnt.fillRect(head, hbg);
		QString page = blkName(blk, bytes);
		name = QString("#%0 %1").arg(gethexword(blk * bytes)).arg(page);
		if (fm.horizontalAdvance(name) > bw) name = page;
		if (fm.horizontalAdvance(name) <= bw) {
			pnt.setPen(htx);
			pnt.drawText(head, Qt::AlignCenter, name);
		}
		QRect dst(bx, g.all.top(), bw, g.all.height());
		pnt.drawImage(dst, img, QRect(blk * g.bw, 0, g.bw, HEAT_BLKH));
		paintGrid(pnt, dst, g.cs, g.cs, gnd);
		// two pages in one 16K slot: a line between its halves
		if ((bytes == MEM_16K) && page.contains('/')) {
			int yy = g.all.top() + (HEAT_BLKH / 2) * g.cs - 1;
			pnt.fillRect(bx, yy, bw, 1, HEAT_COL_SEP);
		}
	}
	// the yellow between the slots, headings included so it reads as one
	// line all the way down
	for (blk = 1; blk < g.blocks; blk++) {
		int bx = g.all.left() + blk * g.span;
		pnt.fillRect(bx - HEAT_BLKGAP, g.all.top() - g.hh, HEAT_BLKGAP, g.all.height() + g.hh, HEAT_COL_SEP);
	}
	// the same frame the raster gets, around the headings and the cells alike
	pnt.setPen(palette().color(QPalette::Mid));
	pnt.drawRect(g.all.left() - 1, g.all.top() - g.hh - 1, g.all.width() + 1, g.all.height() + g.hh + 1);
	paintOffHint(pnt, g.all);
}

// PAGES VIEW

// the pages a strip is shown for: every page of every area the machine has,
// or only those something touched
QList<xHeatStrip> xHeatView::strips() const {
	Computer* comp = conf.zx;
	QList<xHeatStrip> list;
	for (unsigned i = 0; i < sizeof(heat_areas) / sizeof(heat_areas[0]); i++) {
		int area = heat_areas[i].area;
		int pages = comp_cell_pages(comp, area);
		for (int pg = 0; pg < pages; pg++) {
			xHeatStrip st;
			st.area = area;
			st.page = pg;
			st.tot = comp_heat_page(comp, area, pg);
			if (!st.tot.valid) continue;
			if (!untouched && !st.tot.rd && !st.tot.wr && !st.tot.ex) continue;
			list.append(st);
		}
	}
	return list;
}

// a strip is one page at four bytes a cell, 256 across: 16 rows for 16K, 8 for 8K
int xHeatView::stripRows(int area) const {
	return comp_cell_unit(conf.zx, area) / (HEAT_STRIPW * HEAT_GROUP);
}

int xHeatView::stripHeight(int area, const xStripGeom& g) const {
	return g.hh + stripRows(area) * g.cs + HEAT_STRIPGAP;
}

xHeatView::xStripGeom xHeatView::stripGeom() const {
	xStripGeom g;
	g.cs = (width() - 2 * HEAT_PAD) / HEAT_STRIPW;
	if (g.cs < 1) g.cs = 1;
	if (g.cs > HEAT_STRIPMAX) g.cs = HEAT_STRIPMAX;
	g.hh = blkHead();
	g.left = (width() - HEAT_STRIPW * g.cs) / 2;
	if (g.left < HEAT_PAD) g.left = HEAT_PAD;
	return g;
}

// the strip under the point: 0 on its heading, 1 on its cells (with the
// offset in the page), -1 off them all
int xHeatView::stripAt(const QPoint& pos, xHeatStrip* res, int* off) const {
	QList<xHeatStrip> list = strips();
	xStripGeom g = stripGeom();
	int y = HEAT_PAD;
	if ((pos.x() < g.left) || (pos.x() >= g.left + HEAT_STRIPW * g.cs)) return -1;
	for (int i = top; (i < list.size()) && (y < height()); i++) {
		const xHeatStrip& st = list[i];
		int rows = stripRows(st.area);
		if ((pos.y() >= y) && (pos.y() < y + g.hh)) {
			*res = st;
			return 0;
		}
		int cy = y + g.hh;
		if ((pos.y() >= cy) && (pos.y() < cy + rows * g.cs)) {
			*res = st;
			*off = (((pos.y() - cy) / g.cs) * HEAT_STRIPW + (pos.x() - g.left) / g.cs) * HEAT_GROUP;
			return 1;
		}
		y += stripHeight(st.area, g);
	}
	return -1;
}

void xHeatView::paintStrips(QPainter& pnt, QRgb none, const QColor& gnd) {
	Computer* comp = conf.zx;
	QList<xHeatStrip> list = strips();
	xStripGeom g = stripGeom();
	int wid = HEAT_STRIPW * g.cs;
	// the strips that are on screen, and their counters: "by count" scales
	// against the tops of what is shown
	QList<int> shown;
	int y = HEAT_PAD;
	for (int i = top; (i < list.size()) && (y < height()); i++) {
		shown.append(i);
		y += stripHeight(list[i].area, g);
	}
	QList<QVector<xHeatCell> > cells;
	xHeatMax m = {0, 0, 0, 1};
	foreach (int i, shown) {
		const xHeatStrip& st = list[i];
		int rows = stripRows(st.area);
		QVector<xHeatCell> cv(HEAT_STRIPW * rows);
		for (int r = 0; r < rows; r++) {
			int type, base;
			if (!comp_cell_xadr(comp, st.area, st.page, r * HEAT_STRIPW * HEAT_GROUP, &type, &base)) continue;
			for (int x = 0; x < HEAT_STRIPW; x++) {
				xHeatCell& c = cv[r * HEAT_STRIPW + x];
				c = heat_sum(comp, type, base + x * HEAT_GROUP);
				heat_max_add(&m, c);
			}
		}
		cells.append(cv);
	}
	vmax = m;
	xHeatScale sc = heat_scale(m);
	QColor hbg = conf.pal.value("dbg.header.bg");
	QColor htx = conf.pal.value("dbg.header.txt");
	QFontMetrics fm = fontMetrics();
	y = HEAT_PAD;
	for (int n = 0; n < shown.size(); n++) {
		const xHeatStrip& st = list[shown[n]];
		int rows = stripRows(st.area);
		// the heading: the page's name at the left, its sums at the right,
		// each after a swatch of its counter
		QRect head(g.left, y, wid, g.hh);
		pnt.fillRect(head, hbg);
		pnt.setPen(htx);
		xCell cl = {st.area, st.page, 0, 0};
		char buf[32];
		QString name = (st.area == CELL_RAM) ? QString("RAM%0").arg(st.page) : QString(cell_token(cl, buf));
		pnt.drawText(head.adjusted(4, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, name);
		struct {QRgb col; unsigned long long val; int which;} sums[] = {
			{HEAT_COL_RD, st.tot.rd, HEATCH_RD}, {HEAT_COL_WR, st.tot.wr, HEATCH_WR}, {HEAT_COL_EX, st.tot.ex, HEATCH_EX}
		};
		int x = head.right() - 3;
		for (int k = 2; k >= 0; k--) {
			if (!heat_shown(chan, sums[k].which)) continue;
			QString num = heat_count(sums[k].val);
			int tw = fm.horizontalAdvance(num);
			x -= tw;
			pnt.drawText(QRect(x, y, tw, g.hh), Qt::AlignLeft | Qt::AlignVCenter, num);
			x -= HEAT_SWATCH + 3;
			pnt.fillRect(x, y + (g.hh - HEAT_SWATCH) / 2, HEAT_SWATCH, HEAT_SWATCH, QColor(sums[k].col));
			// framed as the legend's are: on a heading the color of a counter it would vanish
			pnt.setPen(palette().color(QPalette::Mid));
			pnt.drawRect(x, y + (g.hh - HEAT_SWATCH) / 2, HEAT_SWATCH - 1, HEAT_SWATCH - 1);
			pnt.setPen(htx);
			x -= HEAT_LEGGAP;
		}
		// the cells
		QImage img(HEAT_STRIPW, rows, QImage::Format_RGB32);
		const QVector<xHeatCell>& cv = cells[n];
		for (int r = 0; r < rows; r++) {
			QRgb* line = (QRgb*)img.scanLine(r);
			for (int c = 0; c < HEAT_STRIPW; c++)
				line[c] = heat_cell_color(cv[r * HEAT_STRIPW + c], chan, none, sc);
		}
		QRect dst(g.left, y + g.hh, wid, rows * g.cs);
		pnt.drawImage(dst, img);
		paintGrid(pnt, dst, g.cs, g.cs, gnd);
		pnt.setPen(palette().color(QPalette::Mid));
		pnt.drawRect(QRect(g.left, y, wid, g.hh + rows * g.cs).adjusted(-1, -1, 0, 0));
		y += stripHeight(st.area, g);
	}
	QRect box(g.left, HEAT_PAD, wid, height() - 2 * HEAT_PAD);
	if (list.isEmpty()) {
		pnt.setPen(HEAT_COL_HINT);
		if (conf.zx->flgHEAT) pnt.drawText(box, Qt::AlignCenter, "nothing touched yet");
	}
	paintOffHint(pnt, box);
}

// nothing is being counted: say so over whichever picture is up
void xHeatView::paintOffHint(QPainter& pnt, const QRect& box) const {
	if (conf.zx->flgHEAT) return;
	pnt.setPen(HEAT_COL_HINT);
	pnt.drawText(box, Qt::AlignCenter, "collecting is off");
}

void xHeatView::paintEvent(QPaintEvent*) {
	QElapsedTimer tm;
	tm.start();
	QStyleOption opt;
	opt.initFrom(this);
	QPainter pnt(this);
	// a custom widget gets the style sheet background only by asking for it
	style()->drawPrimitive(QStyle::PE_Widget, &opt, &pnt, this);
	// the ground the style paints: the untouched grey and the lines that part
	// one cell from the next both come out of it
	QColor gnd = opt.palette.color(QPalette::Window);
	QRgb none = heat_none_for(gnd);
	if (blockView()) {
		paintBlocks(pnt, none, gnd);
	} else if (view == HEATV_PAGES) {
		paintStrips(pnt, none, gnd);
	} else {
		paintRaster(pnt, none, gnd);
	}
	paintMs = tm.nsecsElapsed() / 1e6;
	emit s_painted();
}

void xHeatView::resizeEvent(QResizeEvent*) {
	setTop(top);		// a taller panel may have scrolled past the end
	emit s_geom();
}

// offset inside the current source under the cursor, -1 when the point is
// outside the picture
int xHeatView::cellAt(const QPoint& pos) const {
	if (blockView()) return blkAt(pos);
	int cw = cellWFor(width());
	int ch = cellHFor(cw);
	int left = (width() - HEAT_BPR * cw) / 2;
	if ((pos.x() < left) || (pos.y() < HEAT_PAD)) return -1;
	int x = (pos.x() - left) / cw;
	int y = (pos.y() - HEAT_PAD) / ch;
	if ((x < 0) || (x >= HEAT_BPR)) return -1;
	if ((y < 0) || (y >= rowsFitFor(ch))) return -1;
	return (top + y) * HEAT_BPR + x;
}

void xHeatView::mouseMoveEvent(QMouseEvent* ev) {
	static const xHeatCell nocell = {0, 0, 0, 0};
	Computer* comp = conf.zx;
	if (view == HEATV_PAGES) {
		xHeatStrip st;
		int off = 0;
		int at = stripAt(ev->pos(), &st, &off);
		if (at < 0) {
			emit s_cell(QString(), nocell);
			return;
		}
		int type, abs;
		comp_cell_xadr(comp, st.area, st.page, 0, &type, &abs);
		if (at == 0) {
			// the heading reads out the page's sums
			emit s_sums(cellName(type, abs, 0), st.tot);
			return;
		}
		comp_cell_xadr(comp, st.area, st.page, off, &type, &abs);
		emit s_cell(QString("%0-#%1").arg(cellName(type, abs, 1)).arg(gethexword((off + HEAT_GROUP - 1) & 0xffff)),
			heat_sum(comp, type, abs));
		return;
	}
	int off = cellAt(ev->pos());
	if (off < 0) {
		emit s_cell(QString(), nocell);
		return;
	}
	QString str;
	if (view == HEATV_CPU) {
		xAdr xadr = comp_get_xadr(comp, off, MEM_VIEW_READ);
		// every number in the readout carries the listing's own hash, the way
		// the block headings do. a block cell covers a group of bytes: name
		// the whole of it
		QString adr = blockView() ? QString("#%0-#%1").arg(gethexword(off)).arg(gethexword(off + HEAT_GROUP - 1))
			: QString("#%0").arg(gethexword(off));
		str = QString("%0  %1").arg(adr).arg(cellName(xadr.type, xadr.abs, 1));
	} else {
		int type, base;
		rowSource(off / HEAT_BPR, &type, &base);
		str = cellName(type, base + (off % HEAT_BPR), 1);
	}
	emit s_cell(str, cellData(off));
}

void xHeatView::leaveEvent(QEvent*) {
	static const xHeatCell nocell = {0, 0, 0, 0};
	emit s_cell(QString(), nocell);
}

// a strip clicked opens its page
void xHeatView::mousePressEvent(QMouseEvent* ev) {
	if ((view != HEATV_PAGES) || (ev->button() != Qt::LeftButton)) return;
	xHeatStrip st;
	int off;
	if (stripAt(ev->pos(), &st, &off) >= 0)
		emit s_open(st.area, st.page);
}

// jump the disassembler there, when the cell is one the cpu can see at all
void xHeatView::mouseDoubleClickEvent(QMouseEvent* ev) {
	if (view == HEATV_PAGES) return;
	int off = cellAt(ev->pos());
	if (off < 0) return;
	if (view == HEATV_CPU) {
		emit s_adr(off);
	} else {
		int type, base;
		rowSource(off / HEAT_BPR, &type, &base);
		int adr = comp_find_xadr(conf.zx, type, base + (off % HEAT_BPR), MEM_VIEW_READ);
		if (adr >= 0) emit s_adr(adr);
	}
}

void xHeatView::wheelEvent(QWheelEvent* ev) {
	if (blockView()) return;		// nothing to scroll
	int delta = ev->angleDelta().y();
	if (delta == 0) return;
	int step = (view == HEATV_PAGES) ? 1 : 4;
	emit s_scroll((delta > 0) ? -step : step);
	ev->accept();
}

// LEGEND

// a cell's counter stops here, and says so
#define HEAT_CELL_MAX	0xffffffffULL

xHeatLegend::xHeatLegend(QWidget* par):QWidget(par) {
	chan = HEATCH_ALL;
	cnt[0] = cnt[1] = cnt[2] = 0;
	top[0] = top[1] = top[2] = 0;
	valid = false;
	sums = false;
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
}

void xHeatLegend::setChannel(int c) {
	chan = c;
	updateGeometry();
	update();
}

void xHeatLegend::setCounts(xHeatCell c) {
	// every mouse move outside the picture repeats the same empty reading
	if (!sums && (valid == (c.valid != 0)) && (cnt[0] == c.rd) && (cnt[1] == c.wr) && (cnt[2] == c.ex)) return;
	cnt[0] = c.rd;
	cnt[1] = c.wr;
	cnt[2] = c.ex;
	valid = (c.valid != 0);
	sums = false;
	update();
}

void xHeatLegend::setSums(xHeatTotal t) {
	cnt[0] = t.rd;
	cnt[1] = t.wr;
	cnt[2] = t.ex;
	valid = (t.valid != 0);
	sums = true;
	update();
}

void xHeatLegend::setMax(xHeatMax m) {
	if ((m.rd == top[0]) && (m.wr == top[1]) && (m.ex == top[2])) return;
	top[0] = m.rd;
	top[1] = m.wr;
	top[2] = m.ex;
	if (chan == HEATCH_COUNT) update();
}

// the swatches, in the color a cell of that counter comes out as, and "None"
// for the gray of a cell nothing ever touched - the views' own palette, so the
// legend follows the theme with them
QList<xHeatLegend::xHeatLegItem> xHeatLegend::build() const {
	QList<xHeatLegItem> res;
	bool lit = (chan == HEATCH_COUNT);	// pure red, green and blue
	if (heat_shown(chan, HEATCH_RD)) res << xHeatLegItem{lit ? qRgb(0, 255, 0) : HEAT_COL_RD, "Read", &cnt[0], false};
	if (heat_shown(chan, HEATCH_WR)) res << xHeatLegItem{lit ? qRgb(255, 0, 0) : HEAT_COL_WR, "Write", &cnt[1], false};
	if (heat_shown(chan, HEATCH_EX)) res << xHeatLegItem{lit ? qRgb(0, 0, 255) : HEAT_COL_EX, "Exec", &cnt[2], false};
	res << xHeatLegItem{heat_none_for(palette().color(QPalette::Window)), "None", NULL, false};
	return res;
}

// what the mixed modes' colors mean, one under each column of the legend: the
// mixes, or each counter's ramp from one hit to the top count in the view
QList<xHeatLegend::xHeatLegItem> xHeatLegend::build2() const {
	QList<xHeatLegItem> res;
	if (chan == HEATCH_MIX) {
		res << xHeatLegItem{HEAT_COL_RW, "R+W", NULL, false};
		res << xHeatLegItem{HEAT_COL_RX, "R+X", NULL, false};
		res << xHeatLegItem{HEAT_COL_WX, "W+X", NULL, false};
		res << xHeatLegItem{HEAT_COL_RWX, "All", NULL, false};
	} else if (chan == HEATCH_COUNT) {
		res << xHeatLegItem{qRgb(0, 255, 0), "", &top[0], true};
		res << xHeatLegItem{qRgb(255, 0, 0), "", &top[1], true};
		res << xHeatLegItem{qRgb(0, 0, 255), "", &top[2], true};
	}
	return res;
}

// every count is drawn in one field this wide, so the labels keep their places
// whatever the numbers do
int xHeatLegend::numWidth() const {
	return fontMetrics().horizontalAdvance("999999");
}

int xHeatLegend::itemWidth(const xHeatLegItem& it) const {
	QFontMetrics fm = fontMetrics();
	int w = it.ramp ? HEAT_RAMP : HEAT_SWATCH;
	if (!it.lab.isEmpty()) w += 3 + fm.horizontalAdvance(it.val ? (it.lab + ":") : it.lab);
	if (it.val) w += 3 + numWidth();
	return w;
}

// the columns are the legend's own items; the mixes' line takes them over
QList<int> xHeatLegend::columns() const {
	QList<int> res;
	int x = 0;
	foreach (const xHeatLegItem& it, build()) {
		res << x;
		x += itemWidth(it) + HEAT_LEGGAP;
	}
	return res;
}

// a cell's count that reached the limit has stopped there: "max". a page's
// sums never stop
QString xHeatLegend::number(const xHeatLegItem& it) const {
	if (!it.ramp && !valid) return "-";
	unsigned long long v = *it.val;
	if ((it.ramp || !sums) && (v >= HEAT_CELL_MAX)) return "max";
	return heat_count(v);
}

QSize xHeatLegend::sizeHint() const {
	QFontMetrics fm = fontMetrics();
	int lh = qMax(HEAT_SWATCH, fm.height());
	QList<xHeatLegItem> one = build();
	QList<xHeatLegItem> two = build2();
	QList<int> col = columns();
	int w = col.last() + itemWidth(one.last());
	for (int i = 0; i < two.size() && i < col.size(); i++)
		w = qMax(w, col[i] + itemWidth(two[i]));
	return QSize(w, 2 * lh + 1);
}

QSize xHeatLegend::minimumSizeHint() const {
	return sizeHint();
}

void xHeatLegend::paintItem(QPainter& pnt, const xHeatLegItem& it, int x, int y0, int h) {
	QFontMetrics fm = fontMetrics();
	int y = y0 + (h - HEAT_SWATCH) / 2;
	int sw = it.ramp ? HEAT_RAMP : HEAT_SWATCH;
	if (it.ramp) {
		// the floor a single hit gets, up to the full color
		QLinearGradient grad(x, 0, x + sw, 0);
		QColor full(it.col);
		grad.setColorAt(0, QColor(full.red() * HEAT_LOG_FLOOR / 255, full.green() * HEAT_LOG_FLOOR / 255, full.blue() * HEAT_LOG_FLOOR / 255));
		grad.setColorAt(1, full);
		pnt.fillRect(x, y, sw, HEAT_SWATCH, grad);
	} else {
		pnt.fillRect(x, y, sw, HEAT_SWATCH, QColor(it.col));
	}
	pnt.setPen(palette().color(QPalette::Mid));
	pnt.drawRect(x, y, sw - 1, HEAT_SWATCH - 1);
	pnt.setPen(palette().color(QPalette::WindowText));
	x += sw;
	if (!it.lab.isEmpty()) {
		QString lab = it.val ? (it.lab + ":") : it.lab;
		int tw = fm.horizontalAdvance(lab);
		pnt.drawText(QRect(x + 3, y0, tw, h), Qt::AlignLeft | Qt::AlignVCenter, lab);
		x += 3 + tw;
	}
	// left, so the count sits against its own label rather than against the
	// next one; the field keeps its width either way
	if (it.val)
		pnt.drawText(QRect(x + 3, y0, numWidth(), h), Qt::AlignLeft | Qt::AlignVCenter, number(it));
}

void xHeatLegend::paintEvent(QPaintEvent*) {
	QPainter pnt(this);
	int lh = qMax(HEAT_SWATCH, fontMetrics().height());
	QList<int> col = columns();
	QList<xHeatLegItem> two = build2();
	for (int i = 0; i < two.size() && i < col.size(); i++)
		paintItem(pnt, two[i], col[i], 0, lh);
	QList<xHeatLegItem> one = build();
	for (int i = 0; i < one.size(); i++)
		paintItem(pnt, one[i], col[i], lh + 1, lh);
}

// PANEL

xHeatPanel::xHeatPanel(QWidget* p):QWidget(p) {
	ui.setupUi(this);
	setObjectName("HEATPANEL");

	view = new xHeatView;
	ui.layHeatView->insertWidget(0, view);		// before the scrollbar
	legend = new xHeatLegend;
	// the address has a line of its own above the legend, both against the
	// right edge: side by side the legend took the room and the address clipped
	ui.layHeatFoot->addWidget(legend, 0, Qt::AlignRight);
	ui.labHeatInfo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

	ui.cbHeatChan->addItem("All", HEATCH_ALL);
	ui.cbHeatChan->addItem("All, mixed", HEATCH_MIX);
	ui.cbHeatChan->addItem("All, by count", HEATCH_COUNT);
	ui.cbHeatChan->addItem("Read", HEATCH_RD);
	ui.cbHeatChan->addItem("Write", HEATCH_WR);
	ui.cbHeatChan->addItem("Exec", HEATCH_EX);
	// the areas come later, with the machine: the box has to grow for them, and from
	// the start it is as wide as the longest of them (DIVROM, ALTROM), so changing
	// machines hardly moves the controls beside it
	ui.cbHeatView->setSizeAdjustPolicy(QComboBox::AdjustToContents);
	ui.cbHeatView->setMinimumContentsLength(6);
	ui.cbHeatView->addItem("CPU", HEATV_CPU);
	ui.cbHeatView->addItem("Pages", HEATV_PAGES);

	connect(ui.cbHeatView, SIGNAL(currentIndexChanged(int)), this, SLOT(src_changed()));
	connect(ui.sbHeatPage, SIGNAL(valueChanged(int)), this, SLOT(src_changed()));
	connect(ui.cbHeatChan, SIGNAL(currentIndexChanged(int)), this, SLOT(opts_changed()));
	connect(ui.cbHeatUntouched, &QCheckBox::toggled, view, &xHeatView::setUntouched);
	connect(ui.cbHeatOn, &QCheckBox::toggled, this, &xHeatPanel::collect_toggle);
	connect(ui.tbHeatReset, &QToolButton::clicked, this, &xHeatPanel::counters_reset);
	connect(ui.tbHeatExport, &QToolButton::clicked, this, &xHeatPanel::counters_export);
	connect(ui.tbHeatDetach, &QToolButton::clicked, this, &xHeatPanel::s_detach);
	connect(ui.heatScroll, &QScrollBar::valueChanged, view, &xHeatView::setTop);
	connect(view, &xHeatView::s_geom, this, &xHeatPanel::sync_scroll);
	connect(view, &xHeatView::s_scroll, this, &xHeatPanel::scroll_by);
	connect(view, &xHeatView::s_cell, this, &xHeatPanel::show_cell);
	connect(view, &xHeatView::s_sums, this, &xHeatPanel::show_sums);
	connect(view, &xHeatView::s_adr, this, &xHeatPanel::s_adr);
	connect(view, &xHeatView::s_open, this, &xHeatPanel::open_page);
	connect(view, &xHeatView::s_painted, this, [this]() {legend->setMax(view->lastMax());});

	// no machine state here: the panel is built long before the debugger is
	// first shown, and the first draw() picks the rest up
	ui.sbHeatPage->setEnabled(false);		// the CPU view it starts on has no page
	ui.cbHeatUntouched->setVisible(false);
	opts_changed();
}

double xHeatPanel::paintMs() const {
	return view->lastPaintMs();
}

// the view box: CPU, Pages, then every area the machine has. filled again
// only when the machine's areas change, keeping the choice where it can
void xHeatPanel::fill_views() {
	Computer* comp = conf.zx;
	QString sig;
	for (unsigned i = 0; i < sizeof(heat_areas) / sizeof(heat_areas[0]); i++)
		sig += QString("%0:%1 ").arg(heat_areas[i].area).arg(comp_cell_pages(comp, heat_areas[i].area));
	if (sig == areaSig) return;
	areaSig = sig;
	int cur = getRFIData(ui.cbHeatView);
	ui.cbHeatView->blockSignals(true);
	while (ui.cbHeatView->count() > 2)
		ui.cbHeatView->removeItem(2);
	for (unsigned i = 0; i < sizeof(heat_areas) / sizeof(heat_areas[0]); i++) {
		if (comp_cell_pages(comp, heat_areas[i].area) > 0)
			ui.cbHeatView->addItem(heat_areas[i].name, heat_areas[i].area);
	}
	int idx = ui.cbHeatView->findData(cur);
	ui.cbHeatView->setCurrentIndex((idx < 0) ? 0 : idx);
	ui.cbHeatView->blockSignals(false);
	src_changed();
}

// pages the current machine has in the area being shown
int xHeatPanel::pageMax() const {
	int v = getRFIData(ui.cbHeatView);
	int cnt = (v > 0) ? comp_cell_pages(conf.zx, v) : 0;
	return (cnt > 0) ? (cnt - 1) : 0;
}

void xHeatPanel::src_changed() {
	int v = getRFIData(ui.cbHeatView);
	// the pages view's switch stands at the right, beside Collect, so the
	// boxes at the left keep their places whatever the view
	ui.cbHeatUntouched->setVisible(v == HEATV_PAGES);
	ui.sbHeatPage->setMaximum(pageMax());
	ui.sbHeatPage->setEnabled((v > 0) && (pageMax() > 0));
	view->setSource(v, (v > 0) ? ui.sbHeatPage->value() : 0);
	sync_scroll();
}

void xHeatPanel::opts_changed() {
	int chan = getRFIData(ui.cbHeatChan);
	view->setChannel(chan);
	legend->setChannel(chan);
}

void xHeatPanel::open_page(int area, int page) {
	int idx = ui.cbHeatView->findData(area);
	if (idx < 0) return;
	ui.cbHeatView->blockSignals(true);
	ui.cbHeatView->setCurrentIndex(idx);
	ui.cbHeatView->blockSignals(false);
	ui.sbHeatPage->blockSignals(true);
	ui.sbHeatPage->setMaximum(pageMax());
	ui.sbHeatPage->setValue(page);
	ui.sbHeatPage->blockSignals(false);
	src_changed();
}

void xHeatPanel::collect_toggle(bool on) {
	Computer* comp = conf.zx;
	comp->flgHEAT = on ? 1 : 0;
	if (on) comp_heat_sync(comp);		// banks may not match the current hardware yet
	view->update();
}

void xHeatPanel::counters_reset() {
	if (!areSure("Reset memory heat-map counters?")) return;
	comp_heat_reset(conf.zx);
	view->update();
	sync_scroll();
}

void xHeatPanel::counters_export() {
	QString path = QFileDialog::getSaveFileName(this, "Export memory heat-map", QString(), "Heat-map CSV (*.csv)", nullptr, QFileDialog::DontUseNativeDialog);
	if (path.isEmpty()) return;
	if (!path.endsWith(".csv", Qt::CaseInsensitive))
		path.append(".csv");
	if (comp_heat_save(conf.zx, path.toLocal8Bit().data()) != 0)
		shitHappens("Can't write heat-map file");
}

void xHeatPanel::scroll_by(int rows) {
	ui.heatScroll->setValue(ui.heatScroll->value() + rows);
}

void xHeatPanel::show_cell(QString adr, xHeatCell cell) {
	ui.labHeatInfo->setText(adr);
	legend->setCounts(cell);
}

void xHeatPanel::show_sums(QString adr, xHeatTotal sum) {
	ui.labHeatInfo->setText(adr);
	legend->setSums(sum);
}

// the picture shows as many rows as its height fits, so the range has to be
// worked out again after every resize and every source change. the scrollbar
// only takes room when it can actually scroll: a full-height thumb is nothing
// but noise, and the block view never scrolls at all
void xHeatPanel::sync_scroll() {
	int extra = ui.heatScroll->isVisible() ? ui.heatScroll->width() : 0;
	ui.heatScroll->setVisible(!view->fits(extra));
	ui.heatScroll->blockSignals(true);
	ui.heatScroll->setMaximum(view->maxTop());
	ui.heatScroll->setPageStep(view->rowsFit());
	ui.heatScroll->setValue(view->getTop());
	ui.heatScroll->blockSignals(false);
}

void xHeatPanel::draw() {
	Computer* comp = conf.zx;
	ui.cbHeatOn->blockSignals(true);
	ui.cbHeatOn->setChecked(comp->flgHEAT);
	ui.cbHeatOn->blockSignals(false);
	ui.tbHeatDetach->setChecked(conf.dbg.heatdetach);
	fill_views();
	ui.sbHeatPage->setMaximum(pageMax());	// may clamp the page: let that reach the view
	sync_scroll();				// the pages view grows as pages are touched
	view->update();
}

// DOCK

xHeatWidget::xHeatWidget(QString i, QString t, QWidget* p):xDockWidget(i,t,p) {
	QWidget* wid = new QWidget;
	setWidget(wid);
	setObjectName("HEATWIDGET");	// the name a saved dock layout knows it by

	QVBoxLayout* lay = new QVBoxLayout(wid);
	lay->setContentsMargins(0, 0, 0, 0);
	panel = new xHeatPanel;
	lay->addWidget(panel);

	// what the dock holds while the window has the panel: without it the
	// tab would still be there and empty, with nothing saying why
	gone = new QWidget;
	QVBoxLayout* glay = new QVBoxLayout(gone);
	QLabel* lab = new QLabel("The heat map is in a window of its own.");
	lab->setAlignment(Qt::AlignCenter);
	lab->setWordWrap(true);
	QPushButton* btn = new QPushButton("Bring it back");
	glay->addStretch(1);
	glay->addWidget(lab);
	glay->addWidget(btn, 0, Qt::AlignCenter);
	glay->addStretch(1);
	lay->addWidget(gone);
	gone->hide();

	connect(panel, &xHeatPanel::s_adr, this, &xHeatWidget::s_adr);
	connect(panel, &xHeatPanel::s_detach, this, &xHeatWidget::s_detach);
	connect(btn, &QPushButton::clicked, this, [this](){emit s_detach(false);});
}

void xHeatWidget::setDetached(bool on) {
	panel->setVisible(!on);
	gone->setVisible(on);
	if (!on) panel->draw();
}

void xHeatWidget::draw() {
	if (panel->isVisible())
		panel->draw();
}
