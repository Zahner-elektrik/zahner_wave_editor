// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include <QtTest>
#include <cmath>
#include <numbers>

#include "core/sampling.h"
#include "core/segment.h"
#include "core/wavedocument.h"
#include "core/wavelayer.h"

using namespace zwe;
using namespace zwe::sampling;

namespace {
constexpr double kEps = 1e-9;
}

class TestSampling : public QObject {
    Q_OBJECT

private slots:
    void dcIsConstant();
    void rampInterpolatesAcrossRepeats();
    void sineMatchesReferenceFormula();
    void squareRespectsDutyAndClampsExtremes();
    void triangleSymmetryAndSawtoothEdges();
    void pulseWindow();
    void exponentialDecay();
    void chirpMatchesAnalyticSweepsAndResets();
    void rickerIsCenteredAndSymmetric();
    void npvPulsesGrowFromAConstantBaseline();
    void dpvPulsesRideOnThePrecedingStaircaseLevel();
    void swvAlternatesAroundARisingStaircase();
    void formulaEvaluatesExpressionRespectingTimeReference();
    void pointsInterpolatesViaPointInterpolation();
    void sampleCountMatchesLlround();
    void twoLayerDocumentSumsAndShorterLayerPadsWithZero();
    void disabledLayerExcludedFromTotalButStillSamplable();
    void layerWindowMatchesLayerValueAt();
    void layerWindowPchipMatchesPerSampleEvaluate();
    void layerWindowPartitionsSegmentBoundariesExactly();
    void documentWindowMatchesSampleDocumentAndReportsProgress();
    void windowShapesAreBoundedAndSymmetric();
    void windowSegmentScalesWithAmplitudeAndOffset();
    void multiplyFoldsInListOrder();
    void firstEnabledNodeStartsTheFold();
    void edgePolicyDecidesWhatALayerContributesOutsideItself();
    void groupCombinesItsChildrenBeforeItsOwnLevel();
    void loopedGroupReplaysItsChildren();
    void leafCaptureFollowsTheLeafOrder();
};

namespace {

Segment dcOf(double value, double duration) {
    Segment segment                          = defaultSegment(SegmentType::Dc);
    std::get<DcParams>(segment.params).value = value;
    segment.duration                         = duration;
    return segment;
}

WaveLayer dcLayer(double value, double duration) {
    WaveLayer layer;
    layer.segments = {dcOf(value, duration)};
    return layer;
}

WaveLayer group(std::vector<WaveLayer> children) {
    WaveLayer node;
    node.kind     = LayerKind::Group;
    node.children = std::move(children);
    return node;
}

// Every value of the window agrees with the scalar reference, which is the
// contract the plot and the export both rely on.
void verifyWindowMatchesScalar(
    const WaveDocument& document, double startTime, double rate, size_t count
) {
    const std::vector<double> values = sampleDocumentWindow(document, startTime, rate, count);
    QCOMPARE(values.size(), count);
    for (size_t k = 0; k < count; ++k) {
        const double t = startTime + static_cast<double>(k) / rate;
        QVERIFY2(
            qAbs(values[k] - documentValueAt(document, t)) < kEps,
            qPrintable(QStringLiteral("t = %1: %2 vs %3")
                           .arg(t)
                           .arg(values[k])
                           .arg(documentValueAt(document, t)))
        );
    }
}

}  // namespace

void TestSampling::dcIsConstant() {
    Segment segment                          = defaultSegment(SegmentType::Dc);
    std::get<DcParams>(segment.params).value = 0.75;
    segment.duration                         = 2.0;

    QCOMPARE(segmentValueAt(segment, 0.0, 0.0), 0.75);
    QCOMPARE(segmentValueAt(segment, 1.0, 1.0), 0.75);
    QCOMPARE(segmentValueAt(segment, 1.999999, 1.999999), 0.75);
}

void TestSampling::rampInterpolatesAcrossRepeats() {
    Segment segment   = defaultSegment(SegmentType::Ramp);
    segment.duration  = 2.0;
    segment.repeat    = 2;
    auto& params      = std::get<RampParams>(segment.params);
    params.startValue = 0.0;
    params.endValue   = 1.0;

    // Characteristic points within one repetition (tauRep in [0, duration)).
    QVERIFY(qAbs(segmentValueAt(segment, 0.0, 0.0) - 0.0) < kEps);
    QVERIFY(qAbs(segmentValueAt(segment, 1.0, 1.0) - 0.5) < kEps);
    QVERIFY(qAbs(segmentValueAt(segment, 1.999999, 1.999999) - 0.9999995) < 1e-6);

    // Across repeats, via a full layer: at global t = 2.0 (start of the 2nd
    // repetition) the ramp must restart at startValue, not continue to 2.0.
    WaveLayer layer;
    layer.segments = {segment};
    QVERIFY(qAbs(layerValueAt(layer, 0.0) - 0.0) < kEps);
    QVERIFY(qAbs(layerValueAt(layer, 2.0) - 0.0) < kEps);
    QVERIFY(qAbs(layerValueAt(layer, 3.0) - 0.5) < kEps);
}

void TestSampling::sineMatchesReferenceFormula() {
    Segment segment  = defaultSegment(SegmentType::Sine);
    auto& params     = std::get<SineParams>(segment.params);
    params.amplitude = 2.0;
    params.frequency = 10.0;
    params.phaseDeg  = 90.0;
    params.offset    = 0.5;
    segment.duration = 1.0;

    for (const double tau : {0.0, 0.013, 0.05, 0.1, 0.37}) {
        const double phaseRad = params.phaseDeg * std::numbers::pi / 180.0;
        const double expected =
            params.offset +
            params.amplitude * std::sin(2.0 * std::numbers::pi * params.frequency * tau + phaseRad);
        QVERIFY(qAbs(segmentValueAt(segment, tau, tau) - expected) < kEps);
    }
}

