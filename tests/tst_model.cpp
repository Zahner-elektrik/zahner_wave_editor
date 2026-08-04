// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include <QtTest>

#include "core/segment.h"
#include "core/wavedocument.h"
#include "core/wavelayer.h"

using namespace zwe;

class TestModel : public QObject {
    Q_OBJECT

private slots:
    void segmentTotalDurationIncludesRepeat();
    void layerDurationSumsSegments();
    void documentDurationIgnoresDisabledLayers();
    void documentDurationIsZeroForEmptyDocument();
    void defaultSegmentHasSaneDefaults();
    void segmentTypeMatchesActiveVariantAlternative();
    void sweepStepCountAndDurationDeriveFromTheStaircase();
    void equalityComparesAllFields();
    void groupDurationIsTheLongestEnabledChild();
    void pathsAddressNodesAcrossLevels();
    void leafOrderIsDepthFirst();
};

namespace {

Segment dcSegment(double value, double duration) {
    Segment segment                          = defaultSegment(SegmentType::Dc);
    std::get<DcParams>(segment.params).value = value;
    segment.duration                         = duration;
    return segment;
}

WaveLayer leafLayer(const QString& name, double duration) {
    WaveLayer layer;
    layer.name     = name;
    layer.segments = {dcSegment(1.0, duration)};
    return layer;
}

WaveLayer groupOf(const QString& name, std::vector<WaveLayer> children) {
    WaveLayer group;
    group.name     = name;
    group.kind     = LayerKind::Group;
    group.children = std::move(children);
    return group;
}

}  // namespace

void TestModel::segmentTotalDurationIncludesRepeat() {
    Segment segment;
    segment.duration = 2.0;
    segment.repeat   = 3;
    QCOMPARE(segmentTotalDuration(segment), 6.0);
}

void TestModel::layerDurationSumsSegments() {
    WaveLayer layer;

    Segment a;
    a.duration = 1.0;
    a.repeat   = 2;  // 2.0 s
    Segment b;
    b.duration     = 0.5;
    b.repeat       = 4;  // 2.0 s

    layer.segments = {a, b};
    QCOMPARE(layerDuration(layer), 4.0);

    // An empty layer has zero duration.
    WaveLayer empty;
    QCOMPARE(layerDuration(empty), 0.0);
}

void TestModel::documentDurationIgnoresDisabledLayers() {
    WaveDocument document;

    Segment longSegment;
    longSegment.duration = 10.0;
    longSegment.repeat   = 1;

    Segment shortSegment;
    shortSegment.duration = 1.0;
    shortSegment.repeat   = 1;

    WaveLayer longLayer;
    longLayer.enabled  = true;
    longLayer.segments = {longSegment};

    WaveLayer disabledLongerLayer;
    disabledLongerLayer.enabled  = false;
    disabledLongerLayer.segments = {longSegment, longSegment};  // 20 s, but disabled

    WaveLayer shortLayer;
    shortLayer.enabled  = true;
    shortLayer.segments = {shortSegment};

    document.layers     = {longLayer, disabledLongerLayer, shortLayer};

    // Disabled layers still have their own well-defined duration...
    QCOMPARE(layerDuration(disabledLongerLayer), 20.0);
    // ...but the document only considers enabled layers, so the disabled
    // (longer) layer must not win the max().
    QCOMPARE(documentDuration(document), 10.0);
}

void TestModel::documentDurationIsZeroForEmptyDocument() {
    WaveDocument document;
    QCOMPARE(documentDuration(document), 0.0);

    WaveLayer disabledOnly;
    disabledOnly.enabled = false;
    Segment segment;
    segment.duration      = 5.0;
    disabledOnly.segments = {segment};
    document.layers       = {disabledOnly};
    QCOMPARE(documentDuration(document), 0.0);
}

