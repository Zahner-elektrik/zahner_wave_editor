// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

namespace zwe {

// Where the file dialogs start. A QFileDialog given an empty or relative start
// path resolves it against the working directory, which for an editor started
// by Zahner Lab is the installation folder - not writable for normal users
// and never where their files are (F-297). Every dialog therefore goes through
// startPath(), which only ever hands out absolute paths into folders that
// exist.
namespace filedialogpaths {

// The QSettings keys of the dialog kinds. Open and Save As share one key: a
// document saved somewhere is what the next Open is most likely looking for.
inline constexpr auto DocumentKey  = "fileDialogs/documentPath";
inline constexpr auto ImportCsvKey = "fileDialogs/importCsvPath";
inline constexpr auto ExportKey    = "fileDialogs/exportCsvPath";

// The folder a dialog of the given kind starts in: the folder that kind was
// last used with, else the folder any file dialog was last used with, else the
// user's Documents folder (the home folder where there is none). A remembered
// folder that no longer exists is skipped. Always absolute.
QString startDirectory(const char* key);

// The path to hand to a file dialog of the given kind. suggestedFile - a full
// path, a bare file name or empty - is kept as it is when it is absolute and
// its folder exists; otherwise only its file name is kept and placed in
// startDirectory(key). Always absolute.
QString startPath(const char* key, const QString& suggestedFile = {});

// Remembers the folder of filePath for the given kind and as the folder the
// file dialogs were last used with. Call it with every file the user picked,
// opened or saved.
void remember(const char* key, const QString& filePath);

}  // namespace filedialogpaths

}  // namespace zwe
