// the heat map panel, detached

#include <QApplication>
#include <QCloseEvent>
#include <QMenu>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QVBoxLayout>

#include <cmath>

#include "heatwin.h"
#include "xgui.h"

#define HEAT_PAINT_MS	2.0	// what one frame's picture may cost before frames are skipped
#define HEAT_SKIP_MAX	5	// at most this many frames a picture: ten a second at 50

xHeatWin::xHeatWin(QWidget* p):QDialog(p) {
	setWindowTitle("Heat Map");
	setWindowIcon(QIcon(":/images/memory.png"));
	setSizeGripEnabled(true);
	lastfrm = 0;
	QVBoxLayout* lay = new QVBoxLayout(this);
	lay->setContentsMargins(3, 3, 3, 3);
	panel = new xHeatPanel;
	lay->addWidget(panel);
	updateStyle();
	connect(panel, &xHeatPanel::s_detach, this, &xHeatWin::s_detach);
}

// the debugger hands its own font out child by child; this window is not one
// of its children, so it takes the font here
void xHeatWin::updateStyle() {
	QFont uiFont = QApplication::font();
	foreach (QWidget* wid, findChildren<QWidget*>())
		wid->setFont(qobject_cast<QMenu*>(wid) ? uiFont : conf.dbg.font);
	setFont(conf.dbg.font);
	panel->draw();
}

void xHeatWin::setDetached(bool on) {
	if (on) {
		// geometry(), not move/resize: the same rectangle both ways, where
		// pos()/move() would walk the window down the screen by the height
		// of its own title bar on every restore
		if ((conf.dbg.heatpos.x() >= 0) && (conf.dbg.heatpos.y() >= 0)) {
			setGeometry(QRect(conf.dbg.heatpos, conf.dbg.heatsiz));
		} else {
			resize(conf.dbg.heatsiz);
		}
		show();
		raise();
		activateWindow();
		panel->draw();
	} else {
		hide();
	}
}

// One refresh per emulated frame while painting stays cheap; a big picture
// (the pages of a 4 MB machine at full size) takes as many frames as its cost
// asks for, up to five. Fast mode makes ten times as many frames and no more
// to see in each, so there it takes every tenth. A machine the debugger holds
// makes no frames: what it sends is drawn as it comes.
void xHeatWin::upd() {
	if (!isVisible() || isMinimized()) return;
	if (!conf.emu.pause) {
		int every = (int)ceil(panel->paintMs() / HEAT_PAINT_MS);
		if (every < 1) every = 1;
		if (every > HEAT_SKIP_MAX) every = HEAT_SKIP_MAX;
		if (conf.emu.fast) every = XSCR_FAST_EVERY;
		if ((conf.vid.fcount - lastfrm) < every) return;
	}
	lastfrm = conf.vid.fcount;
	panel->draw();
}

// where the window is left is where it comes back
void xHeatWin::moveEvent(QMoveEvent* ev) {
	QDialog::moveEvent(ev);
	if (!isVisible()) return;
	conf.dbg.heatpos = geometry().topLeft();
}

void xHeatWin::resizeEvent(QResizeEvent* ev) {
	QDialog::resizeEvent(ev);
	if (!isVisible()) return;
	conf.dbg.heatsiz = geometry().size();
}

// Escape on a dialog is reject(), which hides the window on its own without a
// close event: the debugger would go on thinking the panel is detached and the
// next toggle would do nothing at all. Send it the same road as the X button.
void xHeatWin::reject() {
	close();
}

// closing it is another way of saying "put it back in the debugger"
void xHeatWin::closeEvent(QCloseEvent* ev) {
	emit s_detach(false);
	ev->accept();
}
