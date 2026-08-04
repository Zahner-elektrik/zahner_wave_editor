// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include <QtTest>
#include <limits>

#include "core/documenttransform.h"
#include "core/sampling.h"
#include "core/segment.h"
#include "core/wavedocument.h"

using namespace zwe;

namespace {

constexpr double kEps = 1e-9;

void compareDouble(double actual, double expected) {
    QVERIFY2(
        qAbs(actual - expected) < kEps,
        qPrintable(QStringLiteral("%1 != %2").arg(actual).arg(expected))
    );
}

}  // namespace

class TestDocumentTransform : public QObject {
    Q_OBJECT

private slots:
    void scalesAmplitudeAndTimeParameters();
    void formulaScalingPreservesShape();
    void invalidFactorsLeaveDocumentUnchanged();
};

void TestDocumentTransform::scalesAmplitudeAndTimeParameters() {
    WaveDocument document;
    document.sampleRate = 1234.0;
    WaveLayer layer;

    Segment dc                          = defaultSegment(SegmentType::Dc);
    dc.duration                         = 0.5;
    std::get<DcParams>(dc.params).value = 1.25;
    layer.segments.push_back(dc);

    Segment ramp          = defaultSegment(SegmentType::Ramp);
    ramp.duration         = 2.0;
    auto& rampParams      = std::get<RampParams>(ramp.params);
    rampParams.startValue = -1.0;
    rampParams.endValue   = 3.0;
    layer.segments.push_back(ramp);

    Segment sine         = defaultSegment(SegmentType::Sine);
    sine.duration        = 4.0;
    auto& sineParams     = std::get<SineParams>(sine.params);
    sineParams.amplitude = 2.0;
    sineParams.offset    = -0.5;
    sineParams.frequency = 8.0;
    layer.segments.push_back(sine);

    Segment pulse          = defaultSegment(SegmentType::Pulse);
    auto& pulseParams      = std::get<PulseParams>(pulse.params);
    pulseParams.baseValue  = -2.0;
    pulseParams.pulseValue = 5.0;
    pulseParams.delay      = 0.25;
    pulseParams.width      = 0.5;
    layer.segments.push_back(pulse);

    Segment points     = defaultSegment(SegmentType::Points);
    auto& pointParams  = std::get<PointsParams>(points.params);
    pointParams.points = {{0.25, -1.0}, {0.75, 2.0}};
    layer.segments.push_back(points);

    document.layers.push_back(layer);

    const WaveDocument scaled = scaleDocument(document, 2.0, 4.0);
    QCOMPARE(scaled.sampleRate, document.sampleRate);
    QCOMPARE(scaled.layers.size(), std::size_t{1});
    const auto& segments = scaled.layers.front().segments;
    QCOMPARE(segments.size(), std::size_t{5});

    compareDouble(segments[0].duration, 2.0);
    compareDouble(std::get<DcParams>(segments[0].params).value, 2.5);

    compareDouble(segments[1].duration, 8.0);
    compareDouble(std::get<RampParams>(segments[1].params).startValue, -2.0);
    compareDouble(std::get<RampParams>(segments[1].params).endValue, 6.0);

    compareDouble(segments[2].duration, 16.0);
    compareDouble(std::get<SineParams>(segments[2].params).amplitude, 4.0);
    compareDouble(std::get<SineParams>(segments[2].params).offset, -1.0);
    compareDouble(std::get<SineParams>(segments[2].params).frequency, 2.0);

    compareDouble(std::get<PulseParams>(segments[3].params).baseValue, -4.0);
    compareDouble(std::get<PulseParams>(segments[3].params).pulseValue, 10.0);
    compareDouble(std::get<PulseParams>(segments[3].params).delay, 1.0);
    compareDouble(std::get<PulseParams>(segments[3].params).width, 2.0);

    const auto& scaledPoints = std::get<PointsParams>(segments[4].params).points;
    QCOMPARE(scaledPoints.size(), std::size_t{2});
    compareDouble(scaledPoints[0].first, 1.0);
    compareDouble(scaledPoints[0].second, -2.0);
    compareDouble(scaledPoints[1].first, 3.0);
    compareDouble(scaledPoints[1].second, 4.0);
}

void TestDocumentTransform::formulaScalingPreservesShape() {
    Segment formula   = defaultSegment(SegmentType::Formula);
    formula.duration  = 2.0;
    auto& params      = std::get<FormulaParams>(formula.params);
    params.expression = QStringLiteral("t^2 + 1");

    WaveDocument document;
    WaveLayer layer;
    layer.segments.push_back(formula);
    document.layers.push_back(layer);

    const WaveDocument scaled    = scaleDocument(document, 3.0, 2.0);
    const Segment& scaledFormula = scaled.layers.front().segments.front();

    compareDouble(scaledFormula.duration, 4.0);
    compareDouble(
        sampling::segmentValueAt(scaledFormula, 1.5, 1.5),
        3.0 * sampling::segmentValueAt(formula, 0.75, 0.75)
    );
}

void TestDocumentTransform::invalidFactorsLeaveDocumentUnchanged() {
    WaveDocument document;
    WaveLayer layer;
    Segment segment                          = defaultSegment(SegmentType::Dc);
    std::get<DcParams>(segment.params).value = 5.0;
    layer.segments.push_back(segment);
    document.layers.push_back(layer);

    QCOMPARE(scaleDocument(document, 2.0, 0.0), document);
    QCOMPARE(scaleDocument(document, std::numeric_limits<double>::infinity(), 2.0), document);
}

QTEST_MAIN(TestDocumentTransform)
#include "tst_documenttransform.moc"
