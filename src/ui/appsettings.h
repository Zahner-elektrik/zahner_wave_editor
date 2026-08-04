// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>
#include <QVector>

#include "theme.h"

class QCoreApplication;

namespace zwe {

// Application-wide preferences, as opposed to the per-document settings stored
// in the .zwj file. Every setter writes to QSettings immediately, so it is the
// point of no return, and the value is read back on the next start.
// SettingsDialog is the user-facing side of this; window geometry, dock state
// and the recent files list are window state and stay with MainWindow.
namespace appsettings {

// One selectable user interface language.
struct Language {
    QString id;    // language code, matching translations/zwe_<id>.ts
    QString name;  // endonym, so the entry stays readable in any active language
};

// The languages the editor ships, English (the source language of every tr()
// string) first. Adding a translation means registering it here and in
// ZWE_TRANSLATION_LANGUAGES in CMakeLists.txt.
QVector<Language> availableLanguages();

// The endonym of languageId, or an empty string when the editor ships no
// translation for it.
QString languageName(const QString& languageId);

// The shipped language that matches the operating system locale, falling back
// to English. Never empty.
QString systemLanguage();

// The stored language preference. An empty string - the default - means
// "follow systemLanguage()", so a fresh installation follows the operating
// system and keeps following it when the system language changes. A stored id
// the editor no longer ships reads back as empty rather than leaving the
// editor without a language.
QString language();

// Stores the language preference; an empty id restores "follow the system", an
// id the editor does not ship is ignored. Only installTranslations() acts on
// it, so a change becomes visible on the next start.
void setLanguage(const QString& languageId);

// The language the user interface is actually shown in: language(), or
// systemLanguage() when that is empty. Never empty.
QString effectiveLanguage();

// Installs the translations for effectiveLanguage() on app - both this
// application's catalog and Qt's own, which carries the strings of the standard
// dialogs. Call this once during startup, before the first window is
// constructed: widgets translate their strings while they are being built, which
// is why a changed language only shows after a restart. Returns false when no
// catalog was installed - English needs none, and a missing .qm falls back to
// English.
bool installTranslations(QCoreApplication& app);

// Whether the dark color theme is used. With nothing stored yet this follows
// the palette the platform style set up, so a system in dark mode starts dark.
// Needs a QApplication instance for that fallback.
bool darkTheme();
void setDarkTheme(bool dark);

// The accent color, Zahner Red unless stored otherwise.
theme::Accent accent();
void setAccent(theme::Accent accent);

// Whether the canvas draws the legend that names every layer in the color its
// curve is drawn in. Shown unless stored otherwise: it is what tells the curves
// of a multi-layer document apart. View > Show Legend is the user-facing side.
bool showLegend();
void setShowLegend(bool visible);

// Whether canvas edits - dragging a point or a segment boundary, inserting a
// point, nudging one with the arrow keys - land on the grid instead of on the
// pixel the mouse happens to be over. Enabled unless stored otherwise: without
// it a point can only ever be placed at whatever value one pixel maps to, which
// is what makes the Points segment hard to edit. It never affects values that
// come from a file or from the property panel, and Ctrl suspends it for a
// single edit.
bool snapToGrid();
void setSnapToGrid(bool enabled);

// How fine the snap grid is: the number of snap steps one grid cell of the
// canvas is divided into. 1 snaps to the labeled grid lines themselves, 10 - the
// default - to tenths of a cell. Since the cell size is always 1, 2 or 5 times a
// power of ten, dividing by ten keeps the snapped values equally round.
// Anything outside 1..100 reads back as the default.
int snapDivisions();
void setSnapDivisions(int divisions);

// Whether the snap step is the fixed pair below instead of being derived from the
// grid. A fixed step does not follow the zoom, which is what you want when a
// document has to be laid out on, say, exact milliseconds.
bool snapFixedStep();
void setSnapFixedStep(bool fixed);

// The fixed snap step per axis, time in seconds. Only read while
// snapFixedStep() is set; a non-positive step is not stored, so the fields of
// the grid bar cannot silently switch an axis off.
double snapTimeStep();
void setSnapTimeStep(double step);
double snapValueStep();
void setSnapValueStep(double step);

}  // namespace appsettings

}  // namespace zwe