void TestSampling::squareRespectsDutyAndClampsExtremes() {
    Segment segment  = defaultSegment(SegmentType::Square);
    auto& params     = std::get<SquareParams>(segment.params);
    params.amplitude = 1.0;
    params.frequency = 1.0;
    params.phaseDeg  = 0.0;
    params.offset    = 0.0;
    params.duty      = 0.25;
    segment.duration = 1.0;

    QCOMPARE(segmentValueAt(segment, 0.1, 0.1), 1.0);   // inside the high 25%
    QCOMPARE(segmentValueAt(segment, 0.5, 0.5), -1.0);  // inside the low 75%

    // duty at/near the documented degenerate ends must be clamped to (0,1)
    // rather than producing an always-low / undefined wave.
    params.duty = 0.0;
    QCOMPARE(segmentValueAt(segment, 1e-12, 1e-12), 1.0);
    QCOMPARE(segmentValueAt(segment, 0.5, 0.5), -1.0);

    params.duty = 1.0;
    QCOMPARE(segmentValueAt(segment, 0.5, 0.5), 1.0);
}

void TestSampling::triangleSymmetryAndSawtoothEdges() {
    Segment segment  = defaultSegment(SegmentType::Triangle);
    auto& params     = std::get<TriangleParams>(segment.params);
    params.amplitude = 1.0;
    params.frequency = 1.0;
    params.phaseDeg  = 0.0;
    params.offset    = 0.0;
    segment.duration = 1.0;

    // Symmetric triangle: rises over [0, 0.5), falls over [0.5, 1).
    params.symmetry = 0.5;
    QVERIFY(qAbs(segmentValueAt(segment, 0.0, 0.0) - (-1.0)) < kEps);
    QVERIFY(qAbs(segmentValueAt(segment, 0.25, 0.25) - 0.0) < kEps);
    QVERIFY(qAbs(segmentValueAt(segment, 0.5, 0.5) - 1.0) < kEps);
    QVERIFY(qAbs(segmentValueAt(segment, 0.75, 0.75) - 0.0) < kEps);

    // symmetry == 0: pure descending sawtooth over the whole period.
    params.symmetry = 0.0;
    QVERIFY(qAbs(segmentValueAt(segment, 0.0, 0.0) - 1.0) < kEps);
    QVERIFY(qAbs(segmentValueAt(segment, 0.5, 0.5) - 0.0) < kEps);
    QVERIFY(segmentValueAt(segment, 0.999, 0.999) < 0.0);  // approaching -1, no overshoot
    QVERIFY(segmentValueAt(segment, 0.999, 0.999) >= -1.0 - kEps);

    // symmetry == 1: pure ascending sawtooth over the whole period.
    params.symmetry = 1.0;
    QVERIFY(qAbs(segmentValueAt(segment, 0.0, 0.0) - (-1.0)) < kEps);
    QVERIFY(qAbs(segmentValueAt(segment, 0.5, 0.5) - 0.0) < kEps);
    QVERIFY(segmentValueAt(segment, 0.999, 0.999) > 0.0);
    QVERIFY(segmentValueAt(segment, 0.999, 0.999) <= 1.0 + kEps);
}

void TestSampling::pulseWindow() {
    Segment segment   = defaultSegment(SegmentType::Pulse);
    auto& params      = std::get<PulseParams>(segment.params);
    params.baseValue  = -1.0;
    params.pulseValue = 3.0;
    params.delay      = 0.2;
    params.width      = 0.3;
    segment.duration  = 1.0;

    QCOMPARE(segmentValueAt(segment, 0.0, 0.0), -1.0);   // before delay
    QCOMPARE(segmentValueAt(segment, 0.2, 0.2), 3.0);    // exactly at delay (inclusive)
    QCOMPARE(segmentValueAt(segment, 0.35, 0.35), 3.0);  // inside the pulse
    QCOMPARE(segmentValueAt(segment, 0.5, 0.5), -1.0);   // exactly at delay+width (exclusive)
    QCOMPARE(segmentValueAt(segment, 0.9, 0.9), -1.0);   // after the pulse
}

void TestSampling::exponentialDecay() {
    Segment segment     = defaultSegment(SegmentType::Exponential);
    auto& params        = std::get<ExponentialParams>(segment.params);
    params.startValue   = 2.0;
    params.endValue     = 0.0;
    params.timeConstant = 0.5;
    segment.duration    = 3.0;

    QVERIFY(qAbs(segmentValueAt(segment, 0.0, 0.0) - 2.0) < kEps);
    const double expectedAtOne =
        params.endValue + (params.startValue - params.endValue) * std::exp(-1.0 / 0.5);
    QVERIFY(qAbs(segmentValueAt(segment, 1.0, 1.0) - expectedAtOne) < kEps);
    // After several time constants it must have settled close to endValue.
    QVERIFY(qAbs(segmentValueAt(segment, 3.0, 3.0) - 0.0) < 1e-2);
}

void TestSampling::chirpMatchesAnalyticSweepsAndResets() {
    Segment segment     = defaultSegment(SegmentType::Chirp);
    segment.duration    = 2.0;
    segment.repeat      = 2;
    auto& p             = std::get<ChirpParams>(segment.params);
    p.amplitude         = 2.0;
    p.startFrequency    = 1.0;
    p.endFrequency      = 5.0;
    p.phaseDeg          = 30.0;
    p.offset            = -0.25;

    const auto expected = [&](double t, double cycles) {
        return p.offset + p.amplitude * std::sin(
                                            2.0 * std::numbers::pi * cycles +
                                            p.phaseDeg * std::numbers::pi / 180.0
                                        );
    };
    const double t            = 0.7;
    p.sweep                   = ChirpParams::Sweep::Linear;
    const double linearCycles = p.startFrequency * t + (p.endFrequency - p.startFrequency) * t * t /
                                                           (2.0 * segment.duration);
    QVERIFY(qAbs(segmentValueAt(segment, t, t) - expected(t, linearCycles)) < kEps);

    p.sweep                = ChirpParams::Sweep::Exponential;
    const double ratio     = p.endFrequency / p.startFrequency;
    const double expCycles = p.startFrequency * segment.duration *
                             (std::pow(ratio, t / segment.duration) - 1.0) / std::log(ratio);
    QVERIFY(qAbs(segmentValueAt(segment, t, t) - expected(t, expCycles)) < kEps);

    p.endFrequency = p.startFrequency;
    QVERIFY(qAbs(segmentValueAt(segment, t, t) - expected(t, p.startFrequency * t)) < kEps);

    WaveLayer layer;
    layer.segments = {segment};
    QVERIFY(qAbs(layerValueAt(layer, 0.0) - layerValueAt(layer, segment.duration)) < kEps);
}