void TestModel::defaultSegmentHasSaneDefaults() {
    {
        Segment segment = defaultSegment(SegmentType::Dc);
        QCOMPARE(segmentType(segment), SegmentType::Dc);
        QVERIFY(std::holds_alternative<DcParams>(segment.params));
        QCOMPARE(std::get<DcParams>(segment.params).value, 0.0);
    }
    {
        Segment segment = defaultSegment(SegmentType::Ramp);
        QCOMPARE(segmentType(segment), SegmentType::Ramp);
        const auto& params = std::get<RampParams>(segment.params);
        QCOMPARE(params.startValue, 0.0);
        QCOMPARE(params.endValue, 1.0);
    }
    {
        Segment segment = defaultSegment(SegmentType::Sine);
        QCOMPARE(segmentType(segment), SegmentType::Sine);
        const auto& params = std::get<SineParams>(segment.params);
        QCOMPARE(params.amplitude, 1.0);
        QCOMPARE(params.frequency, 1.0);
        QCOMPARE(params.phaseDeg, 0.0);
        QCOMPARE(params.offset, 0.0);
    }
    {
        Segment segment    = defaultSegment(SegmentType::Square);
        const auto& params = std::get<SquareParams>(segment.params);
        QVERIFY(params.duty > 0.0 && params.duty < 1.0);
    }
    {
        Segment segment    = defaultSegment(SegmentType::Triangle);
        const auto& params = std::get<TriangleParams>(segment.params);
        QVERIFY(params.symmetry >= 0.0 && params.symmetry <= 1.0);
    }
    {
        Segment segment    = defaultSegment(SegmentType::Pulse);
        const auto& params = std::get<PulseParams>(segment.params);
        QVERIFY(params.delay >= 0.0);
        QVERIFY(params.width > 0.0);
        QVERIFY(params.delay + params.width <= segment.duration);
    }
    {
        Segment segment    = defaultSegment(SegmentType::Exponential);
        const auto& params = std::get<ExponentialParams>(segment.params);
        QVERIFY(params.timeConstant > 0.0);
    }
    {
        Segment segment    = defaultSegment(SegmentType::Formula);
        const auto& params = std::get<FormulaParams>(segment.params);
        QVERIFY(! params.globalTime);
    }
    {
        Segment segment    = defaultSegment(SegmentType::Points);
        const auto& params = std::get<PointsParams>(segment.params);
        QVERIFY(params.points.size() >= 2);
    }
    {
        Segment segment    = defaultSegment(SegmentType::Chirp);
        const auto& params = std::get<ChirpParams>(segment.params);
        QVERIFY(params.startFrequency >= 0.0);
        QVERIFY(params.endFrequency >= 0.0);
    }
    {
        Segment segment = defaultSegment(SegmentType::Ricker);
        QVERIFY(std::get<RickerParams>(segment.params).centerFrequency > 0.0);
    }
    {
        Segment segment    = defaultSegment(SegmentType::Window);
        const auto& params = std::get<WindowParams>(segment.params);
        QCOMPARE(segmentType(segment), SegmentType::Window);
        QVERIFY(params.alpha >= 0.0 && params.alpha <= 1.0);
        QVERIFY(params.sigma > 0.0);
        QVERIFY(params.beta >= 0.0);
        QVERIFY(params.rise >= 0.0);
        QVERIFY(params.fall >= 0.0);
        QVERIFY(params.rise + params.fall <= segment.duration);
    }

    // A voltammetry segment starts out exactly one sweep long, so adding one
    // shows the whole waveform instead of a cut-off piece of it.
    for (const SegmentType type : {SegmentType::Npv, SegmentType::Swv, SegmentType::Dpv}) {
        const Segment segment = defaultSegment(type);
        QVERIFY(sweepStepCount(segment) >= 1);
        QCOMPARE(segment.duration, sweepDuration(segment));
    }
    {
        Segment segment    = defaultSegment(SegmentType::Npv);
        const auto& params = std::get<NpvParams>(segment.params);
        QVERIFY(params.stepValue > 0.0);
        QVERIFY(params.pulseTime > 0.0 && params.pulseTime <= params.stepTime);
        // The pulses start on the start value rather than one step past it, so
        // the sweep is a step longer than the span alone would make it.
        QCOMPARE(
            segment.duration,
            static_cast<double>(sweepStepCount(segment) + 1) * params.stepTime
        );
    }
    {
        Segment segment    = defaultSegment(SegmentType::Swv);
        const auto& params = std::get<SwvParams>(segment.params);
        QVERIFY(params.stepValue > 0.0);
        QVERIFY(params.amplitude > 0.0);
        QVERIFY(params.period > 0.0);
    }
    {
        Segment segment    = defaultSegment(SegmentType::Dpv);
        const auto& params = std::get<DpvParams>(segment.params);
        QVERIFY(params.stepValue > 0.0);
        QVERIFY(params.pulseValue > 0.0);
        QVERIFY(params.pulseTime > 0.0 && params.pulseTime <= params.stepTime);
        QVERIFY(! params.invertPulse);
    }

    // Every default segment has a strictly positive duration and at least
    // one repeat.
    const SegmentType types[] = {
        SegmentType::Dc,
        SegmentType::Ramp,
        SegmentType::Sine,
        SegmentType::Square,
        SegmentType::Triangle,
        SegmentType::Pulse,
        SegmentType::Exponential,
        SegmentType::Formula,
        SegmentType::Points,
        SegmentType::Chirp,
        SegmentType::Ricker,
        SegmentType::Window,
        SegmentType::Npv,
        SegmentType::Swv,
        SegmentType::Dpv
    };
    for (const SegmentType type : types) {
        const Segment segment = defaultSegment(type);
        QVERIFY(segment.duration > 0.0);
        QVERIFY(segment.repeat >= 1);
    }
}

