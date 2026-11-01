#pragma once

#include "dbg_widgets.h"
#include "ui_form_heat.h"

#include <QImage>
#include <QRgb>

// bytes per raster row: one 16K page is then 64 rows, the 64K cpu space 256
#define HEAT_BPR	256

// block view: the 64K cpu space as square 16K slots side by side - or, on a
// machine that pages its RAM by 8K (the Next), as eight 8K slots half as wide.
// one cell per group of bytes, 64 cells down every block
#define HEAT_GROUP	4	// bytes one cell stands for
#define HEAT_BLKH	64	// cells down one block

static_assert(64 * HEAT_BLKH * HEAT_GROUP == MEM_16K, "a 16K block must be exactly one 16K slot");

// what the picture covers: the cpu space, the column of pages, or one page of
// an area - the area is then the view itself (CELL_*, all above 0)
enum {
	HEATV_CPU = -1,
	HEATV_PAGES = -2
};

// which counter the view shows, and how
enum {
	HEATCH_ALL = 0,		// all three at once, each cell taking the strongest
	HEATCH_MIX,		// all three, each counter that is not 0 lighting its own color
	HEATCH_COUNT,		// all three, each lighting its color by how big it is
	HEATCH_RD,
	HEATCH_WR,
	HEATCH_EX
};

// the biggest count of each counter in a view: "by count" scales against it
typedef xHeatCell xHeatMax;

// one strip of the pages view
struct xHeatStrip {
	int area;
	int page;
	xHeatTotal tot;
};

// The heat picture. Painted, not laid out: one byte (or one group of bytes)
// is a square of 1..8 px, sized so that the whole source fits the panel when
// it can, and scrolled by whole rows - whole strips in the pages view - when
// it cannot. The cpu view uses the block layout, which always fits.
class xHeatView : public QWidget {
	Q_OBJECT
	public:
		xHeatView(QWidget* = nullptr);
		void setSource(int view, int page);
		void setChannel(int);
		void setUntouched(bool);
		void setTop(int);
		int getTop() const;
		int rowsTotal() const;		// rows the whole source takes
		int rowsFit() const;		// rows the panel shows now
		int maxTop() const;		// last row the panel can start at
		bool blockView() const;		// cpu space drawn as square blocks
		bool fits(int extra) const;	// nothing to scroll, given that much more width
		xHeatMax lastMax() const;	// the counters' tops in what was painted last
		double lastPaintMs() const;	// what the last paint cost
		QSize minimumSizeHint() const;
	signals:
		void s_adr(int);		// double click on a cell visible to the cpu
		void s_cell(QString, xHeatCell);	// address and counts under the cursor
		void s_sums(QString, xHeatTotal);	// ...or a page's sums, the cursor on its heading
		void s_geom();			// size changed: the scrollbar range has to follow
		void s_scroll(int);		// wheel: rows to move by
		void s_open(int area, int page);	// a strip of the pages view clicked
		void s_painted();		// lastMax() has moved on
	protected:
		void paintEvent(QPaintEvent*);
		void resizeEvent(QResizeEvent*);
		void mouseMoveEvent(QMouseEvent*);
		void mousePressEvent(QMouseEvent*);
		void mouseDoubleClickEvent(QMouseEvent*);
		void leaveEvent(QEvent*);
		void wheelEvent(QWheelEvent*);
	private:
		int view;			// HEATV_* or the area (CELL_*)
		int page;			// page of the area
		int chan;
		bool untouched;			// the pages view lists every page
		int top;			// first row shown
		xHeatMax vmax;
		double paintMs;
		// the block layout, worked out in one go: every piece of it comes
		// from the cell size
		struct xBlkGeom {
			QRect all;		// the cells of all blocks, headings not counted
			int cs;			// side of one cell
			int hh;			// height of a heading
			int span;		// from one block's left edge to the next
			int blocks;		// how many: 4 of 16K, 8 of 8K
			int bw;			// cells across one block
		};
		// the pages view's layout, the same way
		struct xStripGeom {
			int cs;			// side of one cell
			int hh;			// height of a strip's heading
			int left;		// where the cells start
		};
		int unit() const;		// page size of the area shown
		int cellWFor(int wid) const;
		int cellHFor(int cw) const;
		int rowsFitFor(int ch) const;
		int cellAt(const QPoint&) const;	// offset inside the source, -1 outside
		xHeatCell cellData(int off) const;
		void rowSource(int row, int* type, int* base) const;
		QString cellName(int type, int abs, int withOfs) const;
		int blkHead() const;			// height of a block's heading
		xBlkGeom blkGeom() const;
		int blkAt(const QPoint&) const;		// cpu address of the group under the point
		QString blkName(int blk, int bytes) const;
		void paintBlocks(QPainter&, QRgb none, const QColor& gnd);
		void paintRaster(QPainter&, QRgb none, const QColor& gnd);
		QList<xHeatStrip> strips() const;
		int stripRows(int area) const;		// cell rows of one page's strip
		int stripHeight(int area, const xStripGeom&) const;
		xStripGeom stripGeom() const;
		int stripAt(const QPoint&, xHeatStrip* st, int* off) const;	// -1 none, 0 heading, 1 cells
		void paintStrips(QPainter&, QRgb none, const QColor& gnd);
		void paintGrid(QPainter&, const QRect&, int cw, int ch, const QColor& gnd);
		void paintOffHint(QPainter&, const QRect&) const;
};