void TestSampling::rickerIsCenteredAndSymmetric() {
    Segment segment   = defaultSegment(SegmentType::Ricker);
    segment.duration  = 2.0;
    segment.repeat    = 2;
    auto& p           = std::get<RickerParams>(segment.params);
    p.amplitude       = 3.0;
    p.centerFrequency = 2.0;
    p.offset          = 0.5;

    QCOMPARE(segmentValueAt(segment, 1.0, 1.0), 3.5);
    QVERIFY(qAbs(segmentValueAt(segment, 0.8, 0.8) - segmentValueAt(segment, 1.2, 1.2)) < kEps);
    QVERIFY(qAbs(segmentValueAt(segment, 0.0, 0.0) - p.offset) < 1e-12);

    WaveLayer layer;
    layer.segments = {segment};
    QCOMPARE(layerValueAt(layer, 1.0), layerValueAt(layer, 3.0));
}

void TestSampling::npvPulsesGrowFromAConstantBaseline() {
    Segment segment  = defaultSegment(SegmentType::Npv);
    auto& p          = std::get<NpvParams>(segment.params);
    p.baseValue      = 0.0;
    p.startValue     = 0.1;
    p.endValue       = 0.5;
    p.stepValue      = 0.1;
    p.stepTime       = 1.0;
    p.pulseTime      = 0.4;
    segment.duration = sweepDuration(segment);

    // Both ends of the span are pulses, so there is one pulse more than the
    // span holds steps - and the sweep is one step time longer for it.
    QCOMPARE(sweepStepCount(segment), size_t{4});
    QCOMPARE(sweepLevelCount(segment), size_t{5});
    QCOMPARE(segment.duration, 5.0);

    // The pulse opens every step, the first one on the start value and each
    // next one a step value closer to the end value; the remainder of the step
    // is back on the baseline.
    for (int step = 0; step < 5; ++step) {
        const double start    = static_cast<double>(step);
        const double expected = 0.1 * (step + 1);
        QVERIFY(qAbs(segmentValueAt(segment, start, start) - expected) < kEps);
        QVERIFY(qAbs(segmentValueAt(segment, start + 0.39, 0.0) - expected) < kEps);
        QCOMPARE(segmentValueAt(segment, start + 0.4, 0.0), 0.0);
        QCOMPARE(segmentValueAt(segment, start + 0.99, 0.0), 0.0);
    }

    // Past the sweep the segment stays in its last step rather than starting the
    // staircase over - a restart would put the first pulse right after 5 s.
    QCOMPARE(segmentValueAt(segment, 5.2, 0.0), 0.0);
    QCOMPARE(segmentValueAt(segment, 7.5, 0.0), 0.0);

    // A descending pulse train over a baseline the pulses run towards, which is
    // what the separate base value is there for: the sweep rests at 0 while its
    // pulses walk from 0.5 down to 0.1.
    p.startValue = 0.5;
    p.endValue   = 0.1;
    QCOMPARE(sweepLevelCount(segment), size_t{5});
    for (int step = 0; step < 5; ++step) {
        const double start    = static_cast<double>(step);
        const double expected = 0.5 - 0.1 * step;
        QVERIFY(qAbs(segmentValueAt(segment, start, start) - expected) < kEps);
        QCOMPARE(segmentValueAt(segment, start + 0.5, 0.0), 0.0);
    }

    // The baseline is a value of its own: it moves without touching the pulses,
    // and it may sit anywhere against them - above the train as readily as below
    // it.
    p.baseValue = 0.7;
    QVERIFY(qAbs(segmentValueAt(segment, 0.0, 0.0) - 0.5) < kEps);
    QVERIFY(qAbs(segmentValueAt(segment, 0.5, 0.0) - 0.7) < kEps);
    QVERIFY(qAbs(segmentValueAt(segment, 4.0, 0.0) - 0.1) < kEps);
    QVERIFY(qAbs(segmentValueAt(segment, 9.0, 0.0) - 0.7) < kEps);

    // The scan direction comes from start and end alone: the step value stays a
    // positive magnitude and the pulses mirror.
    p.baseValue  = 0.0;
    p.startValue = 0.0;
    p.endValue   = -0.5;
    QCOMPARE(sweepLevelCount(segment), size_t{6});
    QCOMPARE(segmentValueAt(segment, 0.0, 0.0), 0.0);
    QVERIFY(qAbs(segmentValueAt(segment, 1.2, 0.0) + 0.1) < kEps);
    QVERIFY(qAbs(segmentValueAt(segment, 5.2, 0.0) + 0.5) < kEps);
    QCOMPARE(segmentValueAt(segment, 5.6, 0.0), 0.0);

    // A sweep whose span is shorter than a single step has no staircase to walk
    // and holds its baseline.
    p.baseValue = 0.25;
    p.endValue  = 0.05;
    QCOMPARE(sweepStepCount(segment), size_t{0});
    QCOMPARE(sweepLevelCount(segment), size_t{0});
    QCOMPARE(segmentValueAt(segment, 0.0, 0.0), 0.25);
    QCOMPARE(segmentValueAt(segment, 2.0, 2.0), 0.25);
}