void TestModel::segmentTypeMatchesActiveVariantAlternative() {
    Segment segment;
    segment.params = SineParams{};
    QCOMPARE(segmentType(segment), SegmentType::Sine);

    segment.params = PointsParams{};
    QCOMPARE(segmentType(segment), SegmentType::Points);

    segment.params = ChirpParams{};
    QCOMPARE(segmentType(segment), SegmentType::Chirp);

    segment.params = RickerParams{};
    QCOMPARE(segmentType(segment), SegmentType::Ricker);

    segment.params = NpvParams{};
    QCOMPARE(segmentType(segment), SegmentType::Npv);

    segment.params = SwvParams{};
    QCOMPARE(segmentType(segment), SegmentType::Swv);

    segment.params = DpvParams{};
    QCOMPARE(segmentType(segment), SegmentType::Dpv);
}

void TestModel::sweepStepCountAndDurationDeriveFromTheStaircase() {
    QCOMPARE(sweepStepCount(0.0, 0.5, 0.1), size_t{5});
    // Rounded down: the sweep walks whole steps only, the remainder is dropped.
    QCOMPARE(sweepStepCount(0.0, 0.55, 0.1), size_t{5});
    // The direction is the caller's business, not the step count's.
    QCOMPARE(sweepStepCount(0.0, -0.5, 0.1), size_t{5});
    QCOMPARE(sweepStepCount(0.0, 0.5, -0.1), size_t{5});
    // Nothing to sweep: a span shorter than one step, and a step of zero.
    QCOMPARE(sweepStepCount(0.0, 0.05, 0.1), size_t{0});
    QCOMPARE(sweepStepCount(0.2, 0.2, 0.1), size_t{0});
    QCOMPARE(sweepStepCount(0.0, 0.5, 0.0), size_t{0});
    // A vanishing step would ask for a sweep no machine can sample.
    QCOMPARE(sweepStepCount(0.0, 1.0, 1e-300), MaximumSweepSteps);

    // Normal pulse voltammetry sends a pulse on the start value and one on the
    // end value, so it holds a level more than the span holds steps and its
    // duration counts that extra one.
    Segment npv          = defaultSegment(SegmentType::Npv);
    auto& npvParams      = std::get<NpvParams>(npv.params);
    npvParams.startValue = 0.0;
    npvParams.endValue   = 0.5;
    npvParams.stepValue  = 0.1;
    npvParams.stepTime   = 0.2;
    QCOMPARE(sweepStepCount(npv), size_t{5});
    QCOMPARE(sweepLevelCount(npv), size_t{6});
    QCOMPARE(sweepStepTime(npv), 0.2);
    QCOMPARE(sweepDuration(npv), 1.2);

    // A span too short for a single step leaves nothing to walk, for npv as
    // well: there is no pulse train of one.
    npvParams.endValue = 0.05;
    QCOMPARE(sweepStepCount(npv), size_t{0});
    QCOMPARE(sweepLevelCount(npv), size_t{0});
    QCOMPARE(sweepDuration(npv), 0.0);

    // For a square wave sweep the period takes the role of the step time, and
    // its staircase holds exactly the steps the span does.
    Segment swv          = defaultSegment(SegmentType::Swv);
    auto& swvParams      = std::get<SwvParams>(swv.params);
    swvParams.startValue = 0.0;
    swvParams.endValue   = 0.5;
    swvParams.stepValue  = 0.1;
    swvParams.period     = 0.4;
    QCOMPARE(sweepStepTime(swv), 0.4);
    QCOMPARE(sweepLevelCount(swv), size_t{5});
    QCOMPARE(sweepDuration(swv), 2.0);

    // Every other segment type has no sweep at all, which a caller fitting a
    // duration has to be able to tell apart from a sweep of length zero.
    const Segment sine = defaultSegment(SegmentType::Sine);
    QCOMPARE(sweepStepCount(sine), size_t{0});
    QCOMPARE(sweepLevelCount(sine), size_t{0});
    QCOMPARE(sweepStepTime(sine), 0.0);
    QCOMPARE(sweepDuration(sine), 0.0);
}

void TestModel::equalityComparesAllFields() {
    WaveDocument a;
    a.name       = QStringLiteral("Doc");
    a.sampleRate = 1000.0;

    WaveLayer layer;
    layer.name      = QStringLiteral("Layer");
    Segment segment = defaultSegment(SegmentType::Sine);
    layer.segments  = {segment};
    a.layers        = {layer};

    WaveDocument b  = a;
    QCOMPARE(a, b);

    // Changing a deeply nested parameter must break equality.
    std::get<SineParams>(b.layers[0].segments[0].params).amplitude = 2.0;
    QVERIFY(! (a == b));

    // Changing a top-level field must break equality too.
    WaveDocument c = a;
    c.name         = QStringLiteral("Other");
    QVERIFY(! (a == c));

    // Segment/layer/params structs themselves compare field-by-field.
    QVERIFY(DcParams{1.0} == DcParams{1.0});
    QVERIFY(! (DcParams{1.0} == DcParams{2.0}));
}

