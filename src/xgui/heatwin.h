#pragma once

#include <QDialog>

#include "debuga/dbg_heat.h"

// The heat map panel in a window of its own. It belongs to the main window,
// not to the debugger: the debugger hides itself the moment the machine is let
// go, and the heat map is worth watching while the machine runs.
class xHeatWin : public QDialog {
	Q_OBJECT
	public:
		xHeatWin(QWidget* = nullptr);
	signals:
		void s_detach(bool);
	public slots:
		void upd();			// one refresh per emulated frame, or fewer when painting costs
		void setDetached(bool);
		void updateStyle();
	protected:
		void reject();
		void closeEvent(QCloseEvent*);
		void moveEvent(QMoveEvent*);
		void resizeEvent(QResizeEvent*);
	private:
		xHeatPanel* panel;
		int lastfrm;		// frame the picture was drawn at
};
