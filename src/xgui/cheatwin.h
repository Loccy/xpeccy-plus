#pragma once

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>

#include "xgui.h"

// Cheats: the trainers of a .pok and the pokes made by hand, ticked on and off
// live. What is in it is the core's (xcore/cheats); the window shows it and asks.
class xCheatWin : public QDialog {
	public:
		xCheatWin(QWidget* = NULL);
		void showWindow();
		void upd();			// once a frame: shown again when the core says it changed
		bool openFile(const QString&);	// a message box when the file does not read
	protected:
		bool eventFilter(QObject*, QEvent*);
		void moveEvent(QMoveEvent*);
		void resizeEvent(QResizeEvent*);
		void showEvent(QShowEvent*);
	private:
		xElideLabel* labFile;
		QPushButton* btnClear;
		QTreeWidget* tree;
		QLabel* labNote;
		QLineEdit* adrDec;
		QLineEdit* adrHex;
		QLineEdit* valDec;
		QLineEdit* valHex;
		QPushButton* btnPoke;
		QList<QWidget*> decSide;
		QList<QWidget*> hexSide;
		int serial;
		bool rzx;
		unsigned labGen;		// the labels in effect when filled: a label poke's place moves with them
		void fill();
		void onClick(QTreeWidgetItem*, int);
		bool askValue(int idx, const QString& name);
		void pokeByHand();
		void tie(QLineEdit* dec, QLineEdit* hex, int max, int digits);
		int fieldValue(QLineEdit* dec, int max);
		void dimSide(bool hex);		// the hex row dimmed, or the decimal one
		void note(const QString&);
};
