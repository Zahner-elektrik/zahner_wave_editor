// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QGuiApplication>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QtTest>
#include <limits>

#include "ui/engineeringedit.h"
#include "ui/spectrumpanel.h"

using namespace zwe;

namespace {

// Types a value into a field and commits it, which is what leaving the field
// does for a user.
void commitValue(EngineeringEdit* field, const QString& text) {
    field->selectAll();
    QTest::keyClicks(field, text);
    QTest::keyClick(field, Qt::Key_Return);
}

EngineeringEdit* rateField(SpectrumPanel& panel) {
    return panel.findChild<EngineeringEdit*>(QStringLiteral("spectrumSampleRateEdit"));
}

QString figure(SpectrumPanel& panel, const char* objectName) {
    const auto* label = panel.findChild<QLabel*>(QLatin1String(objectName));
    return label ? label->text() : QStringLiteral("<missing %1>").arg(QLatin1String(objectName));
}

// A plausible analysis: a 1 V sine of 50 Hz on a 1.5 mV offset, one second at
// 1 k 1/s - with one figure left undefined, as for a signal without a mean.
spectrum::Statistics sineStatistics() {
    spectrum::Statistics statistics;
    statistics.count                = 1000;
    statistics.rate                 = 1000.0;
    statistics.duration             = 1.0;
    statistics.mean                 = 0.0015;
    statistics.rms                  = 0.7071;
    statistics.acRms                = 0.7071;
    statistics.minimum              = -0.9985;
    statistics.maximum              = 1.0015;
    statistics.peak                 = 1.0015;
    statistics.peakToPeak           = 2.0;
    statistics.crestFactor          = 1.414;
    statistics.formFactor           = std::numeric_limits<double>::quiet_NaN();
    statistics.maxStep              = 0.3141;
    statistics.maxSlewRate          = 314.1;
    statistics.binWidth             = 1.0;
    statistics.nyquist              = 500.0;
    statistics.fundamentalFrequency = 50.0;
    statistics.fundamentalAmplitude = 1.0;
    statistics.thd                  = 0.0123;
    statistics.nyquistPowerFraction = 0.0;
    return statistics;
}

}  // namespace

class TestSpectrumPanel : public QObject {
    Q_OBJECT

private slots:
    void defaultSettings();
    void rateFollowsValueRateUntilOneIsEntered();
    void typingTheValueRateFollowsItAgain();
    void leavingTheRateFieldUnchangedKeepsTheExactRate();
    void rejectsRatesWithoutSpectrum();
    void everySettingEmitsOnce();
    void showsStatistics();
    void warnsAboutPowerNearNyquist();
    void messageReplacesFigures();
    void copiesFiguresAsTabSeparatedLines();
};

void TestSpectrumPanel::defaultSettings() {
    SpectrumPanel panel;
    const SpectrumPanel::Settings settings = panel.settings();
    QCOMPARE(settings.window, spectrum::Window::Rectangular);
    QVERIFY(settings.logFrequency);
    QVERIFY(! settings.logAmplitude);
    QCOMPARE(panel.findChild<QComboBox*>(QStringLiteral("spectrumWindowCombo"))->currentIndex(), 0);
    QVERIFY(
        ! panel.findChild<QCheckBox*>(QStringLiteral("spectrumLogAmplitudeCheck"))->isChecked()
    );
    QVERIFY(panel.findChild<QCheckBox*>(QStringLiteral("spectrumLogFrequencyCheck"))->isChecked());
    // Nothing analyzed yet, so there is nothing to copy either.
    QVERIFY(! panel.findChild<QPushButton*>(QStringLiteral("spectrumCopyButton"))->isEnabled());
    QVERIFY(
        ! panel.findChild<QWidget*>(QStringLiteral("spectrumFiguresWidget"))->isVisibleTo(&panel)
    );
}

