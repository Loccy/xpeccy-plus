#include "dbg_stack.h"
#include "../xgui.h"
#include "xcore/xcore.h"

#include <QPainter>
#include <QStyle>
#include <QStyleOption>

#define	STACK_PAD	3	// margin around the block of lines
#define	STACK_GAP	3	// between the offset column and the word

xStackView::xStackView(QWidget* par):QWidget(par) {
}

int xStackView::rowPitch() const {
	return fontMetrics().height() + 3;
}

int xStackView::rowsFit() const {
	int one = fontMetrics().height();
	int free = height() - 2 * STACK_PAD;
	if (free < one) return 1;		// a squeezed panel still shows the top of SP
	return (free - one) / rowPitch() + 1;
}

// one line, as wide as the widest the numbers can be: an address is the widest word, and
// the +0 row's name is one with a colon. Measured as whole strings of the format, every
// digit the widest one, and a pixel to spare for each: a string at a fractional scale can
// come out wider than its characters added up.
QSize xStackView::minimumSizeHint() const {
	QFontMetrics fm = fontMetrics();
	bool dec = (conf.dbg.numfmt == CPU_NUM_DEC);
	QString digits = dec ? "0123456789" : "0123456789ABCDEF";
	QChar wide = digits[0];
	foreach (QChar c, digits)
		if (fm.horizontalAdvance(c) > fm.horizontalAdvance(wide)) wide = c;
	QString word = dbg_adr(0xFFFF);
	for (int i = 0; i < word.size(); i++)
		if (digits.contains(word[i])) word[i] = wide;
	return QSize(fm.horizontalAdvance(word + ":") + 1 + STACK_GAP + fm.horizontalAdvance(word) + 1 + 2 * STACK_PAD,
		fm.height() + 2 * STACK_PAD);
}

QSize xStackView::sizeHint() const {
	return minimumSizeHint();
}

void xStackView::setRows(const QList<xStackRow>& lst) {
	rows = lst;
	if (numfmt != conf.dbg.numfmt) {		// another width of numbers
		numfmt = conf.dbg.numfmt;
		updateGeometry();
	}
	update();
}

void xStackView::paintEvent(QPaintEvent*) {
	QStyleOption opt;
	opt.initFrom(this);
	QPainter pnt(this);
	// a custom widget gets the style sheet background only by asking for it
	style()->drawPrimitive(QStyle::PE_Widget, &opt, &pnt, this);
	if (rows.isEmpty()) return;
	QFontMetrics fm = fontMetrics();
	int nw = 0;
	int vw = 0;
	foreach (const xStackRow& row, rows) {
		nw = qMax(nw, fm.horizontalAdvance(row.name));
		vw = qMax(vw, fm.horizontalAdvance(row.value));
	}
	int gap = nw ? STACK_GAP : 0;
	// the block is centered as a whole, so the words keep one left edge
	int x = (width() - nw - gap - vw) / 2;
	if (x < STACK_PAD) x = STACK_PAD;
	int hgt = fm.height();
	int pitch = rowPitch();
	int y = STACK_PAD;
	// derived rather than taken from the palette: a plain widget gets no
	// alternate-background-color from the style sheet, that is an item view thing
	QColor tint = bg_tint(opt.palette.color(QPalette::Window));
	pnt.setPen(opt.palette.color(QPalette::WindowText));
	foreach (const xStackRow& row, rows) {
		if (row.anchor)
			pnt.fillRect(QRect(0, y, width(), hgt), tint);
		if (!row.name.isEmpty())
			pnt.drawText(QRect(x, y, nw, hgt), Qt::AlignRight | Qt::AlignVCenter, row.name);
		pnt.drawText(QRect(x + nw + gap, y, vw, hgt), Qt::AlignLeft | Qt::AlignVCenter, row.value);
		y += pitch;
	}
}
