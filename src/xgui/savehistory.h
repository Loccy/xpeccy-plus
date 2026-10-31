#pragma once

#include <QString>
#include <QWidget>

// File > Save History: every game with saves, the old ones cleared by age, and
// one game's saves to load or delete. current: the folder of the game in use,
// picked first. Returns the save to load, empty for none.
QString save_history(QWidget* parent, const QString& current);