void TestSpectrumPanel::rateFollowsValueRateUntilOneIsEntered() {
    SpectrumPanel panel;
    QSignalSpy changed(&panel, &SpectrumPanel::settingsChanged);
    auto* field = rateField(panel);
    auto* hint  = panel.findChild<QLabel*>(QStringLiteral("spectrumRateHintLabel"));
    QVERIFY(field && hint);

    panel.setValueRate(2000.0);
    QCOMPARE(panel.settings().sampleRate, 2000.0);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(field->value(), 2000.0);
    QCOMPARE(hint->text(), QStringLiteral("value rate"));

    // The same value rate again changes nothing and says nothing.
    panel.setValueRate(2000.0);
    QCOMPARE(changed.count(), 1);

    panel.setValueRate(5000.0);
    QCOMPARE(panel.settings().sampleRate, 5000.0);
    QCOMPARE(changed.count(), 2);

    commitValue(field, QStringLiteral("10k"));
    QCOMPARE(panel.settings().sampleRate, 10000.0);
    QCOMPARE(changed.count(), 3);
    QCOMPARE(hint->text(), QString::fromUtf8("2 × value rate"));

    // A rate of its own stays when the value rate moves.
    panel.setValueRate(8000.0);
    QCOMPARE(panel.settings().sampleRate, 10000.0);
    QCOMPARE(changed.count(), 3);
    QCOMPARE(hint->text(), QString::fromUtf8("1.25 × value rate"));

    commitValue(field, QStringLiteral("4k"));
    QCOMPARE(hint->text(), QString::fromUtf8("0.5 × value rate"));
    QCOMPARE(changed.count(), 4);

    // Entering the value rate itself ties the rate to it again.
    commitValue(field, QStringLiteral("8k"));
    QCOMPARE(panel.settings().sampleRate, 8000.0);
    QCOMPARE(changed.count(), 5);
    QCOMPARE(hint->text(), QStringLiteral("value rate"));

    panel.setValueRate(3000.0);
    QCOMPARE(panel.settings().sampleRate, 3000.0);
    QCOMPARE(changed.count(), 6);
}

void TestSpectrumPanel::typingTheValueRateFollowsItAgain() {
    SpectrumPanel panel;
    panel.setValueRate(1000.0);
    QSignalSpy changed(&panel, &SpectrumPanel::settingsChanged);
    auto* field = rateField(panel);

    commitValue(field, QStringLiteral("3k"));
    QCOMPARE(changed.count(), 1);
    commitValue(field, QStringLiteral("1000"));
    QCOMPARE(panel.settings().sampleRate, 1000.0);
    QCOMPARE(changed.count(), 2);

    panel.setValueRate(1500.0);
    QCOMPARE(panel.settings().sampleRate, 1500.0);
    QCOMPARE(changed.count(), 3);
}

void TestSpectrumPanel::leavingTheRateFieldUnchangedKeepsTheExactRate() {
    // The field shows "1.235M" for this rate. Committing that text must not turn
    // the rate into 1235000 and stop it from following.
    SpectrumPanel panel;
    panel.setValueRate(1234567.0);
    QSignalSpy changed(&panel, &SpectrumPanel::settingsChanged);
    auto* field = rateField(panel);
    QCOMPARE(field->text(), QStringLiteral("1.235M"));

    QTest::keyClick(field, Qt::Key_Return);
    QCOMPARE(panel.settings().sampleRate, 1234567.0);
    QCOMPARE(field->value(), 1234567.0);
    QCOMPARE(changed.count(), 0);

    panel.setValueRate(2000.0);
    QCOMPARE(panel.settings().sampleRate, 2000.0);
    QCOMPARE(changed.count(), 1);
}

void TestSpectrumPanel::rejectsRatesWithoutSpectrum() {
    SpectrumPanel panel;
    panel.setValueRate(1000.0);
    QSignalSpy changed(&panel, &SpectrumPanel::settingsChanged);
    auto* field = rateField(panel);

    for (const QString& text : {QStringLiteral("0"), QStringLiteral("-5")}) {
        commitValue(field, text);
        QCOMPARE(panel.settings().sampleRate, 1000.0);
        QCOMPARE(field->text(), QStringLiteral("1k"));
    }
    QCOMPARE(changed.count(), 0);

    // An invalid value rate is not something to follow.
    panel.setValueRate(0.0);
    panel.setValueRate(std::numeric_limits<double>::quiet_NaN());
    QCOMPARE(panel.settings().sampleRate, 1000.0);
    QCOMPARE(changed.count(), 0);
}

