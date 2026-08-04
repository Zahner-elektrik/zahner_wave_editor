// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>
#include <QStringView>
#include <optional>

namespace zwe::engineering {

// Parses a C-locale number with an optional SI suffix. Accepted suffixes are
// f, p, n, u/µ, m, k, M and G.
std::optional<double> parse(QStringView text);

// Formats a finite value with four significant digits and an SI suffix.
QString format(double value);

}  // namespace zwe::engineering
