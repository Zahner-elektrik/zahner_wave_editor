// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QColor>
#include <QIcon>
#include <QPalette>
#include <QString>
#include <cstddef>

namespace zwe::theme {

enum class Accent { Red, Blue };

QColor accentColor(Accent accent);
QString accentName(Accent accent);
Accent accentFromName(const QString& name, Accent fallback = Accent::Red);

// The color of leaf layer number index, counted in the order of leafPaths() -
// depth first, disabled layers included. Derived from the palette's link color
// by rotating the hue, so the whole set follows the accent color and stays
// readable on either background. The canvas draws the layer's curve in it and
// the structure tree tints the layer's icons with it: that shared color is what
// connects a node in the tree to its curve in the plot.
QColor curveColor(size_t index, const QPalette& palette);

QPalette lightPalette(Accent accent = Accent::Red);
QPalette darkPalette(Accent accent = Accent::Red);
bool paletteIsDark(const QPalette& palette);
QIcon applicationIcon(bool dark);
QIcon accentIcon(const QIcon& icon, const QColor& accent);
// Every visible pixel of icon in color, its alpha kept. Unlike accentIcon() this
// looks for no accent to replace, which is what a glyph drawn in a neutral gray
// needs - there the color carries the meaning, not the shape.
QIcon tintedIcon(const QIcon& icon, const QColor& color);
void apply(bool dark, Accent accent = Accent::Red);

}  // namespace zwe::theme
