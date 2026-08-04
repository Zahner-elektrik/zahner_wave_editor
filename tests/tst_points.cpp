// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include <QtTest>
#include <limits>

#include "core/pointinterpolation.h"
#include "core/segment.h"

using namespace zwe;
using namespace zwe::pointinterpolation;

namespace {
constexpr double kEps = 1e-9;

PointsParams makeParams(
    PointsParams::Interp interp, std::vector<std::pair<double, double>> points
) {
    PointsParams params;
    params.interp = interp;
    params.points = std::move(points);
    return params;
}
}  // namespace

class TestPoints : public QObject {
    Q_OBJECT

private slots:
    void linearExactAtAndBetweenKnots();
    void stepExactAtAndBetweenKnots();
    void holdsFirstAndLastOutsideRange();
    void pchipPassesThroughAllKnots();
    void pchipNoOvershootOnStepLikeData();
    void pchipDegeneratesToLinearForTwoPoints();
    void validationRejectsTooFewPoints();
    void validationRejectsDuplicateT();
    void validationRejectsUnsortedT();
    void validationRejectsNonFiniteValues();
    void validationAcceptsGoodPoints();
};

void TestPoints::linearExactAtAndBetweenKnots() {
    const PointsParams params =
        makeParams(PointsParams::Interp::Linear, {{0.0, 0.0}, {1.0, 2.0}, {2.0, 0.0}});

    QVERIFY(qAbs(evaluate(params, 0.0) - 0.0) < kEps);   // at first knot
    QVERIFY(qAbs(evaluate(params, 1.0) - 2.0) < kEps);   // at middle knot
    QVERIFY(qAbs(evaluate(params, 2.0) - 0.0) < kEps);   // at last knot
    QVERIFY(qAbs(evaluate(params, 0.5) - 1.0) < kEps);   // halfway up
    QVERIFY(qAbs(evaluate(params, 1.5) - 1.0) < kEps);   // halfway down
    QVERIFY(qAbs(evaluate(params, 0.25) - 0.5) < kEps);  // quarter up
}

void TestPoints::stepExactAtAndBetweenKnots() {
    const PointsParams params =
        makeParams(PointsParams::Interp::Step, {{0.0, 1.0}, {1.0, 3.0}, {2.0, 5.0}});

    QCOMPARE(evaluate(params, 0.0), 1.0);
    QCOMPARE(evaluate(params, 0.5), 1.0);  // holds the left knot's value
    QCOMPARE(evaluate(params, 0.999), 1.0);
    QCOMPARE(evaluate(params, 1.0), 3.0);  // jumps exactly at the next knot
    QCOMPARE(evaluate(params, 1.5), 3.0);
    QCOMPARE(evaluate(params, 2.0), 5.0);
}

void TestPoints::holdsFirstAndLastOutsideRange() {
    for (const auto interp :
         {PointsParams::Interp::Linear, PointsParams::Interp::Step, PointsParams::Interp::Pchip}) {
        const PointsParams params = makeParams(interp, {{0.5, 10.0}, {1.0, 20.0}, {1.5, 5.0}});
        QCOMPARE(evaluate(params, 0.0), 10.0);  // before the first point
        QCOMPARE(evaluate(params, -5.0), 10.0);
        QCOMPARE(evaluate(params, 1.5), 5.0);    // exactly at the last point
        QCOMPARE(evaluate(params, 100.0), 5.0);  // after the last point
    }
}

void TestPoints::pchipPassesThroughAllKnots() {
    const PointsParams params =
        makeParams(PointsParams::Interp::Pchip, {{0.0, 0.0}, {1.0, 1.0}, {2.0, 0.5}, {3.0, 2.0}});

    for (const auto& [t, v] : params.points) {
        QVERIFY(qAbs(evaluate(params, t) - v) < kEps);
    }
}

void TestPoints::pchipNoOvershootOnStepLikeData() {
    // A staircase-like point set: flat, flat, then a jump, then flat again.
    // Fritsch-Carlson tangents must not let the spline overshoot beyond the
    // local min/max around the jump.
    const PointsParams params =
        makeParams(PointsParams::Interp::Pchip, {{0.0, 0.0}, {1.0, 0.0}, {2.0, 1.0}, {3.0, 1.0}});

    for (double t = 0.0; t <= 3.0; t += 0.01) {
        const double v = evaluate(params, t);
        QVERIFY2(
            v >= -kEps && v <= 1.0 + kEps,
            qPrintable(QStringLiteral("overshoot at t=%1: v=%2").arg(t).arg(v))
        );
    }
}

void TestPoints::pchipDegeneratesToLinearForTwoPoints() {
    const PointsParams params = makeParams(PointsParams::Interp::Pchip, {{0.0, 0.0}, {2.0, 4.0}});
    for (const double t : {0.0, 0.5, 1.0, 1.5, 2.0}) {
        QVERIFY(qAbs(evaluate(params, t) - 2.0 * t) < kEps);
    }
}

void TestPoints::validationRejectsTooFewPoints() {
    QVERIFY(! arePointsValid({}));
    QVERIFY(! arePointsValid({{0.0, 0.0}}));
}

void TestPoints::validationRejectsDuplicateT() {
    QVERIFY(! arePointsValid({{0.0, 0.0}, {1.0, 1.0}, {1.0, 2.0}}));
}

void TestPoints::validationRejectsUnsortedT() {
    QVERIFY(! arePointsValid({{0.0, 0.0}, {2.0, 1.0}, {1.0, 2.0}}));
}

void TestPoints::validationRejectsNonFiniteValues() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    QVERIFY(! arePointsValid({{0.0, nan}, {1.0, 1.0}}));
    QVERIFY(! arePointsValid({{0.0, 0.0}, {inf, 1.0}}));
}

void TestPoints::validationAcceptsGoodPoints() {
    QVERIFY(arePointsValid({{0.0, 0.0}, {0.5, 1.0}, {1.0, -1.0}}));
}

QTEST_GUILESS_MAIN(TestPoints)
#include "tst_points.moc"