void TestSampling::dpvPulsesRideOnThePrecedingStaircaseLevel() {
    Segment segment  = defaultSegment(SegmentType::Dpv);
    auto& p          = std::get<DpvParams>(segment.params);
    p.startValue     = 0.0;
    p.endValue       = 0.5;
    p.stepValue      = 0.1;
    p.pulseValue     = 0.05;
    p.stepTime       = 1.0;
    p.pulseTime      = 0.4;
    p.invertPulse    = false;
    segment.duration = sweepDuration(segment);

    QCOMPARE(sweepStepCount(segment), size_t{5});
    QCOMPARE(segment.duration, 5.0);

    // Because the pulse opens the step it rises from the level held just
    // before it - the preceding step's base, which is the start value for the
    // first step - while the step itself then settles one step value higher.
    double previousBase = p.startValue;
    for (int step = 0; step < 5; ++step) {
        const double start = static_cast<double>(step);
        const double base  = 0.1 * (step + 1);
        QVERIFY(qAbs(segmentValueAt(segment, start, start) - (previousBase + 0.05)) < kEps);
        QVERIFY(qAbs(segmentValueAt(segment, start + 0.39, 0.0) - (previousBase + 0.05)) < kEps);
        QVERIFY(qAbs(segmentValueAt(segment, start + 0.4, 0.0) - base) < kEps);
        QVERIFY(qAbs(segmentValueAt(segment, start + 0.99, 0.0) - base) < kEps);
        previousBase = base;
    }

    // Past the sweep the last step's base is held, not the baseline.
    QVERIFY(qAbs(segmentValueAt(segment, 9.0, 0.0) - 0.5) < kEps);

    // An inverted pulse points against the scan direction; the staircase itself
    // is unaffected.
    p.invertPulse = true;
    QVERIFY(qAbs(segmentValueAt(segment, 0.0, 0.0) + 0.05) < kEps);
    QVERIFY(qAbs(segmentValueAt(segment, 1.0, 0.0) - (0.1 - 0.05)) < kEps);
    QVERIFY(qAbs(segmentValueAt(segment, 0.5, 0.0) - 0.1) < kEps);

    // A downward sweep mirrors both staircase and pulse.
    p.invertPulse = false;
    p.endValue    = -0.5;
    QVERIFY(qAbs(segmentValueAt(segment, 0.0, 0.0) + 0.05) < kEps);
    QVERIFY(qAbs(segmentValueAt(segment, 0.5, 0.0) + 0.1) < kEps);
    QVERIFY(qAbs(segmentValueAt(segment, 4.5, 0.0) + 0.5) < kEps);
}

void TestSampling::swvAlternatesAroundARisingStaircase() {
    Segment segment  = defaultSegment(SegmentType::Swv);
    auto& p          = std::get<SwvParams>(segment.params);
    p.startValue     = 0.0;
    p.endValue       = 0.5;
    p.stepValue      = 0.1;
    p.amplitude      = 0.02;
    p.period         = 1.0;
    segment.duration = sweepDuration(segment);

    QCOMPARE(sweepStepCount(segment), size_t{5});
    QCOMPARE(segment.duration, 5.0);

    // Forward pulse over the first half of the period, reverse pulse over the
    // second, both around a staircase level that starts at the start value.
    for (int step = 0; step < 5; ++step) {
        const double start = static_cast<double>(step);
        const double base  = 0.1 * step;
        QVERIFY(qAbs(segmentValueAt(segment, start, start) - (base + 0.02)) < kEps);
        QVERIFY(qAbs(segmentValueAt(segment, start + 0.49, 0.0) - (base + 0.02)) < kEps);
        QVERIFY(qAbs(segmentValueAt(segment, start + 0.5, 0.0) - (base - 0.02)) < kEps);
        QVERIFY(qAbs(segmentValueAt(segment, start + 0.99, 0.0) - (base - 0.02)) < kEps);
    }

    // The raised half period comes first whichever way the sweep runs, so a
    // downward sweep still opens above its staircase level.
    p.endValue = -0.5;
    QVERIFY(qAbs(segmentValueAt(segment, 0.0, 0.0) - 0.02) < kEps);
    QVERIFY(qAbs(segmentValueAt(segment, 4.0, 0.0) - (-0.4 + 0.02)) < kEps);
    QVERIFY(qAbs(segmentValueAt(segment, 4.5, 0.0) - (-0.4 - 0.02)) < kEps);

    // The amplitude is a magnitude too - a negative one is not a phase flip.
    p.endValue  = 0.5;
    p.amplitude = -0.02;
    QVERIFY(qAbs(segmentValueAt(segment, 0.0, 0.0) - 0.02) < kEps);
}

void TestSampling::formulaEvaluatesExpressionRespectingTimeReference() {
    // time_reference "local": the segment sees its own tauRep, not tGlobal.
    Segment localSegment   = defaultSegment(SegmentType::Formula);
    auto& localParams      = std::get<FormulaParams>(localSegment.params);
    localParams.expression = QStringLiteral("t*2");
    localParams.globalTime = false;
    QVERIFY(qAbs(segmentValueAt(localSegment, 0.25, 99.0) - 0.5) < kEps);

    // time_reference "global": the segment sees tGlobal instead of tauRep.
    Segment globalSegment   = defaultSegment(SegmentType::Formula);
    auto& globalParams      = std::get<FormulaParams>(globalSegment.params);
    globalParams.expression = QStringLiteral("t*2");
    globalParams.globalTime = true;
    QVERIFY(qAbs(segmentValueAt(globalSegment, 0.25, 99.0) - 198.0) < kEps);

    // An invalid expression evaluates to 0.0 rather than throwing or crashing.
    Segment invalidSegment = defaultSegment(SegmentType::Formula);
    std::get<FormulaParams>(invalidSegment.params).expression = QStringLiteral("nonsense(");
    QCOMPARE(segmentValueAt(invalidSegment, 0.5, 0.5), 0.0);
}

void TestSampling::pointsInterpolatesViaPointInterpolation() {
    // Detailed interpolation-mode coverage lives in tst_points.cpp; this
    // just checks segmentValueAt() actually delegates to it (tauRep, not
    // tGlobal, and defaultSegment()'s (0,0)-(1,1) default line).
    Segment segment = defaultSegment(SegmentType::Points);
    QVERIFY(qAbs(segmentValueAt(segment, 0.5, 99.0) - 0.5) < kEps);
    QCOMPARE(segmentValueAt(segment, -1.0, -1.0), 0.0);  // holds the first point
    QCOMPARE(segmentValueAt(segment, 2.0, 2.0), 1.0);    // holds the last point
}

void TestSampling::sampleCountMatchesLlround() {
    WaveDocument document;
    document.sampleRate = 1000.0;

    WaveLayer layer;
    Segment segment       = defaultSegment(SegmentType::Dc);
    segment.duration      = 0.0123;  // deliberately not a clean multiple of the rate
    segment.repeat        = 1;
    layer.segments        = {segment};
    document.layers       = {layer};

    const double duration = documentDuration(document);
    const size_t expected = static_cast<size_t>(std::llround(duration * document.sampleRate));
    QCOMPARE(sampleCount(document), expected);

    WaveDocument empty;
    QCOMPARE(sampleCount(empty), size_t(0));
}

