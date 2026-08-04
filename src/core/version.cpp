// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "version.h"

// Generated from version.h.in into the build directory by the top level
// CMakeLists.txt, so a new tag or commit only needs a reconfigure, not an edit.
#include "zwe_version.h"

namespace zwe
{

QString applicationVersion()
{
    return QStringLiteral(ZWE_VERSION_STRING);
}

QString applicationVersionFull()
{
    return QStringLiteral(ZWE_VERSION_FULL_STRING);
}

}  // namespace zwe