void TestSpectrumPanel::everySettingEmitsOnce() {
    SpectrumPanel panel;
    QSignalSpy changed(&panel, &SpectrumPanel::settingsChanged);
    auto* window       = panel.findChild<QComboBox*>(QStringLiteral("spectrumWindowCombo"));
    auto* logFrequency = panel.findChild<QCheckBox*>(QStringLiteral("spectrumLogFrequencyCheck"));
    auto* logAmplitude = panel.findChild<QCheckBox*>(QStringLiteral("spectrumLogAmplitudeCheck"));
    QVERIFY(window && logFrequency && logAmplitude);
    QCOMPARE(window->count(), 4);

    window->setCurrentIndex(static_cast<int>(spectrum::Window::FlatTop));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(panel.settings().window, spectrum::Window::FlatTop);
    window->setCurrentIndex(static_cast<int>(spectrum::Window::FlatTop));
    QCOMPARE(changed.count(), 1);
    window->setCurrentIndex(static_cast<int>(spectrum::Window::BlackmanHarris));
    QCOMPARE(panel.settings().window, spectrum::Window::BlackmanHarris);
    QCOMPARE(changed.count(), 2);

    logFrequency->click();
    QCOMPARE(changed.count(), 3);
    QVERIFY(! panel.settings().logFrequency);

    logAmplitude->click();
    QCOMPARE(changed.count(), 4);
    QVERIFY(panel.settings().logAmplitude);

    commitValue(rateField(panel), QStringLiteral("2k"));
    QCOMPARE(changed.count(), 5);
    QCOMPARE(panel.settings().sampleRate, 2000.0);

    // Committing the rate it already has is no change.
    commitValue(rateField(panel), QStringLiteral("2k"));
    QCOMPARE(changed.count(), 5);

    // Neither are statistics or messages: they are results, not settings.
    panel.setStatistics(sineStatistics());
    panel.setMessage(QStringLiteral("Analyzing"));
    QCOMPARE(changed.count(), 5);
}

void TestSpectrumPanel::showsStatistics() {
    SpectrumPanel panel;
    panel.setStatistics(sineStatistics());

    QVERIFY(
        panel.findChild<QWidget*>(QStringLiteral("spectrumFiguresWidget"))->isVisibleTo(&panel)
    );
    QVERIFY(
        ! panel.findChild<QLabel*>(QStringLiteral("spectrumMessageLabel"))->isVisibleTo(&panel)
    );
    QVERIFY(panel.findChild<QPushButton*>(QStringLiteral("spectrumCopyButton"))->isEnabled());

    QCOMPARE(figure(panel, "spectrumCountValue"), QStringLiteral("1000"));
    QCOMPARE(figure(panel, "spectrumRateValue"), QStringLiteral("1k 1/s"));
    QCOMPARE(figure(panel, "spectrumDurationValue"), QStringLiteral("1 s"));
    QCOMPARE(figure(panel, "spectrumMeanValue"), QStringLiteral("1.5m"));
    QCOMPARE(figure(panel, "spectrumRmsValue"), QStringLiteral("707.1m"));
    QCOMPARE(figure(panel, "spectrumMinimumValue"), QStringLiteral("-998.5m"));
    QCOMPARE(figure(panel, "spectrumPeakToPeakValue"), QStringLiteral("2"));
    QCOMPARE(figure(panel, "spectrumCrestFactorValue"), QStringLiteral("1.414"));
    QCOMPARE(figure(panel, "spectrumFormFactorValue"), QString::fromUtf8("–"));
    QCOMPARE(figure(panel, "spectrumMaxSlewRateValue"), QStringLiteral("314.1 1/s"));
    QCOMPARE(figure(panel, "spectrumBinWidthValue"), QStringLiteral("1 Hz"));
    QCOMPARE(figure(panel, "spectrumNyquistValue"), QStringLiteral("500 Hz"));
    QCOMPARE(figure(panel, "spectrumFundamentalFrequencyValue"), QStringLiteral("50 Hz"));
    QCOMPARE(figure(panel, "spectrumThdValue"), QStringLiteral("1.23 %"));
    QCOMPARE(figure(panel, "spectrumNyquistPowerValue"), QStringLiteral("0 %"));

    // An undefined figure of any kind reads the same.
    spectrum::Statistics undefined = sineStatistics();
    undefined.rms                  = std::numeric_limits<double>::quiet_NaN();
    undefined.thd                  = std::numeric_limits<double>::quiet_NaN();
    panel.setStatistics(undefined);
    QCOMPARE(figure(panel, "spectrumRmsValue"), QString::fromUtf8("–"));
    QCOMPARE(figure(panel, "spectrumThdValue"), QString::fromUtf8("–"));
}