void TestSampling::twoLayerDocumentSumsAndShorterLayerPadsWithZero() {
    WaveDocument document;
    document.sampleRate = 10.0;

    WaveLayer longLayer;
    longLayer.enabled                            = true;
    Segment longSegment                          = defaultSegment(SegmentType::Dc);
    std::get<DcParams>(longSegment.params).value = 1.0;
    longSegment.duration                         = 2.0;
    longLayer.segments                           = {longSegment};

    WaveLayer shortLayer;
    shortLayer.enabled                            = true;
    Segment shortSegment                          = defaultSegment(SegmentType::Dc);
    std::get<DcParams>(shortSegment.params).value = 5.0;
    shortSegment.duration                         = 1.0;
    shortLayer.segments                           = {shortSegment};

    document.layers                               = {longLayer, shortLayer};

    const std::vector<double> total               = sampleDocument(document);
    const size_t n                                = sampleCount(document);
    QCOMPARE(total.size(), n);

    for (size_t k = 0; k < n; ++k) {
        const double t        = sampleTime(document, k);
        const double expected = t < 1.0 ? 6.0 : 1.0;  // short layer padded with 0 beyond its end
        QVERIFY(qAbs(total[k] - expected) < kEps);
    }
}

void TestSampling::disabledLayerExcludedFromTotalButStillSamplable() {
    WaveDocument document;
    document.sampleRate = 10.0;

    WaveLayer enabledLayer;
    enabledLayer.enabled                            = true;
    Segment enabledSegment                          = defaultSegment(SegmentType::Dc);
    std::get<DcParams>(enabledSegment.params).value = 1.0;
    enabledSegment.duration                         = 1.0;
    enabledLayer.segments                           = {enabledSegment};

    WaveLayer disabledLayer;
    disabledLayer.enabled                            = false;
    Segment disabledSegment                          = defaultSegment(SegmentType::Dc);
    std::get<DcParams>(disabledSegment.params).value = 100.0;
    disabledSegment.duration                         = 1.0;
    disabledLayer.segments                           = {disabledSegment};

    document.layers                                  = {enabledLayer, disabledLayer};

    const std::vector<double> total                  = sampleDocument(document);
    for (const double v : total) {
        QVERIFY(qAbs(v - 1.0) < kEps);  // the disabled layer's 100.0 never shows up
    }

    // sampleLayer() itself ignores `enabled` and still returns the raw curve,
    // so the UI can preview a disabled layer.
    const std::vector<double> disabledCurve = sampleLayer(document, {1});
    for (const double v : disabledCurve) {
        QVERIFY(qAbs(v - 100.0) < kEps);
    }
}

void TestSampling::layerWindowMatchesLayerValueAt() {
    WaveLayer layer;
    Segment sine                                = defaultSegment(SegmentType::Sine);
    sine.duration                               = 0.5;
    sine.repeat                                 = 3;
    std::get<SineParams>(sine.params).frequency = 4.0;
    Segment ramp                                = defaultSegment(SegmentType::Ramp);
    ramp.duration                               = 1.0;
    std::get<RampParams>(ramp.params).endValue  = 2.0;
    layer.segments                              = {sine, ramp};

    // A window starting before t = 0 and reaching past the layer's end:
    // values must match the per-time reference, including the zero padding.
    const double startTime           = -0.25;
    const double rate                = 40.0;
    const size_t count               = 130;  // covers up to t = 3.0 > 2.5 = layer end
    const std::vector<double> values = sampleLayerWindow(layer, startTime, rate, count);
    QCOMPARE(values.size(), count);
    for (size_t k = 0; k < count; ++k) {
        const double t = startTime + static_cast<double>(k) / rate;
        QVERIFY(qAbs(values[k] - layerValueAt(layer, t)) < kEps);
    }
}

void TestSampling::layerWindowPchipMatchesPerSampleEvaluate() {
    // The window sampler hoists the Pchip tangents out of the sample loop;
    // the result must equal the plain per-sample evaluate() path.
    PointsParams params;
    params.interp = PointsParams::Interp::Pchip;
    params.points = {{0.0, 0.0}, {0.3, 1.0}, {0.7, -0.5}, {1.1, 0.25}, {1.5, 2.0}};
    Segment segment;
    segment.duration = 1.5;
    segment.repeat   = 2;
    segment.params   = params;
    WaveLayer layer;
    layer.segments                   = {segment};

    const double rate                = 41.0;
    const std::vector<double> values = sampleLayerWindow(layer, 0.0, rate, 130);
    for (size_t k = 0; k < values.size(); ++k) {
        const double t = static_cast<double>(k) / rate;
        QVERIFY(qAbs(values[k] - layerValueAt(layer, t)) < kEps);
    }
}

void TestSampling::layerWindowPartitionsSegmentBoundariesExactly() {
    WaveLayer layer;
    Segment first                           = defaultSegment(SegmentType::Dc);
    std::get<DcParams>(first.params).value  = 1.0;
    first.duration                          = 1.0;
    Segment second                          = defaultSegment(SegmentType::Dc);
    std::get<DcParams>(second.params).value = 2.0;
    second.duration                         = 1.0;
    layer.segments                          = {first, second};

    // A sample exactly on the boundary belongs to the following segment; a
    // sample exactly on the layer end is zero (same as layerValueAt).
    const std::vector<double> values = sampleLayerWindow(layer, 0.0, 2.0, 5);
    QCOMPARE(values.size(), size_t{5});
    QCOMPARE(values[0], 1.0);  // t = 0.0
    QCOMPARE(values[1], 1.0);  // t = 0.5
    QCOMPARE(values[2], 2.0);  // t = 1.0
    QCOMPARE(values[3], 2.0);  // t = 1.5
    QCOMPARE(values[4], 0.0);  // t = 2.0, past the layer
}

