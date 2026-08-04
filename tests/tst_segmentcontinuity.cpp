// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include <QUndoStack>
#include <QtTest>

#include "core/sampling.h"
#include "core/segmentcontinuity.h"
#include "ui/commands.h"

using namespace zwe;

namespace {

Segment dc(double value) {
    Segment segment                          = defaultSegment(SegmentType::Dc);
    std::get<DcParams>(segment.params).value = value;
    return segment;
}

WaveLayer layer(std::initializer_list<Segment> segments) {
    WaveLayer result;
    result.segments = segments;
    return result;
}

}  // namespace

class TestSegmentContinuity : public QObject {
    Q_OBJECT

private slots:
    void dcUsesBeginningMiddleAndEndNeighbors();
    void explicitBoundariesUseBothNeighbors();
    void oscillatorsPreserveShapeAndMatchStart_data();
    void oscillatorsPreserveShapeAndMatchStart();
    void sweepsShiftWholeAndKeepTheirSpan_data();
    void sweepsShiftWholeAndKeepTheirSpan();
    void userShapedSegmentsRemainUnchanged();
    void emptyAndDisabledLayersAreSafe();
    void initializedValueSurvivesUndoRedo();
};

void TestSegmentContinuity::dcUsesBeginningMiddleAndEndNeighbors() {
    const WaveLayer source = layer({dc(2.0), dc(8.0)});

    QCOMPARE(
        std::get<DcParams>(initializeSegmentContinuity(source, 0, dc(-1.0)).params).value, 2.0
    );
    QCOMPARE(
        std::get<DcParams>(initializeSegmentContinuity(source, 1, dc(-1.0)).params).value, 2.0
    );
    QCOMPARE(
        std::get<DcParams>(initializeSegmentContinuity(source, 2, dc(-1.0)).params).value, 8.0
    );
}

void TestSegmentContinuity::explicitBoundariesUseBothNeighbors() {
    const WaveLayer source = layer({dc(3.0), dc(9.0)});

    Segment ramp = initializeSegmentContinuity(source, 1, defaultSegment(SegmentType::Ramp));
    QCOMPARE(std::get<RampParams>(ramp.params).startValue, 3.0);
    QCOMPARE(std::get<RampParams>(ramp.params).endValue, 9.0);

    Segment exponential =
        initializeSegmentContinuity(source, 1, defaultSegment(SegmentType::Exponential));
    QCOMPARE(std::get<ExponentialParams>(exponential.params).startValue, 3.0);
    QCOMPARE(std::get<ExponentialParams>(exponential.params).endValue, 9.0);

    ramp = initializeSegmentContinuity(source, 0, defaultSegment(SegmentType::Ramp));
    QCOMPARE(std::get<RampParams>(ramp.params).startValue, 3.0);
    QCOMPARE(std::get<RampParams>(ramp.params).endValue, 3.0);
    ramp = initializeSegmentContinuity(
        source, source.segments.size(), defaultSegment(SegmentType::Ramp)
    );
    QCOMPARE(std::get<RampParams>(ramp.params).startValue, 9.0);
    QCOMPARE(std::get<RampParams>(ramp.params).endValue, 9.0);
}

void TestSegmentContinuity::oscillatorsPreserveShapeAndMatchStart_data() {
    QTest::addColumn<int>("type");
    for (SegmentType type :
         {SegmentType::Sine,
          SegmentType::Square,
          SegmentType::Triangle,
          SegmentType::Chirp,
          SegmentType::Ricker}) {
        QTest::newRow(QByteArray::number(static_cast<int>(type)).constData())
            << static_cast<int>(type);
    }
}

void TestSegmentContinuity::oscillatorsPreserveShapeAndMatchStart() {
    QFETCH(int, type);
    const WaveLayer source = layer({dc(7.5)});
    Segment original       = defaultSegment(static_cast<SegmentType>(type));
    original.duration      = 2.5;
    original.repeat        = 3;
    Segment initialized    = initializeSegmentContinuity(source, 1, original);

    QCOMPARE(initialized.duration, original.duration);
    QCOMPARE(initialized.repeat, original.repeat);
    QVERIFY(qAbs(sampling::segmentValueAt(initialized, 0.0, 0.0) - 7.5) < 1e-12);

    std::visit(
        [&](const auto& before) {
            using Params = std::decay_t<decltype(before)>;
            if constexpr (requires { before.offset; }) {
                const auto& after = std::get<Params>(initialized.params);
                Params expected   = before;
                expected.offset   = after.offset;
                QCOMPARE(after, expected);
            }
        },
        original.params
    );
}