void TestSpectrumPanel::warnsAboutPowerNearNyquist() {
    SpectrumPanel panel;
    auto* warning = panel.findChild<QLabel*>(QStringLiteral("spectrumAliasingWarning"));
    QVERIFY(warning);

    panel.setStatistics(sineStatistics());
    QVERIFY(! warning->isVisibleTo(&panel));

    spectrum::Statistics aliased = sineStatistics();
    aliased.nyquistPowerFraction = 0.05;
    panel.setStatistics(aliased);
    QVERIFY(warning->isVisibleTo(&panel));
    QVERIFY(warning->text().contains(QStringLiteral("5 %")));

    aliased.nyquistPowerFraction = std::numeric_limits<double>::quiet_NaN();
    panel.setStatistics(aliased);
    QVERIFY(! warning->isVisibleTo(&panel));

    // A message has no figures, so there is nothing to warn about either.
    aliased.nyquistPowerFraction = 0.05;
    panel.setStatistics(aliased);
    panel.setMessage(QStringLiteral("Analyzing"));
    QVERIFY(! warning->isVisibleTo(&panel));
}

void TestSpectrumPanel::messageReplacesFigures() {
    SpectrumPanel panel;
    auto* message = panel.findChild<QLabel*>(QStringLiteral("spectrumMessageLabel"));
    auto* figures = panel.findChild<QWidget*>(QStringLiteral("spectrumFiguresWidget"));
    auto* copy    = panel.findChild<QPushButton*>(QStringLiteral("spectrumCopyButton"));
    QVERIFY(message && figures && copy);

    panel.setStatistics(sineStatistics());
    panel.setMessage(QStringLiteral("The output is too long to analyze."));
    QVERIFY(message->isVisibleTo(&panel));
    QCOMPARE(message->text(), QStringLiteral("The output is too long to analyze."));
    QVERIFY(! figures->isVisibleTo(&panel));
    QVERIFY(! copy->isEnabled());

    // Clearing the message does not bring back figures that were replaced.
    panel.setMessage(QString());
    QVERIFY(! message->isVisibleTo(&panel));
    QVERIFY(! figures->isVisibleTo(&panel));

    panel.setMessage(QStringLiteral("Analyzing"));
    panel.setStatistics(sineStatistics());
    QVERIFY(! message->isVisibleTo(&panel));
    QVERIFY(figures->isVisibleTo(&panel));
    QVERIFY(copy->isEnabled());
}

void TestSpectrumPanel::copiesFiguresAsTabSeparatedLines() {
    SpectrumPanel panel;
    panel.setStatistics(sineStatistics());
    QGuiApplication::clipboard()->clear();

    panel.findChild<QPushButton*>(QStringLiteral("spectrumCopyButton"))->click();
    const QString copied    = QGuiApplication::clipboard()->text();
    const QStringList lines = copied.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QVERIFY(lines.contains(QStringLiteral("RMS\t707.1m")));
    QVERIFY(lines.contains(QStringLiteral("Samples\t1000")));
    QVERIFY(lines.contains(QString::fromUtf8("Form factor\t–")));
    QVERIFY(lines.contains(QStringLiteral("THD\t1.23 %")));
    // One line per figure, and nothing but figures.
    QCOMPARE(lines.size(), 20);
    for (const QString& line : lines) {
        QCOMPARE(line.count(QLatin1Char('\t')), 1);
    }
}

QTEST_MAIN(TestSpectrumPanel)
#include "tst_spectrumpanel.moc"