void TestSampling::documentWindowMatchesSampleDocumentAndReportsProgress() {
    WaveDocument document;
    document.sampleRate = 1000.0;

    WaveLayer layer;
    Segment chirp   = defaultSegment(SegmentType::Chirp);
    chirp.duration  = 2.0;
    layer.segments  = {chirp};
    document.layers = {layer};

    size_t calls = 0, lastDone = 0, total = 0;
    const std::vector<double> withProgress =
        sampleDocument(document, [&](size_t done, size_t totalSamples) {
            ++calls;
            QVERIFY(done > lastDone || calls == 1);
            lastDone = done;
            total    = totalSamples;
        });
    QVERIFY(calls >= 1);
    QCOMPARE(lastDone, total);
    QCOMPARE(total, sampleCount(document));

    // Windowed sampling of a sub-range agrees with the full-document samples.
    const size_t offset = 500;
    const std::vector<double> window =
        sampleDocumentWindow(document, offset / document.sampleRate, document.sampleRate, 200);
    for (size_t k = 0; k < window.size(); ++k) {
        const double t  = static_cast<double>(offset + k) / document.sampleRate;
        double expected = 0.0;
        for (const WaveLayer& docLayer : document.layers) {
            expected += layerValueAt(docLayer, t);
        }
        QVERIFY(qAbs(window[k] - expected) < 1e-6);
    }
}

void TestSampling::windowShapesAreBoundedAndSymmetric() {
    const WindowParams::Shape shapes[] = {
        WindowParams::Shape::Rectangular,
        WindowParams::Shape::Hann,
        WindowParams::Shape::Hamming,
        WindowParams::Shape::Blackman,
        WindowParams::Shape::BlackmanHarris,
        WindowParams::Shape::Tukey,
        WindowParams::Shape::Gauss,
        WindowParams::Shape::Kaiser,
        WindowParams::Shape::Trapezoid
    };
    constexpr double duration = 2.0;
    for (const WindowParams::Shape shape : shapes) {
        WindowParams params;
        params.shape = shape;
        for (const double tau : {0.0, 0.13, 0.5, 0.999, 1.0, 1.5, 1.87, 2.0}) {
            const double value = windowShapeAt(params, tau, duration);
            QVERIFY2(value >= 0.0 && value <= 1.0, qPrintable(QString::number(value)));
            // Every shape is symmetric about the middle of the repetition.
            QVERIFY(qAbs(value - windowShapeAt(params, duration - tau, duration)) < 1e-12);
        }
        // The peak sits in the middle and reaches 1 for all but Gauss, which
        // approaches it.
        QVERIFY(windowShapeAt(params, duration / 2.0, duration) > 0.9);
        // Outside the repetition the ends are held, and a degenerate duration
        // cannot divide by zero.
        QCOMPARE(windowShapeAt(params, -1.0, duration), windowShapeAt(params, 0.0, duration));
        QCOMPARE(windowShapeAt(params, 5.0, duration), windowShapeAt(params, duration, duration));
        QCOMPARE(windowShapeAt(params, 0.5, 0.0), 1.0);
    }

    WindowParams hann;
    hann.shape = WindowParams::Shape::Hann;
    QVERIFY(qAbs(windowShapeAt(hann, 0.0, 1.0) - 0.0) < kEps);
    QVERIFY(qAbs(windowShapeAt(hann, 0.5, 1.0) - 1.0) < kEps);
    QVERIFY(qAbs(windowShapeAt(hann, 0.25, 1.0) - 0.5) < kEps);

    WindowParams hamming;
    hamming.shape = WindowParams::Shape::Hamming;
    QVERIFY(qAbs(windowShapeAt(hamming, 0.0, 1.0) - 0.08) < kEps);

    // Tukey degenerates into a rectangle at alpha 0 and into a Hann at alpha 1.
    WindowParams tukey;
    tukey.shape = WindowParams::Shape::Tukey;
    tukey.alpha = 0.0;
    QCOMPARE(windowShapeAt(tukey, 0.0, 1.0), 1.0);
    tukey.alpha = 1.0;
    for (const double tau : {0.0, 0.2, 0.35, 0.5}) {
        QVERIFY(qAbs(windowShapeAt(tukey, tau, 1.0) - windowShapeAt(hann, tau, 1.0)) < kEps);
    }
    tukey.alpha = 0.5;
    QCOMPARE(windowShapeAt(tukey, 0.5, 1.0), 1.0);  // inside the flat part
    QVERIFY(qAbs(windowShapeAt(tukey, 0.125, 1.0) - 0.5) < kEps);  // middle of the taper

    // The trapezoid ramps over rise, holds, and falls over fall.
    WindowParams trapezoid;
    trapezoid.shape = WindowParams::Shape::Trapezoid;
    trapezoid.rise  = 0.2;
    trapezoid.fall  = 0.4;
    QCOMPARE(windowShapeAt(trapezoid, 0.0, 2.0), 0.0);
    QVERIFY(qAbs(windowShapeAt(trapezoid, 0.1, 2.0) - 0.5) < kEps);
    QCOMPARE(windowShapeAt(trapezoid, 1.0, 2.0), 1.0);
    QVERIFY(qAbs(windowShapeAt(trapezoid, 1.8, 2.0) - 0.5) < kEps);
    QCOMPARE(windowShapeAt(trapezoid, 2.0, 2.0), 0.0);
    // More ramp than duration is scaled down instead of misbehaving: dragging a
    // segment boundary shorter must not produce a broken window.
    trapezoid.rise = 3.0;
    trapezoid.fall = 1.0;
    QCOMPARE(windowShapeAt(trapezoid, 0.0, 2.0), 0.0);
    QVERIFY(qAbs(windowShapeAt(trapezoid, 1.5, 2.0) - 1.0) < kEps);
    QCOMPARE(windowShapeAt(trapezoid, 2.0, 2.0), 0.0);

    // Kaiser at beta 0 is a rectangle; a larger beta tapers.
    WindowParams kaiser;
    kaiser.shape = WindowParams::Shape::Kaiser;
    kaiser.beta  = 0.0;
    QVERIFY(qAbs(windowShapeAt(kaiser, 0.0, 1.0) - 1.0) < kEps);
    kaiser.beta = 8.6;
    // 1 / I0(8.6), the textbook end value of a Kaiser window.
    QVERIFY(qAbs(windowShapeAt(kaiser, 0.0, 1.0) - 0.00135) < 1e-4);
    QVERIFY(qAbs(windowShapeAt(kaiser, 0.5, 1.0) - 1.0) < kEps);
}

