// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include <QCheckBox>
#include <QComboBox>
#include <QSettings>
#include <QSignalSpy>
#include <QtTest>

#include "ui/appsettings.h"
#include "ui/engineeringedit.h"
#include "ui/gridbar.h"

using namespace zwe;

namespace {

constexpr auto SnapToGridKey    = "canvas/snapToGrid";
constexpr auto SnapDivisionsKey = "canvas/snapDivisions";
constexpr auto SnapFixedStepKey = "canvas/snapFixedStep";
constexpr auto SnapTimeStepKey  = "canvas/snapTimeStep";
constexpr auto SnapValueStepKey = "canvas/snapValueStep";

}  // namespace

class TestGridBar : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void snappingIsOnInTenthsOfAGridCellByDefault();
    void switchingToAFixedStepAdoptsTheStepInEffect();
    void aNonPositiveStepIsRejected();
};

void TestGridBar::initTestCase() {
    // Test binaries never run main.cpp's setup, and QSettings fails every access
    // without an organization name. The application name differs from the other
    // suites' because this one writes the canvas keys and ctest runs in parallel.
    QCoreApplication::setOrganizationName(QStringLiteral("Zahner-Elektrik"));
    QCoreApplication::setApplicationName(QStringLiteral("ZahnerWaveEditorGridBarTests"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
}

void TestGridBar::init() {
    QSettings settings;
    for (const char* key :
         {SnapToGridKey, SnapDivisionsKey, SnapFixedStepKey, SnapTimeStepKey, SnapValueStepKey}) {
        settings.remove(QLatin1String(key));
    }
    settings.sync();
}

void TestGridBar::snappingIsOnInTenthsOfAGridCellByDefault() {
    GridBar bar;
    const SnapSettings settings = bar.settings();
    QVERIFY(settings.enabled);
    QVERIFY(! settings.fixedStep);
    QCOMPARE(settings.divisions, 10);

    auto* enabled = bar.findChild<QCheckBox*>(QStringLiteral("snapEnabledCheckBox"));
    QVERIFY(enabled);
    QVERIFY(enabled->isChecked());

    QSignalSpy changed(&bar, &GridBar::settingsChanged);
    enabled->click();
    QCOMPARE(changed.count(), 1);
    QVERIFY(! bar.settings().enabled);
    // Stored right away rather than on close, so the next start agrees.
    QVERIFY(! appsettings::snapToGrid());
}

void TestGridBar::switchingToAFixedStepAdoptsTheStepInEffect() {
    GridBar bar;
    auto* fineness = bar.findChild<QComboBox*>(QStringLiteral("snapFinenessComboBox"));
    auto* timeStep = bar.findChild<EngineeringEdit*>(QStringLiteral("snapTimeStepEdit"));
    QVERIFY(fineness);
    QVERIFY(timeStep);
    // While the step follows the grid the fields are a readout of it.
    QVERIFY(timeStep->isReadOnly());

    bar.showAutomaticStep(0.02, 0.5);
    QCOMPARE(timeStep->value(), 0.02);

    QSignalSpy changed(&bar, &GridBar::settingsChanged);
    const int fixedIndex = fineness->findData(0);
    QVERIFY(fixedIndex >= 0);
    fineness->setCurrentIndex(fixedIndex);
    emit fineness->activated(fixedIndex);
    QCOMPARE(changed.count(), 1);

    const SnapSettings settings = bar.settings();
    QVERIFY(settings.fixedStep);
    QCOMPARE(settings.timeStep, 0.02);
    QCOMPARE(settings.valueStep, 0.5);
    QVERIFY(! timeStep->isReadOnly());
    // The automatic step of a later zoom must not overwrite the typed one.
    bar.showAutomaticStep(1.0, 1.0);
    QCOMPARE(bar.settings().timeStep, 0.02);
}

void TestGridBar::aNonPositiveStepIsRejected() {
    GridBar bar;
    auto* fineness = bar.findChild<QComboBox*>(QStringLiteral("snapFinenessComboBox"));
    auto* timeStep = bar.findChild<EngineeringEdit*>(QStringLiteral("snapTimeStepEdit"));
    QVERIFY(fineness);
    QVERIFY(timeStep);
    bar.showAutomaticStep(0.05, 0.1);
    const int fixedIndex = fineness->findData(0);
    fineness->setCurrentIndex(fixedIndex);
    emit fineness->activated(fixedIndex);
    QCOMPARE(bar.settings().timeStep, 0.05);

    timeStep->setValue(0.0);
    emit timeStep->editingFinished();
    // Zero would quietly stop the time axis from snapping; the stored step stays.
    QCOMPARE(bar.settings().timeStep, 0.05);
    QCOMPARE(timeStep->value(), 0.05);
}

QTEST_MAIN(TestGridBar)
#include "tst_gridbar.moc"
