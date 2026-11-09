#pragma once
#include <QColor>
#include <QImage>
#include <QString>
struct EquationImage { QImage image; QString error; };
EquationImage renderEquation(const QString &source, bool display, int size, QColor color);
