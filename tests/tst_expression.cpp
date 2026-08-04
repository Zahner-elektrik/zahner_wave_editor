// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include <QtTest>
#include <cmath>
#include <numbers>

#include "core/expression.h"

using namespace zwe;

namespace {
constexpr double kEps = 1e-9;
}

class TestExpression : public QObject {
    Q_OBJECT

private slots:
    void evaluatesReferenceFormula();
    void constantsPiAndEAreAvailable();
    void syntaxErrorReportsPosition();
    void unknownIdentifierIsCompileError();
    void evaluationNeverThrows();
    void instancesAreIndependent();
    void recompilingReplacesThePreviousExpression();
};

void TestExpression::evaluatesReferenceFormula() {
    Expression expr;
    const auto error = expr.compile(QStringLiteral("sin(2*pi*10*t)*exp(-t/2)"));
    QVERIFY(! error.has_value());

    for (const double t : {0.0, 0.01, 0.1, 0.37, 1.5}) {
        const double expected = std::sin(2.0 * std::numbers::pi * 10.0 * t) * std::exp(-t / 2.0);
        QVERIFY(qAbs(expr.evaluate(t) - expected) < kEps);
    }
}

void TestExpression::constantsPiAndEAreAvailable() {
    Expression piExpr;
    QVERIFY(! piExpr.compile(QStringLiteral("pi")).has_value());
    QVERIFY(qAbs(piExpr.evaluate(0.0) - std::numbers::pi) < kEps);

    Expression eExpr;
    QVERIFY(! eExpr.compile(QStringLiteral("e")).has_value());
    QVERIFY(qAbs(eExpr.evaluate(0.0) - std::numbers::e) < kEps);
}

void TestExpression::syntaxErrorReportsPosition() {
    Expression expr;
    const auto error = expr.compile(QStringLiteral("sin(2*t"));  // unbalanced parenthesis
    QVERIFY(error.has_value());
    QVERIFY(! error->message.isEmpty());
    QVERIFY(error->position >= 0);

    // A failed compile must not leave a stale, previously-working expression
    // reachable through evaluate().
    QCOMPARE(expr.evaluate(1.0), 0.0);
}

void TestExpression::unknownIdentifierIsCompileError() {
    Expression expr;
    const auto error = expr.compile(QStringLiteral("foo*t"));
    QVERIFY(error.has_value());

    // Must not silently evaluate to some coincidental non-zero value.
    QCOMPARE(expr.evaluate(3.0), 0.0);
}

void TestExpression::evaluationNeverThrows() {
    Expression neverCompiled;
    QCOMPARE(neverCompiled.evaluate(1.0), 0.0);  // nothing was ever compiled

    Expression brokenExpr;
    brokenExpr.compile(QStringLiteral("t + )"));
    QCOMPARE(brokenExpr.evaluate(1.0), 0.0);

    // A runtime domain issue (division by zero) must not throw either.
    Expression divByZero;
    QVERIFY(! divByZero.compile(QStringLiteral("1/t")).has_value());
    const double result = divByZero.evaluate(0.0);
    QVERIFY(std::isinf(result) || std::isnan(result) || qAbs(result) >= 0.0);
}

void TestExpression::instancesAreIndependent() {
    Expression a;
    Expression b;
    QVERIFY(! a.compile(QStringLiteral("t*2")).has_value());
    QVERIFY(! b.compile(QStringLiteral("t*3")).has_value());

    QVERIFY(qAbs(a.evaluate(5.0) - 10.0) < kEps);
    QVERIFY(qAbs(b.evaluate(5.0) - 15.0) < kEps);
    // Re-evaluating a after using b must be unaffected by b's state.
    QVERIFY(qAbs(a.evaluate(5.0) - 10.0) < kEps);
}

void TestExpression::recompilingReplacesThePreviousExpression() {
    Expression expr;
    QVERIFY(! expr.compile(QStringLiteral("t*2")).has_value());
    QVERIFY(qAbs(expr.evaluate(4.0) - 8.0) < kEps);

    QVERIFY(! expr.compile(QStringLiteral("t*10")).has_value());
    QVERIFY(qAbs(expr.evaluate(4.0) - 40.0) < kEps);
}

QTEST_GUILESS_MAIN(TestExpression)
#include "tst_expression.moc"