void TestSegmentContinuity::sweepsShiftWholeAndKeepTheirSpan_data() {
    QTest::addColumn<int>("type");
    for (SegmentType type : {SegmentType::Npv, SegmentType::Swv, SegmentType::Dpv}) {
        QTest::newRow(QByteArray::number(static_cast<int>(type)).constData())
            << static_cast<int>(type);
    }
}

void TestSegmentContinuity::sweepsShiftWholeAndKeepTheirSpan() {
    QFETCH(int, type);
    const WaveLayer source    = layer({dc(7.5)});
    const Segment original    = defaultSegment(static_cast<SegmentType>(type));
    const Segment initialized = initializeSegmentContinuity(source, 1, original);

    if (const auto* npv = std::get_if<NpvParams>(&initialized.params)) {
        // NPV rests on a baseline of its own and spends most of its time there,
        // so it is that level which continues the neighbour; the pulses keep
        // standing out from it.
        QVERIFY(qAbs(npv->baseValue - 7.5) < 1e-12);
    } else {
        // The other two open straight into their staircase, so it is the first
        // sample that is matched.
        QVERIFY(qAbs(sampling::segmentValueAt(initialized, 0.0, 0.0) - 7.5) < 1e-12);
    }

    // Every value moves by the same amount, which is what keeps the step count -
    // and so the duration the sweep needs - the one it was created with.
    QCOMPARE(sweepStepCount(initialized), sweepStepCount(original));
    QCOMPARE(sweepDuration(initialized), sweepDuration(original));
    QCOMPARE(initialized.duration, original.duration);
    QCOMPARE(initialized.repeat, original.repeat);

    std::visit(
        [&](const auto& before) {
            using Params = std::decay_t<decltype(before)>;
            if constexpr (requires { before.stepValue; }) {
                const auto& after       = std::get<Params>(initialized.params);
                const double spanBefore = before.endValue - before.startValue;
                const double spanAfter  = after.endValue - after.startValue;
                QVERIFY(qAbs(spanAfter - spanBefore) < 1e-12);

                // Nothing but the levels themselves is touched, and the pulses
                // of an NPV keep their height above its baseline.
                Params expected     = before;
                expected.startValue = after.startValue;
                expected.endValue   = after.endValue;
                if constexpr (std::is_same_v<Params, NpvParams>) {
                    QVERIFY(
                        qAbs((after.startValue - after.baseValue) -
                             (before.startValue - before.baseValue)) < 1e-12
                    );
                    expected.baseValue = after.baseValue;
                }
                QCOMPARE(after, expected);
            }
        },
        original.params
    );
}

void TestSegmentContinuity::userShapedSegmentsRemainUnchanged() {
    const WaveLayer source = layer({dc(4.0)});
    for (SegmentType type : {SegmentType::Pulse, SegmentType::Formula, SegmentType::Points}) {
        const Segment original = defaultSegment(type);
        QCOMPARE(initializeSegmentContinuity(source, 1, original), original);
    }
}

void TestSegmentContinuity::emptyAndDisabledLayersAreSafe() {
    WaveLayer empty;
    empty.enabled          = false;
    const Segment original = defaultSegment(SegmentType::Ramp);
    QCOMPARE(initializeSegmentContinuity(empty, 42, original), original);

    WaveLayer disabled = layer({dc(6.0)});
    disabled.enabled   = false;
    QCOMPARE(
        std::get<DcParams>(initializeSegmentContinuity(disabled, 1, dc(0.0)).params).value, 6.0
    );
}

void TestSegmentContinuity::initializedValueSurvivesUndoRedo() {
    WaveDocument before;
    before.layers.push_back(layer({dc(4.25)}));
    WaveDocument after = before;
    const Segment initialized =
        initializeSegmentContinuity(before.layers.front(), 1, defaultSegment(SegmentType::Sine));
    after.layers.front().segments.push_back(initialized);

    WaveDocument current = before;
    QUndoStack history;
    history.push(new DocumentEditCommand(
        before,
        after,
        DocumentSelection{{0}, 0},
        DocumentSelection{{0}, 1},
        [&](const WaveDocument& document, const DocumentSelection&) { current = document; },
        QStringLiteral("Insert Segment")
    ));
    QCOMPARE(current.layers.front().segments.back(), initialized);
    history.undo();
    QCOMPARE(current, before);
    history.redo();
    QCOMPARE(current.layers.front().segments.back(), initialized);
    QCOMPARE(sampling::segmentValueAt(current.layers.front().segments.back(), 0.0, 0.0), 4.25);
}

QTEST_GUILESS_MAIN(TestSegmentContinuity)
#include "tst_segmentcontinuity.moc"
