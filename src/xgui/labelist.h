#pragma once

#include <QAbstractListModel>
#include <QDialog>
#include "../xcore/xcore.h"

#include "ui_labelist.h"

class xLabelistModel : public QAbstractListModel {
	Q_OBJECT
	public:
		xLabelistModel(QObject* = NULL);
		QStringList list;
		bool romMode;		// the ROM labels, not a set of the user's
	public slots:
		void reset(QString = QString());
		void setCpuMode(bool);
	protected:
		QString fstr;
		bool cpuMode;
		int rowCount(const QModelIndex& = QModelIndex()) const;
		QVariant data(const QModelIndex&, int) const;
		QModelIndex index(int, int, const QModelIndex& = QModelIndex()) const;
};

class xLabeList : public QDialog {
	Q_OBJECT
	public:
		xLabeList(QWidget* = NULL);
	public slots:
		void show();
	signals:
		void labSelected(QString);
		void adrSelected(int);		// a CPU address: a ROM label's, not looked up by name
		void labSetChanged();
	private slots:
		void listDoubleClicked(QModelIndex);
		void changeLabelSet();
		void newGroup();
		void editGroup();
		void delGroup();
	private:
		Ui::LabList ui;
		xLabelistModel* mod;
		void fillSetList();
		void setRomMode(bool);
		int romIdx;		// the ROM labels' line in the set list, -1: none
};
