// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QtTest>

#include "ui/exportdialog.h"
#include "ui/importdialog.h"

using namespace zwe;

class TestCsvDialogs : public QObject {
    Q_OBJECT

private slots:
    void exportShowsContractInformation();
    void exportSamplingRateIsTemporaryAndUpdatesInformation();
    void exportFormatSwitchesSuffixAndHidesSignificantDigits();
    void importDetectsOneAndTwoColumnFiles();
};

void TestCsvDialogs::exportShowsContractInformation() {
    WaveDocument document;
    document.sampleRate                       = 4.0;
    document.exportSettings.lastExportPath    = QStringLiteral("wave.csv");
    document.exportSettings.significantDigits = 7;
    WaveLayer layer;
    layer.segments.push_back(defaultSegment(SegmentType::Dc));
    document.layers.push_back(std::move(layer));

    ExportDialog dialog(document);
    const auto* information = dialog.findChild<QLabel*>(QStringLiteral("exportInformationLabel"));
    QVERIFY(information);
    QVERIFY(information->text().contains(QStringLiteral("Number of points: 4")));
    QVERIFY(information->text().contains(QStringLiteral("Total duration: 1 s")));
    QVERIFY(! information->text().contains(QStringLiteral("Custom Waveform")));
    QCOMPARE(dialog.filePath(), QStringLiteral("wave.csv"));
    QCOMPARE(dialog.significantDigits(), 7);
    QVERIFY(! dialog.findChild<QDoubleSpinBox*>(QStringLiteral("exportOutputDataRateSpin")));
    QCOMPARE(dialog.findChild<QDoubleSpinBox*>(QStringLiteral("exportSampleRateSpin"))->decimals(), 1);
    QVERIFY(dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->isEnabled());
}

void TestCsvDialogs::exportSamplingRateIsTemporaryAndUpdatesInformation() {
    WaveDocument document = WaveDocument{};
    document.sampleRate   = 4.0;
    WaveLayer layer;
    layer.segments.push_back(defaultSegment(SegmentType::Dc));
    document.layers.push_back(std::move(layer));

    ExportDialog dialog(document);
    auto* sampleRate = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("exportSampleRateSpin"));
    const auto* information = dialog.findChild<QLabel*>(QStringLiteral("exportInformationLabel"));
    QVERIFY(sampleRate);
    QVERIFY(information);
    QCOMPARE(dialog.sampleRate(), 4.0);

    sampleRate->setValue(10.0);
    QCOMPARE(dialog.sampleRate(), 10.0);
    QVERIFY(information->text().contains(QStringLiteral("Number of points: 10")));
    QVERIFY(! information->text().contains(QStringLiteral("Custom Waveform")));
    QCOMPARE(document.sampleRate, 4.0);
}

void TestCsvDialogs::exportFormatSwitchesSuffixAndHidesSignificantDigits() {
    WaveDocument document                  = WaveDocument{};
    document.sampleRate                    = 4.0;
    document.exportSettings.lastExportPath = QStringLiteral("wave.csv");
    WaveLayer layer;
    layer.segments.push_back(defaultSegment(SegmentType::Dc));
    document.layers.push_back(std::move(layer));

    ExportDialog dialog(document);
    auto* format            = dialog.findChild<QComboBox*>(QStringLiteral("exportFormatCombo"));
    auto* digits            = dialog.findChild<QSpinBox*>(QStringLiteral("significantDigitsSpin"));
    const auto* information = dialog.findChild<QLabel*>(QStringLiteral("exportInformationLabel"));
    QVERIFY(format);
    QVERIFY(digits);
    QVERIFY(information);

    // A CSV document opens on CSV: digits apply, no byte count is shown.
    QCOMPARE(dialog.format(), ExportFormat::Csv);
    QCOMPARE(ExportDialog::suffixFor(dialog.format()), QStringLiteral(".csv"));
    QVERIFY(digits->isVisibleTo(&dialog));
    QVERIFY(! information->text().contains(QStringLiteral("File size")));

    format->setCurrentIndex(format->findData(static_cast<int>(ExportFormat::Binary)));
    QCOMPARE(dialog.format(), ExportFormat::Binary);
    QCOMPARE(ExportDialog::suffixFor(dialog.format()), QStringLiteral(".bin"));
    // Digits describe decimal text only, so the row goes away for binary.
    QVERIFY(! digits->isVisibleTo(&dialog));
    QCOMPARE(dialog.filePath(), QStringLiteral("wave.bin"));
    QVERIFY(information->text().contains(QStringLiteral("File size: 32 bytes")));

    // Switching back restores both the row and the suffix.
    format->setCurrentIndex(format->findData(static_cast<int>(ExportFormat::Csv)));
    QVERIFY(digits->isVisibleTo(&dialog));
    QCOMPARE(dialog.filePath(), QStringLiteral("wave.csv"));

    // A name the user typed themselves is left alone.
    dialog.setFilePath(QStringLiteral("measurement-run-3"));
    format->setCurrentIndex(format->findData(static_cast<int>(ExportFormat::Binary)));
    QCOMPARE(dialog.filePath(), QStringLiteral("measurement-run-3"));
}

void TestCsvDialogs::importDetectsOneAndTwoColumnFiles() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString oneColumnPath = directory.filePath(QStringLiteral("values.csv"));
    QFile oneColumn(oneColumnPath);
    QVERIFY(oneColumn.open(QIODevice::WriteOnly));
    QCOMPARE(oneColumn.write("0\n1\n2\n"), 6);
    oneColumn.close();

    ImportDialog dialog(25.0, QStringLiteral("Layer A"), true);
    dialog.setFilePath(oneColumnPath);
    const auto* interpretation =
        dialog.findChild<QLabel*>(QStringLiteral("columnInterpretationLabel"));
    auto* sampleRate = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("importSampleRateSpin"));
    auto* target     = dialog.findChild<QComboBox*>(QStringLiteral("importTargetCombo"));
    QVERIFY(interpretation);
    QVERIFY(sampleRate);
    QVERIFY(target);
    QVERIFY(interpretation->text().contains(QStringLiteral("1-column value list")));
    QVERIFY(sampleRate->isEnabled());
    QCOMPARE(sampleRate->value(), 25.0);
    QCOMPARE(target->count(), 2);
    QCOMPARE(dialog.target(), ImportDialog::Target::NewLayer);

    ImportDialog withoutSelectedLayer(25.0, {}, false);
    QCOMPARE(
        withoutSelectedLayer.findChild<QComboBox*>(QStringLiteral("importTargetCombo"))->count(), 1
    );

    const QString twoColumnPath = directory.filePath(QStringLiteral("time-values.csv"));
    QFile twoColumn(twoColumnPath);
    QVERIFY(twoColumn.open(QIODevice::WriteOnly));
    const QByteArray twoColumnData("time;value\n0;1\n0.5;2\n");
    QCOMPARE(twoColumn.write(twoColumnData), twoColumnData.size());
    twoColumn.close();

    dialog.setFilePath(twoColumnPath);
    QVERIFY(interpretation->text().contains(QStringLiteral("2-column time/value pairs")));
    QVERIFY(! sampleRate->isEnabled());
    QVERIFY(dialog.findChild<QPlainTextEdit*>(QStringLiteral("importPreview"))
                ->toPlainText()
                .startsWith(QStringLiteral("time;value")));
    QVERIFY(dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Open)->isEnabled());
}

QTEST_MAIN(TestCsvDialogs)
#include "tst_csvdialogs.moc"
