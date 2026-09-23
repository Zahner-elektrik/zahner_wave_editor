// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

namespace zwe::editonly {

// Name of the local socket an --edit-only instance listens on, derived from the
// document it is bound to. The application that started the editor derives the
// same name from the same path to ask for the window, so this derivation is
// part of the interface and must not change silently.
QString socketName(const QString& filePath);

// Asks an --edit-only instance already bound to filePath to bring its window to
// the front, and reports whether one answered. A second invocation on the same
// document uses this to hand over and exit instead of opening a rival window on
// the same file.
bool requestWindow(const QString& filePath);

}  // namespace zwe::editonly
