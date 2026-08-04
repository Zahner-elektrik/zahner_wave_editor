// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include <QtTest>

#include "ui/engineeringnotation.h"

using namespace zwe;

class TestEngineeringEdit : public QObject {
    Q_OBJECT

private slots:
    void parsesEngineeringSuffixes();
    void rejectsInvalidInput();
    void formatsWithFourSignificantDigits();
};

void TestEngineeringEdit::parsesEngineeringSuffixes() {
    QCOMPARE(engineering::parse(QStringLiteral("10m")), std::optional<double>{0.01});
    QCOMPARE(engineering::parse(QStringLiteral("1.5k")), std::optional<double>{1500.0});
    QCOMPARE(engineering::parse(QStringLiteral("2u")), std::optional<double>{2e-6});
    QCOMPARE(engineering::parse(QString::fromUtf8("2µ")), std::optional<double>{2e-6});
    QCOMPARE(engineering::parse(QStringLiteral("1e-3")), std::optional<double>{1e-3});
}

void TestEngineeringEdit::rejectsInvalidInput() {
    QVERIFY(! engineering::parse(QStringLiteral("")));
    QVERIFY(! engineering::parse(QStringLiteral("12x")));
    QVERIFY(! engineering::parse(QStringLiteral("nan")));
}

void TestEngineeringEdit::formatsWithFourSignificantDigits() {
    QCOMPARE(engineering::format(0.00123456), QStringLiteral("1.235m"));
    QCOMPARE(engineering::format(1500.0), QStringLiteral("1.5k"));
    QCOMPARE(engineering::format(999.96), QStringLiteral("1k"));
    QCOMPARE(engineering::format(2e-6), QString::fromUtf8("2µ"));
    QCOMPARE(engineering::format(0.0), QStringLiteral("0"));
}

QTEST_GUILESS_MAIN(TestEngineeringEdit)
#include "tst_engineeringedit.moc"
