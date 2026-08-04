// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include <QFile>
#include <QLocale>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>

#include "core/csvio.h"

using namespace zwe;

namespace {

QString writeFile(QTemporaryDir& directory, const QString& name, const QByteArray& contents) {
    const QString path = directory.filePath(name);
    QFile file(path);
    if (! file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.write(contents);
    return path;
}

WaveDocument sampleDocument(double value, double duration, double sampleRate) {
    WaveDocument document;
    document.sampleRate = sampleRate;
    WaveLayer layer;
    Segment segment                          = defaultSegment(SegmentType::Dc);
    segment.duration                         = duration;
    std::get<DcParams>(segment.params).value = value;
    layer.segments                           = {segment};
    document.layers                          = {layer};
    return document;
}

}  // namespace

class TestCsvIo : public QObject {
    Q_OBJECT

private slots:
    void exportsExactGoldenStringIndependentOfLocale();
    void refusesEmptyAndNonFiniteDocumentsWithoutChangingTarget();
    void importsOneColumnValuesAtSpecifiedRate();
    void importsTwoColumnsWithHeaderAndShiftsTime();
    void rejectsMalformedImportsWithLineNumbers();
    void outputDataRateDoesNotAffectExport();
    void temporaryExportRateDoesNotMutateDocument();
};

void TestCsvIo::exportsExactGoldenStringIndependentOfLocale() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path                        = directory.filePath(QStringLiteral("wave.csv"));

    WaveDocument document                     = sampleDocument(1.25, 1.0, 4.0);
    document.exportSettings.significantDigits = 4;

    const QLocale oldLocale                   = QLocale();
    QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
    const auto result = csvio::exportCsv(document, path);
    QLocale::setDefault(oldLocale);

    QVERIFY(! result.has_value());
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("1.25\n1.25\n1.25\n1.25\n"));
}

void TestCsvIo::temporaryExportRateDoesNotMutateDocument() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    WaveDocument document = sampleDocument(2.0, 1.0, 4.0);
    const QString path    = directory.filePath(QStringLiteral("override.csv"));

    QVERIFY(! csvio::exportCsv(document, path, 10.0).has_value());
    QCOMPARE(document.sampleRate, 4.0);
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll().count('\n'), 10);
}

void TestCsvIo::outputDataRateDoesNotAffectExport() {
    QTemporaryDir directory;
    WaveDocument document   = sampleDocument(0.75, 1.0, 4.0);
    document.outputDataRate = 1.0;
    const QString first     = directory.filePath(QStringLiteral("first.csv"));
    QVERIFY(! csvio::exportCsv(document, first).has_value());
    document.outputDataRate = 12345.0;
    const QString second    = directory.filePath(QStringLiteral("second.csv"));
    QVERIFY(! csvio::exportCsv(document, second).has_value());
    QFile a(first), b(second);
    QVERIFY(a.open(QIODevice::ReadOnly));
    QVERIFY(b.open(QIODevice::ReadOnly));
    QCOMPARE(a.readAll(), b.readAll());
}

void TestCsvIo::refusesEmptyAndNonFiniteDocumentsWithoutChangingTarget() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeFile(directory, QStringLiteral("kept.csv"), QByteArray("keep\n"));
    QVERIFY(! path.isEmpty());

    WaveDocument empty;
    const auto emptyResult = csvio::exportCsv(empty, path);
    QVERIFY(emptyResult.has_value());
    QVERIFY(emptyResult->message.contains(QStringLiteral("empty")));

    WaveDocument nonFinite     = sampleDocument(std::numeric_limits<double>::infinity(), 1.0, 1.0);
    const auto nonFiniteResult = csvio::exportCsv(nonFinite, path);
    QVERIFY(nonFiniteResult.has_value());
    QVERIFY(nonFiniteResult->message.contains(QStringLiteral("non-finite")));

    const auto unwritableResult = csvio::exportCsv(sampleDocument(1.0, 1.0, 1.0), directory.path());
    QVERIFY(unwritableResult.has_value());
    QVERIFY(unwritableResult->message.contains(QStringLiteral("cannot write")));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("keep\n"));
}

void TestCsvIo::importsOneColumnValuesAtSpecifiedRate() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path =
        writeFile(directory, QStringLiteral("values.csv"), QByteArray("0\n0.5\n-1\n"));
    QVERIFY(! path.isEmpty());

    const csvio::ImportResult result = csvio::importCsv(path, 4.0, PointsParams::Interp::Step);
    QVERIFY(result.segment.has_value());
    QVERIFY(! result.error.has_value());
    QCOMPARE(result.segment->duration, 0.75);
    const auto& params = std::get<PointsParams>(result.segment->params);
    QCOMPARE(params.interp, PointsParams::Interp::Step);
    const std::vector<std::pair<double, double>> expectedPoints = {
        {0.0, 0.0}, {0.25, 0.5}, {0.5, -1.0}
    };
    QCOMPARE(params.points, expectedPoints);
}

void TestCsvIo::importsTwoColumnsWithHeaderAndShiftsTime() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeFile(
        directory, QStringLiteral("points.csv"), QByteArray("time;value\n5.0;1.5\n5.25;-2\n")
    );
    QVERIFY(! path.isEmpty());

    const csvio::ImportResult result = csvio::importCsv(path, 0.0);
    QVERIFY(result.segment.has_value());
    QCOMPARE(result.segment->duration, 0.25);
    const auto& params = std::get<PointsParams>(result.segment->params);
    const std::vector<std::pair<double, double>> expectedPoints = {{0.0, 1.5}, {0.25, -2.0}};
    QCOMPARE(params.points, expectedPoints);
}

void TestCsvIo::rejectsMalformedImportsWithLineNumbers() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString badNumber =
        writeFile(directory, QStringLiteral("bad-number.csv"), QByteArray("0\nwhoops\n"));
    const auto badNumberResult = csvio::importCsv(badNumber, 1.0);
    QVERIFY(badNumberResult.error.has_value());
    QVERIFY(badNumberResult.error->message.contains(QStringLiteral("line 2")));

    const QString threeColumns =
        writeFile(directory, QStringLiteral("three.csv"), QByteArray("0;1;2\n"));
    const auto threeColumnsResult = csvio::importCsv(threeColumns, 1.0);
    QVERIFY(threeColumnsResult.error.has_value());
    QVERIFY(threeColumnsResult.error->message.contains(QStringLiteral("line 1")));

    const QString nonFinite =
        writeFile(directory, QStringLiteral("infinity.csv"), QByteArray("0\ninf\n"));
    const auto nonFiniteResult = csvio::importCsv(nonFinite, 1.0);
    QVERIFY(nonFiniteResult.error.has_value());
    QVERIFY(nonFiniteResult.error->message.contains(QStringLiteral("line 2")));

    const QString duplicateTimes =
        writeFile(directory, QStringLiteral("duplicate.csv"), QByteArray("0,1\n0,2\n"));
    const auto duplicateResult = csvio::importCsv(duplicateTimes, 1.0);
    QVERIFY(duplicateResult.error.has_value());
    QVERIFY(duplicateResult.error->message.contains(QStringLiteral("line 2")));

    const QString onePoint =
        writeFile(directory, QStringLiteral("one-point.csv"), QByteArray("1\n"));
    const auto onePointResult = csvio::importCsv(onePoint, 1.0);
    QVERIFY(onePointResult.error.has_value());
    QVERIFY(onePointResult.error->message.contains(QStringLiteral("at least two")));
}

QTEST_MAIN(TestCsvIo)
#include "tst_csvio.moc"