void TestModel::groupDurationIsTheLongestEnabledChild() {
    WaveLayer shortChild = leafLayer(QStringLiteral("short"), 1.0);
    WaveLayer longChild  = leafLayer(QStringLiteral("long"), 4.0);
    WaveLayer group      = groupOf(QStringLiteral("G"), {shortChild, longChild});
    QCOMPARE(layerDuration(group), 4.0);

    // Like documentDuration() one level up, a disabled child does not count.
    group.children[1].enabled = false;
    QCOMPARE(layerDuration(group), 1.0);

    // A group with no children, and one whose children are all off, are 0 s.
    QCOMPARE(layerDuration(groupOf(QStringLiteral("empty"), {})), 0.0);
    group.children[0].enabled = false;
    QCOMPARE(layerDuration(group), 0.0);

    // Nesting keeps working: the deepest content sets the duration.
    const WaveLayer nested = groupOf(
        QStringLiteral("outer"),
        {groupOf(QStringLiteral("inner"), {leafLayer(QStringLiteral("deep"), 2.5)})}
    );
    QCOMPARE(layerDuration(nested), 2.5);

    // A group's own duration takes part in the document's maximum.
    WaveDocument document;
    document.layers = {nested, leafLayer(QStringLiteral("flat"), 1.0)};
    QCOMPARE(documentDuration(document), 2.5);
}

void TestModel::pathsAddressNodesAcrossLevels() {
    std::vector<WaveLayer> roots = {
        leafLayer(QStringLiteral("first"), 1.0),
        groupOf(
            QStringLiteral("group"),
            {leafLayer(QStringLiteral("child"), 1.0),
             groupOf(QStringLiteral("inner"), {leafLayer(QStringLiteral("deep"), 1.0)})}
        )
    };

    QCOMPARE(layerAtPath(roots, {0})->name, QStringLiteral("first"));
    QCOMPARE(layerAtPath(roots, {1, 0})->name, QStringLiteral("child"));
    QCOMPARE(layerAtPath(roots, {1, 1, 0})->name, QStringLiteral("deep"));

    // An empty path, an out-of-range index and a path descending into a leaf
    // all address nothing.
    QCOMPARE(layerAtPath(roots, {}), nullptr);
    QCOMPARE(layerAtPath(roots, {2}), nullptr);
    QCOMPARE(layerAtPath(roots, {0, 0}), nullptr);
    QCOMPARE(layerAtPath(roots, {1, 5}), nullptr);

    // The sibling list is where an insert position resolves, so it exists even
    // for the position after the last node.
    QCOMPARE(layerSiblings(roots, {0}), &roots);
    QCOMPARE(layerSiblings(roots, {1, 2}), &roots[1].children);
    QCOMPARE(layerSiblings(roots, {}), nullptr);
    QCOMPARE(layerSiblings(roots, {0, 0}), nullptr);

    QVERIFY(isLayerPathPrefix({1}, {1, 1, 0}));
    QVERIFY(isLayerPathPrefix({1, 1}, {1, 1}));
    QVERIFY(! isLayerPathPrefix({1, 0}, {1, 1, 0}));
    QVERIFY(! isLayerPathPrefix({1, 1, 0}, {1}));
}

void TestModel::leafOrderIsDepthFirst() {
    std::vector<WaveLayer> roots = {
        leafLayer(QStringLiteral("a"), 1.0),
        groupOf(
            QStringLiteral("group"),
            {leafLayer(QStringLiteral("b"), 1.0),
             groupOf(QStringLiteral("inner"), {leafLayer(QStringLiteral("c"), 1.0)})}
        ),
        leafLayer(QStringLiteral("d"), 1.0)
    };
    // Disabled leaves keep their place: the plot draws them, and the sampler
    // addresses its per-leaf buffers by this order.
    roots[2].enabled = false;

    const std::vector<LayerPath> paths = leafPaths(roots);
    const std::vector<LayerPath> expected{{0}, {1, 0}, {1, 1, 0}, {2}};
    QCOMPARE(paths, expected);

    QCOMPARE(leafCount(roots[0]), size_t{1});
    QCOMPARE(leafCount(roots[1]), size_t{2});
    QCOMPARE(leafCount(groupOf(QStringLiteral("empty"), {})), size_t{0});
}

QTEST_GUILESS_MAIN(TestModel)
#include "tst_model.moc"