// The legend under the picture, which doubles as the readout: one swatch per
// counter the view is showing, each with the count of the cell under the
// cursor. Above it a line says what the mixed modes' colors mean, in the same
// columns; it is there in every mode, empty or not, so the picture does not
// move when the mode changes. Painted, and with no signals of its own, so it
// needs no moc.
class xHeatLegend : public QWidget {
	public:
		xHeatLegend(QWidget* = nullptr);
		void setChannel(int);
		void setCounts(xHeatCell);
		void setSums(xHeatTotal);
		void setMax(xHeatMax);
		QSize sizeHint() const;
		QSize minimumSizeHint() const;
	protected:
		void paintEvent(QPaintEvent*);
	private:
		// one swatch: its color, its name, and the count it stands for -
		// NULL for one with no count. a ramp is drawn dim to bright
		struct xHeatLegItem { QRgb col; QString lab; const unsigned long long* val; bool ramp; };
		int chan;
		unsigned long long cnt[3];	// what the cursor is over: read, write, exec
		bool valid;
		bool sums;			// a page's sums, which do not stop at a cell's limit
		unsigned long long top[3];	// the view's biggest cell counts
		QList<xHeatLegItem> build() const;
		QList<xHeatLegItem> build2() const;	// the mixes' line
		int numWidth() const;		// the fixed field every count is drawn in
		int itemWidth(const xHeatLegItem&) const;
		QList<int> columns() const;	// where the legend's items start
		QString number(const xHeatLegItem&) const;
		void paintItem(QPainter&, const xHeatLegItem&, int x, int y0, int h);
};

// The whole panel: the controls, the picture and the legend. Lives in the
// debugger dock and, when detached, in a window of its own - one instance
// each.
class xHeatPanel : public QWidget {
	Q_OBJECT
	public:
		xHeatPanel(QWidget* = nullptr);
		double paintMs() const;
	signals:
		void s_adr(int);
		void s_detach(bool);
	public slots:
		void draw();
	private slots:
		void src_changed();
		void opts_changed();
		void show_cell(QString, xHeatCell);
		void show_sums(QString, xHeatTotal);
		void collect_toggle(bool);
		void counters_reset();
		void counters_export();
		void scroll_by(int);
		void sync_scroll();
		void open_page(int area, int page);
	private:
		Ui::HeatWidget ui;
		xHeatView* view;
		xHeatLegend* legend;
		QString areaSig;		// the areas the view box was filled for
		void fill_views();
		int pageMax() const;
};

class xHeatWidget : public xDockWidget {
	Q_OBJECT
	public:
		xHeatWidget(QString, QString, QWidget* = nullptr);
		void setDetached(bool);
	signals:
		void s_adr(int);
		void s_detach(bool);
	public slots:
		void draw();
	private:
		xHeatPanel* panel;
		QWidget* gone;		// what the dock shows while the window has the panel
};
