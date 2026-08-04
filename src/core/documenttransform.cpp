// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "documenttransform.h"

#include <QRegularExpression>
#include <cmath>
#include <type_traits>

namespace zwe {

namespace {

struct ScaleFactors {
    double amplitudeFactor = 1.0;
    double timeFactor      = 1.0;
    // Pre-formatted for the textual rewrite a formula segment needs.
    QString amplitudeText;
    QString timeText;
    QRegularExpression timeVariable;
};

void scaleLayerTree(WaveLayer& layer, const ScaleFactors& factors) {
    const double amplitudeFactor           = factors.amplitudeFactor;
    const double timeFactor                = factors.timeFactor;
    const QString& amplitude               = factors.amplitudeText;
    const QString& time                    = factors.timeText;
    const QRegularExpression& timeVariable = factors.timeVariable;

    // A group holds no segments of its own, so scaling it means scaling its
    // subtree; a leaf has no children and only runs the loop below.
    for (WaveLayer& child : layer.children) {
        scaleLayerTree(child, factors);
    }

    for (Segment& segment : layer.segments) {
        segment.duration *= timeFactor;
        std::visit(
            [&](auto& params) {
                using Params = std::decay_t<decltype(params)>;
                if constexpr (std::is_same_v<Params, DcParams>) {
                    params.value *= amplitudeFactor;
                } else if constexpr (std::is_same_v<Params, RampParams>) {
                    params.startValue *= amplitudeFactor;
                    params.endValue *= amplitudeFactor;
                } else if constexpr (std::is_same_v<Params, SineParams> ||
                                     std::is_same_v<Params, SquareParams> ||
                                     std::is_same_v<Params, TriangleParams>) {
                    params.amplitude *= amplitudeFactor;
                    params.offset *= amplitudeFactor;
                    params.frequency /= timeFactor;
                } else if constexpr (std::is_same_v<Params, PulseParams>) {
                    params.baseValue *= amplitudeFactor;
                    params.pulseValue *= amplitudeFactor;
                    params.delay *= timeFactor;
                    params.width *= timeFactor;
                } else if constexpr (std::is_same_v<Params, ExponentialParams>) {
                    params.startValue *= amplitudeFactor;
                    params.endValue *= amplitudeFactor;
                    params.timeConstant *= timeFactor;
                } else if constexpr (std::is_same_v<Params, FormulaParams>) {
                    QString expression = params.expression;
                    if (expression.trimmed().isEmpty()) {
                        expression = QStringLiteral("0");
                    }
                    if (timeFactor != 1.0) {
                        expression.replace(timeVariable, QStringLiteral("(t/%1)").arg(time));
                    }
                    if (amplitudeFactor != 1.0) {
                        expression = QStringLiteral("(%1)*(%2)").arg(amplitude, expression);
                    }
                    params.expression = expression;
                } else if constexpr (std::is_same_v<Params, PointsParams>) {
                    for (auto& [pointTime, value] : params.points) {
                        pointTime *= timeFactor;
                        value *= amplitudeFactor;
                    }
                } else if constexpr (std::is_same_v<Params, ChirpParams>) {
                    params.amplitude *= amplitudeFactor;
                    params.offset *= amplitudeFactor;
                    params.startFrequency /= timeFactor;
                    params.endFrequency /= timeFactor;
                } else if constexpr (std::is_same_v<Params, RickerParams>) {
                    params.amplitude *= amplitudeFactor;
                    params.offset *= amplitudeFactor;
                    params.centerFrequency /= timeFactor;
                } else if constexpr (std::is_same_v<Params, WindowParams>) {
                    // alpha, sigma and beta are fractions of the segment, so
                    // they survive a time scale unchanged.
                    params.amplitude *= amplitudeFactor;
                    params.offset *= amplitudeFactor;
                    params.rise *= timeFactor;
                    params.fall *= timeFactor;
                } else if constexpr (std::is_same_v<Params, NpvParams> ||
                                     std::is_same_v<Params, SwvParams> ||
                                     std::is_same_v<Params, DpvParams>) {
                    // The step count has to survive the scale, which it does
                    // because span and step magnitude scale together. A negative
                    // amplitude factor mirrors the sweep by swapping start and
                    // end; the magnitudes stay positive so the scaled segment is
                    // still one a .zwj can be written from and read back.
                    const double magnitudeFactor = std::abs(amplitudeFactor);
                    params.startValue *= amplitudeFactor;
                    params.endValue *= amplitudeFactor;
                    params.stepValue *= magnitudeFactor;
                    if constexpr (std::is_same_v<Params, NpvParams>) {
                        // The baseline is a value on the axis like start and
                        // end, so it mirrors with them and the pulses keep their
                        // height above it.
                        params.baseValue *= amplitudeFactor;
                    }
                    if constexpr (std::is_same_v<Params, SwvParams>) {
                        params.amplitude *= magnitudeFactor;
                        params.period *= timeFactor;
                    } else {
                        if constexpr (std::is_same_v<Params, DpvParams>) {
                            params.pulseValue *= magnitudeFactor;
                        }
                        params.stepTime *= timeFactor;
                        params.pulseTime *= timeFactor;
                    }
                }
            },
            segment.params
        );
    }
}

}  // namespace

WaveDocument scaleDocument(
    const WaveDocument& document, double amplitudeFactor, double timeFactor
) {
    if (! std::isfinite(amplitudeFactor) || ! std::isfinite(timeFactor) || timeFactor <= 0.0) {
        return document;
    }

    WaveDocument scaled = document;
    const ScaleFactors factors{
        amplitudeFactor,
        timeFactor,
        QString::number(amplitudeFactor, 'g', 17),
        QString::number(timeFactor, 'g', 17),
        QRegularExpression(QStringLiteral("\\bt\\b"))
    };
    for (WaveLayer& layer : scaled.layers) {
        scaleLayerTree(layer, factors);
    }
    return scaled;
}

}  // namespace zwe
