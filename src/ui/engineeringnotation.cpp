// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "engineeringnotation.h"

#include <QLocale>
#include <algorithm>
#include <cmath>

namespace zwe::engineering {

namespace {

std::optional<int> exponentForSuffix(QChar suffix) {
    if (suffix == u'\xB5' || suffix == u'u') {
        return -6;
    }
    switch (suffix.unicode()) {
        case 'f':
            return -15;
        case 'p':
            return -12;
        case 'n':
            return -9;
        case 'm':
            return -3;
        case 'k':
            return 3;
        case 'M':
            return 6;
        case 'G':
            return 9;
        default:
            return std::nullopt;
    }
}

QString suffixForExponent(int exponent) {
    switch (exponent) {
        case -15:
            return QStringLiteral("f");
        case -12:
            return QStringLiteral("p");
        case -9:
            return QStringLiteral("n");
        case -6:
            return QString::fromUtf8("µ");
        case -3:
            return QStringLiteral("m");
        case 3:
            return QStringLiteral("k");
        case 6:
            return QStringLiteral("M");
        case 9:
            return QStringLiteral("G");
        default:
            return {};
    }
}

}  // namespace

std::optional<double> parse(QStringView text) {
    QString number = text.trimmed().toString();
    if (number.isEmpty()) {
        return std::nullopt;
    }

    int exponent = 0;
    if (const auto suffixExponent = exponentForSuffix(number.back())) {
        exponent = *suffixExponent;
        number.chop(1);
    }
    if (number.isEmpty()) {
        return std::nullopt;
    }

    bool ok            = false;
    const double raw   = QLocale::c().toDouble(number, &ok);
    const double value = raw * std::pow(10.0, exponent);
    if (! ok || ! std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

QString format(double value) {
    if (! std::isfinite(value)) {
        return {};
    }
    if (value == 0.0) {
        return QStringLiteral("0");
    }

    const double magnitude = std::fabs(value);
    int exponent           = static_cast<int>(std::floor(std::log10(magnitude) / 3.0)) * 3;
    exponent               = std::clamp(exponent, -15, 9);
    double scaled          = value / std::pow(10.0, exponent);
    if (std::fabs(scaled) >= 999.95 && exponent < 9) {
        exponent += 3;
        scaled = value / std::pow(10.0, exponent);
    }
    return QString::number(scaled, 'g', 4) + suffixForExponent(exponent);
}

}  // namespace zwe::engineering