void TestSampling::windowSegmentScalesWithAmplitudeAndOffset() {
    Segment segment  = defaultSegment(SegmentType::Window);
    segment.duration = 1.0;
    segment.repeat   = 2;
    auto& params     = std::get<WindowParams>(segment.params);
    params.shape     = WindowParams::Shape::Hann;
    params.amplitude = 0.5;
    params.offset    = 0.5;

    // Modulating between half and full, and repeating seamlessly: the second
    // repetition starts over rather than continuing.
    QVERIFY(qAbs(segmentValueAt(segment, 0.0, 0.0) - 0.5) < kEps);
    QVERIFY(qAbs(segmentValueAt(segment, 0.5, 0.5) - 1.0) < kEps);
    WaveLayer layer;
    layer.segments = {segment};
    QVERIFY(qAbs(layerValueAt(layer, 1.0) - 0.5) < kEps);
    QVERIFY(qAbs(layerValueAt(layer, 1.5) - 1.0) < kEps);

    // A negative amplitude turns the window into a notch.
    params.amplitude = -1.0;
    params.offset    = 1.0;
    QVERIFY(qAbs(segmentValueAt(segment, 0.5, 0.5) - 0.0) < kEps);
    QVERIFY(qAbs(segmentValueAt(segment, 0.0, 0.0) - 1.0) < kEps);
}

void TestSampling::multiplyFoldsInListOrder() {
    WaveDocument document;
    document.sampleRate = 8.0;
    WaveLayer carrier   = dcLayer(3.0, 1.0);
    WaveLayer gain      = dcLayer(2.0, 1.0);
    gain.mode           = LayerMode::Multiply;
    WaveLayer offset    = dcLayer(1.0, 1.0);
    document.layers     = {carrier, gain, offset};

    // (3 * 2) + 1: the order of the list is the order of the combination.
    QCOMPARE(documentValueAt(document, 0.5), 7.0);
    verifyWindowMatchesScalar(document, 0.0, document.sampleRate, sampleCount(document));

    // Swapping the last two changes the result, which is what makes the tree
    // order meaningful: 3 + 1 = 4, times 2.
    std::swap(document.layers[1], document.layers[2]);
    QCOMPARE(documentValueAt(document, 0.5), 8.0);
    verifyWindowMatchesScalar(document, 0.0, document.sampleRate, sampleCount(document));
}

void TestSampling::firstEnabledNodeStartsTheFold() {
    WaveDocument document;
    document.sampleRate  = 4.0;
    WaveLayer first      = dcLayer(5.0, 1.0);
    WaveLayer multiplier = dcLayer(3.0, 1.0);
    multiplier.mode      = LayerMode::Multiply;
    document.layers      = {first, multiplier};
    QCOMPARE(documentValueAt(document, 0.5), 15.0);

    // Switching the first layer off must not multiply an empty accumulator to
    // zero: the multiplier now opens the level and its mode is ignored.
    document.layers[0].enabled = false;
    QCOMPARE(documentValueAt(document, 0.5), 3.0);
    verifyWindowMatchesScalar(document, 0.0, document.sampleRate, sampleCount(document));

    // The mode of a layer that opens a level is ignored even when it is the
    // first entry of the list.
    document.layers[0].enabled = true;
    document.layers[0].mode    = LayerMode::Multiply;
    QCOMPARE(documentValueAt(document, 0.5), 15.0);

    // A level without any enabled node contributes nothing at all.
    document.layers[0].enabled = false;
    document.layers[1].enabled = false;
    QCOMPARE(documentValueAt(document, 0.5), 0.0);
}

void TestSampling::edgePolicyDecidesWhatALayerContributesOutsideItself() {
    WaveDocument document;
    document.sampleRate = 10.0;
    WaveLayer base      = dcLayer(2.0, 2.0);
    WaveLayer ramp;
    Segment rampSegment                                   = defaultSegment(SegmentType::Ramp);
    rampSegment.duration                                  = 0.5;
    std::get<RampParams>(rampSegment.params).startValue    = 1.0;
    std::get<RampParams>(rampSegment.params).endValue      = 3.0;
    ramp.segments                                         = {rampSegment};
    document.layers                                       = {base, ramp};

    // Neutral under Add is zero, so nothing changes beyond the short layer.
    QCOMPARE(document.layers[1].edge, LayerEdge::Neutral);
    QCOMPARE(documentValueAt(document, 1.0), 2.0);
    QCOMPARE(documentValueAt(document, 0.25), 4.0);

    // Neutral under Multiply is one, which is what makes a short window usable.
    document.layers[1].mode = LayerMode::Multiply;
    QCOMPARE(documentValueAt(document, 1.0), 2.0);
    QCOMPARE(documentValueAt(document, 0.25), 4.0);
    verifyWindowMatchesScalar(document, -0.5, document.sampleRate, 30);

    // Zero always contributes zero, so a multiplying layer silences the rest.
    document.layers[1].edge = LayerEdge::Zero;
    QCOMPARE(documentValueAt(document, 1.0), 0.0);
    verifyWindowMatchesScalar(document, -0.5, document.sampleRate, 30);

    // Hold last freezes the nearest value of the layer's own curve, on both
    // sides of it - before its start that is where the curve begins.
    document.layers[1].edge = LayerEdge::HoldLast;
    QVERIFY(qAbs(documentValueAt(document, 1.0) - 2.0 * 3.0) < 1e-6);
    QVERIFY(qAbs(layerValueAt(document.layers[1], -0.25) - 1.0) < 1e-6);
    QVERIFY(qAbs(layerValueAt(document.layers[1], 5.0) - 3.0) < 1e-6);
    verifyWindowMatchesScalar(document, -0.5, document.sampleRate, 30);

    // Loop repeats the layer's own curve with its duration as the period, in
    // both directions from it.
    document.layers[1].edge = LayerEdge::Loop;
    QVERIFY(qAbs(documentValueAt(document, 0.25) - documentValueAt(document, 0.75)) < kEps);
    QVERIFY(qAbs(documentValueAt(document, 0.1) - documentValueAt(document, 1.6)) < kEps);
    QVERIFY(
        qAbs(layerValueAt(document.layers[1], -0.4) - layerValueAt(document.layers[1], 0.1)) < kEps
    );
    verifyWindowMatchesScalar(document, -0.5, document.sampleRate, 30);
    // Also with a sample grid that is not commensurate with the period, and
    // with a period shorter than the sample spacing, where the window sampler
    // falls back to evaluating each sample on its own.
    verifyWindowMatchesScalar(document, -0.31, 7.0, 40);
    verifyWindowMatchesScalar(document, 0.0, 1.5, 12);
}

