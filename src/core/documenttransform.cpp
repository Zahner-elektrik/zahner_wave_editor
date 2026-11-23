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

void scaleLevel(
    std::vector<WaveLayer>& nodes, const ScaleFactors& scaled, const ScaleFactors& timeOnly
);

// Scales the node's own curve by factors.amplitudeFactor. timeOnly carries the
// same time factor with an amplitude factor of 1, for the nodes of a subtree
// whose amplitude has to stay as it is (see scaleLevel()).
void scaleLayerTree(WaveLayer& layer, const ScaleFactors& factors, const ScaleFactors& timeOnly) {
    // A group holds no segments of its own, so scaling it means scaling the
    // fold of its children; a leaf has no children and only runs the loop below.
    scaleLevel(layer.children, factors, timeOnly);

    const double amplitudeFactor           = factors.amplitudeFactor;
    const double timeFactor                = factors.timeFactor;
    const QString& amplitude               = factors.amplitudeText;
    const QString& time                    = factors.timeText;
    const QRegularExpression& timeVariable = factors.timeVariable;

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

// Scales the fold of one level - what sampling makes of these nodes - by
// scaled.amplitudeFactor, not every node in it. The fold starts with the first
// enabled node and then adds or multiplies the others in list order, so the
// result scales by a exactly when the first enabled node and every Add node
// scale by a, while a Multiply node keeps its amplitude: (a*x + a*y) * w is
// a*((x + y) * w). Scaling the Multiply nodes as well would scale the result by
// a once per multiplying node. A disabled node is treated by its mode, as it
// would be once enabled below the first enabled node. The time factor applies
// to every node regardless.
void scaleLevel(
    std::vector<WaveLayer>& nodes, const ScaleFactors& scaled, const ScaleFactors& timeOnly
) {
    bool started = false;
    for (WaveLayer& node : nodes) {
        const bool startsFold      = node.enabled && ! started;
        const bool scalesAmplitude = startsFold || node.mode == LayerMode::Add;
        started                    = started || node.enabled;
        scaleLayerTree(node, scalesAmplitude ? scaled : timeOnly, timeOnly);
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
    ScaleFactors timeOnly    = factors;
    timeOnly.amplitudeFactor = 1.0;
    timeOnly.amplitudeText   = QStringLiteral("1");
    scaleLevel(scaled.layers, factors, timeOnly);
    return scaled;
}

}  // namespace zwe
