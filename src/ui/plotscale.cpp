// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "plotscale.h"

#include <QLatin1Char>
#include <algorithm>
#include <cmath>
#include <limits>

namespace zwe::plotscale {

double niceStep(double span, int desiredTicks) {
    if (! (span > 0.0) || desiredTicks <= 0) {
        return 1.0;
    }

    const double base       = std::pow(10.0, std::floor(std::log10(span / desiredTicks)));
    const double normalized = (span / desiredTicks) / base;
    const double factor     = normalized <= 1.0   ? 1.0
                              : normalized <= 2.0 ? 2.0
                              : normalized <= 5.0 ? 5.0
                                                  : 10.0;
    return factor * base;
}

QString siLabel(double value, const QString& unit) {
    if (std::abs(value) < std::numeric_limits<double>::epsilon()) {
        return QStringLiteral("0 %1").arg(unit);
    }
    return exactSiLabel(value, unit);
}

QString exactSiLabel(double value, const QString& unit) {
    if (value == 0.0 || ! std::isfinite(value)) {
        return QStringLiteral("0 %1").arg(unit);
    }
    static constexpr const char* Prefixes[] = {"a", "f", "p", "n", "µ", "m", "", "k", "M", "G"};
    const int exponent =
        std::clamp(static_cast<int>(std::floor(std::log10(std::abs(value)) / 3.0)) * 3, -18, 9);
    const double scaled = value / std::pow(10.0, exponent);
    return QString::number(scaled, 'g', 3) + QLatin1Char(' ') +
           QString::fromUtf8(Prefixes[(exponent + 18) / 3]) + unit;
}

QColor translucent(QColor color, int alpha) {
    color.setAlpha(alpha);
    return color;
}

std::vector<LogTick> logTicks(double minimum, double maximum, bool subdivide) {
    std::vector<LogTick> ticks;
    if (! (minimum > 0.0) || ! (maximum >= minimum) || ! std::isfinite(maximum)) {
        return ticks;
    }

    // Wide enough to absorb the rounding of 10^x, far too narrow to let a tick
    // that is really outside the view in: at 1e-9 of a decade it would sit a
    // millionth of a pixel beyond the frame.
    constexpr double Tolerance = 1e-9;
    const double lower         = minimum * (1.0 - Tolerance);
    const double upper         = maximum * (1.0 + Tolerance);
    const int firstExponent    = static_cast<int>(std::floor(std::log10(lower)));
    const int lastExponent     = static_cast<int>(std::floor(std::log10(upper)));
    const int lastMultiple     = subdivide ? 9 : 1;
    for (int exponent = firstExponent; exponent <= lastExponent; ++exponent) {
        const double decade = std::pow(10.0, exponent);
        for (int multiple = 1; multiple <= lastMultiple; ++multiple) {
            const double value = multiple * decade;
            if (value >= lower && value <= upper) {
                ticks.push_back({value, multiple});
            }
        }
    }
    return ticks;
}

}  // namespace zwe::plotscale