void TestSampling::groupCombinesItsChildrenBeforeItsOwnLevel() {
    // A + (B * window): what a group is for. B is 4, the window halves it, and
    // A adds 1 on top - not (A + B) * window.
    WaveDocument document;
    document.sampleRate  = 8.0;
    WaveLayer windowLayer = dcLayer(0.5, 1.0);
    windowLayer.mode      = LayerMode::Multiply;
    WaveLayer inner       = group({dcLayer(4.0, 1.0), windowLayer});
    document.layers       = {dcLayer(1.0, 1.0), inner};

    QCOMPARE(documentValueAt(document, 0.5), 3.0);
    verifyWindowMatchesScalar(document, 0.0, document.sampleRate, sampleCount(document));

    // The group takes part in its own level with its own mode.
    document.layers[1].mode = LayerMode::Multiply;
    QCOMPARE(documentValueAt(document, 0.5), 2.0);
    verifyWindowMatchesScalar(document, 0.0, document.sampleRate, sampleCount(document));

    // Disabling the group removes the whole subtree.
    document.layers[1].enabled = false;
    QCOMPARE(documentValueAt(document, 0.5), 1.0);

    // A group's own duration is its longest enabled child, so a nested group
    // that is shorter than the document falls back to its edge policy.
    document.layers[1].enabled = true;
    document.layers[1].mode    = LayerMode::Add;
    document.layers[0]         = dcLayer(1.0, 4.0);
    QCOMPARE(documentValueAt(document, 3.0), 1.0);  // group neutral beyond 1 s
    document.layers[1].edge = LayerEdge::HoldLast;
    QCOMPARE(documentValueAt(document, 3.0), 3.0);  // 1 + held 4 * 0.5
    verifyWindowMatchesScalar(document, 0.0, document.sampleRate, sampleCount(document));
}

void TestSampling::loopedGroupReplaysItsChildren() {
    WaveDocument document;
    document.sampleRate = 20.0;
    WaveLayer pulse;
    Segment rampSegment                                = defaultSegment(SegmentType::Ramp);
    rampSegment.duration                               = 0.4;
    std::get<RampParams>(rampSegment.params).endValue  = 1.0;
    pulse.segments                                     = {rampSegment};
    WaveLayer looped                                   = group({pulse});
    looped.edge                                        = LayerEdge::Loop;
    document.layers                                    = {dcLayer(0.0, 2.0), looped};

    // The subtree repeats with the group's duration as the period.
    for (const double t : {0.05, 0.17, 0.33}) {
        QVERIFY(qAbs(documentValueAt(document, t) - documentValueAt(document, t + 0.4)) < kEps);
        QVERIFY(qAbs(documentValueAt(document, t) - documentValueAt(document, t + 1.2)) < kEps);
    }
    verifyWindowMatchesScalar(document, 0.0, document.sampleRate, sampleCount(document));
    verifyWindowMatchesScalar(document, -0.23, 13.0, 60);
}

void TestSampling::leafCaptureFollowsTheLeafOrder() {
    WaveDocument document;
    document.sampleRate = 4.0;
    WaveLayer scaled    = dcLayer(3.0, 1.0);
    scaled.mode         = LayerMode::Multiply;
    WaveLayer disabled  = dcLayer(9.0, 1.0);
    disabled.enabled    = false;
    document.layers     = {dcLayer(1.0, 1.0), group({dcLayer(2.0, 1.0), scaled}), disabled};

    const WindowSamples samples =
        sampleDocumentWindowWithLeaves(document, 0.0, document.sampleRate, 4);
    const std::vector<LayerPath> paths = leafPaths(document.layers);
    QCOMPARE(samples.leaves.size(), paths.size());
    QCOMPARE(samples.leaves.size(), size_t{4});
    for (size_t k = 0; k < 4; ++k) {
        QCOMPARE(samples.total[k], 1.0 + 2.0 * 3.0);
        QCOMPARE(samples.leaves[0][k], 1.0);
        QCOMPARE(samples.leaves[1][k], 2.0);
        QCOMPARE(samples.leaves[2][k], 3.0);
        // A disabled leaf is still captured so the plot can show it, but it
        // does not reach the total.
        QCOMPARE(samples.leaves[3][k], 9.0);
    }

    // The captured curves are what each leaf contributes where it sits: inside
    // a looping group that is the repeated curve, not the leaf on its own.
    WaveDocument looping;
    looping.sampleRate = 4.0;
    WaveLayer inner    = dcLayer(0.0, 1.0);
    Segment ramp       = defaultSegment(SegmentType::Ramp);
    ramp.duration      = 1.0;
    inner.segments     = {ramp};
    WaveLayer wrapper  = group({inner});
    wrapper.edge       = LayerEdge::Loop;
    looping.layers     = {dcLayer(0.0, 3.0), wrapper};
    const WindowSamples repeated =
        sampleDocumentWindowWithLeaves(looping, 0.0, 4.0, 12);
    QCOMPARE(repeated.leaves.size(), size_t{2});
    for (size_t k = 0; k < 12; ++k) {
        const double t = static_cast<double>(k) / 4.0;
        QVERIFY(qAbs(repeated.leaves[1][k] - documentValueAt(looping, t)) < kEps);
    }
    QVERIFY(repeated.leaves[1][5] > 0.0);  // t = 1.25 is inside the second pass
}

QTEST_GUILESS_MAIN(TestSampling)
#include "tst_sampling.moc"
