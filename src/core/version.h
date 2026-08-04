// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>

namespace zwe
{

// Version of the application, not of the .zwj file format (see zwjio.h). The
// two are versioned independently.
//
// Both strings come from the repository's git tags at configure time; see
// scripts/cmake/version.cmake.

// Numeric release version, e.g. "26.32.0" for the tag v26.32.0. Use this
// wherever a version has to be comparable or parseable.
QString applicationVersion();

// The build this binary was made from: "v26.32.0" for a build off a release tag,
// "v26.32.0-13-gabc1234" or "v26.32.0-13-gabc1234-dirty" for anything else, and
// the bare commit hash in a repository without tags. Use this wherever a build
// has to be identifiable, e.g. in a bug report.
QString applicationVersionFull();

}  // namespace zwe
