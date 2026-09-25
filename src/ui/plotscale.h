// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QColor>
#include <QString>
#include <vector>

// Axis helpers shared by the plots. The waveform canvas and the spectrum below it
// space and label their scales the same way, which is what makes the two read as
// parts of one view rather than as two unrelated charts.
namespace zwe::plotscale {

// A round grid step - 1, 2 or 5 times a power of ten - that divides span into
// about desiredTicks intervals. 1.0 for an empty span or no ticks, so a caller
// never has to guard against a zero step in its tick loop.
double niceStep(double span, int desiredTicks);

// value with an SI prefix from atto to giga and three significant digits, e.g.
// "2.5 ms". An empty unit leaves the prefix alone. Anything closer to zero than
// double's epsilon is written as zero, so a grid line that accumulated a rounding
// error of 1e-17 still reads as the zero it stands for.
QString siLabel(double value, const QString& unit);
// siLabel() without the rounding to zero, for values that are exact - the ticks
// of a logarithmic scale, which may well be 1e-17 without meaning zero.
QString exactSiLabel(double value, const QString& unit);

QColor translucent(QColor color, int alpha);

// One tick of a logarithmic scale: multiple * 10^n, with multiple 1 marking the
// decade itself and 2..9 the ticks in between.
struct LogTick {
    double value;
    int multiple;
};

// The ticks of a logarithmic axis between two positive bounds, ascending: every
// decade inside them and, with subdivide, the 2..9 multiples in between. A bound
// that is a tick itself counts as inside, within a relative tolerance - the ends
// of a fitted view come out of 10^x and are rarely exact. Empty unless
// 0 < minimum <= maximum and both are finite.
std::vector<LogTick> logTicks(double minimum, double maximum, bool subdivide = true);

}  // namespace zwe::plotscale
