// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include <QFile>
#include <QLocale>
#include <QTemporaryDir>
#include <QtTest>
#include <cstring>
#include <limits>

#include "core/binaryio.h"
#include "core/sampling.h"

using namespace zwe;

namespace {

WaveDocument sampleDocument(double value, double duration, double sampleRate) {
    WaveDocument document;
    document.sampleRate                      = sampleRate;
    WaveLayer layer;
    Segment segment                          = defaultSegment(SegmentType::Dc);
    segment.duration                         = duration;
    std::get<DcParams>(segment.params).value = value;
    layer.segments                           = {segment};
    document.layers                          = {layer};
    return document;
}

// The reference encoder: what Python's struct.pack("<d", value) writes.
QByteArray packLittleEndianDouble(double value) {
    quint64 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    QByteArray out;
    for (int byte = 0; byte < 8; ++byte) {
        out.append(static_cast<char>((bits >> (8 * byte)) & 0xFFu));
    }
    return out;
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    if (! file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

}  // namespace

class TestBinaryIo : public QObject {
    Q_OBJECT

private slots:
    void writesLittleEndianDoublesIndependentOfLocale();
    void matchesStructPackForNegativeAndFractionalValues();
    void writesEightBytesPerSampleAcrossTheChunkBoundary();
    void refusesEmptyAndNonFiniteDocumentsWithoutChangingTarget();
    void temporaryExportRateDoesNotMutateDocument();
    void significantDigitsDoNotAffectBinaryExport();
};

void TestBinaryIo::writesLittleEndianDoublesIndependentOfLocale() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path    = directory.filePath(QStringLiteral("wave.bin"));

    WaveDocument document = sampleDocument(1.25, 1.0, 4.0);

    const QLocale oldLocale = QLocale();
    QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
    const auto result = binaryio::exportBinary(document, path);
    QLocale::setDefault(oldLocale);

    QVERIFY(! result.has_value());
    // 1.25 is 0x3FF4000000000000, little-endian: 00 00 00 00 00 00 F4 3F.
    const QByteArray one("\x00\x00\x00\x00\x00\x00\xF4\x3F", 8);
    QCOMPARE(readFile(path), one.repeated(4));
}

void TestBinaryIo::matchesStructPackForNegativeAndFractionalValues() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path          = directory.filePath(QStringLiteral("ramp.bin"));

    // A ramp through zero produces negative, fractional and non-representable
    // values, so every byte of the mantissa and the sign bit are exercised.
    WaveDocument document       = sampleDocument(0.0, 1.0, 16.0);
    Segment ramp                = defaultSegment(SegmentType::Ramp);
    ramp.duration               = 1.0;
    auto& params                = std::get<RampParams>(ramp.params);
    params.startValue           = -1.0;
    params.endValue             = 0.5;
    document.layers[0].segments = {ramp};

    QVERIFY(! binaryio::exportBinary(document, path).has_value());

    // The file must hold exactly the doubles the sampler produced, bit for
    // bit: no rounding, no reordering.
    const std::vector<double> expected = sampling::sampleDocument(document);
    QCOMPARE(expected.size(), size_t{16});
    const QByteArray written = readFile(path);
    QCOMPARE(static_cast<size_t>(written.size()), expected.size() * 8);
    for (size_t index = 0; index < expected.size(); ++index) {
        QCOMPARE(
            written.mid(static_cast<qsizetype>(index) * 8, 8),
            packLittleEndianDouble(expected[index])
        );
    }
}

void TestBinaryIo::writesEightBytesPerSampleAcrossTheChunkBoundary() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("long.bin"));

    // 100 000 samples spans the 65 536-sample write chunk, so a partial final
    // chunk and the buffer reuse in between are both exercised.
    constexpr size_t expectedSamples = 100000;
    WaveDocument document = sampleDocument(-0.5, 1.0, static_cast<double>(expectedSamples));

    QVERIFY(! binaryio::exportBinary(document, path).has_value());

    const QByteArray written = readFile(path);
    QCOMPARE(static_cast<size_t>(written.size()), expectedSamples * 8);
    const QByteArray expected = packLittleEndianDouble(-0.5);
    QCOMPARE(written.first(8), expected);
    QCOMPARE(written.last(8), expected);
    QCOMPARE(written.mid(65536 * 8, 8), expected);
}

void TestBinaryIo::refusesEmptyAndNonFiniteDocumentsWithoutChangingTarget() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("kept.bin"));
    QFile existing(path);
    QVERIFY(existing.open(QIODevice::WriteOnly));
    QCOMPARE(existing.write("keep me"), 7);
    existing.close();

    const WaveDocument empty;
    const auto emptyError = binaryio::exportBinary(empty, path);
    QVERIFY(emptyError.has_value());
    QVERIFY(emptyError->message.contains(QStringLiteral("empty document")));

    const WaveDocument nonFinite =
        sampleDocument(std::numeric_limits<double>::infinity(), 1.0, 1.0);
    const auto nonFiniteError = binaryio::exportBinary(nonFinite, path);
    QVERIFY(nonFiniteError.has_value());
    QVERIFY(nonFiniteError->message.contains(QStringLiteral("non-finite")));

    const auto unwritableError =
        binaryio::exportBinary(sampleDocument(1.0, 1.0, 1.0), directory.path());
    QVERIFY(unwritableError.has_value());
    QVERIFY(unwritableError->message.contains(QStringLiteral("cannot write")));

    QCOMPARE(readFile(path), QByteArray("keep me"));
}

void TestBinaryIo::temporaryExportRateDoesNotMutateDocument() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    WaveDocument document = sampleDocument(2.0, 1.0, 4.0);
    const QString path    = directory.filePath(QStringLiteral("override.bin"));

    QVERIFY(! binaryio::exportBinary(document, path, 10.0).has_value());
    QCOMPARE(document.sampleRate, 4.0);
    QCOMPARE(readFile(path).size(), 10 * 8);
}

void TestBinaryIo::significantDigitsDoNotAffectBinaryExport() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    // A value that a 4-significant-digit CSV export would round; the binary
    // export must keep the full binary64 pattern regardless of the setting.
    WaveDocument document                     = sampleDocument(1.0 / 3.0, 1.0, 1.0);
    document.exportSettings.significantDigits = 4;
    const QString path                        = directory.filePath(QStringLiteral("thirds.bin"));

    QVERIFY(! binaryio::exportBinary(document, path).has_value());
    QCOMPARE(readFile(path), packLittleEndianDouble(1.0 / 3.0));
}

QTEST_MAIN(TestBinaryIo)
#include "tst_binaryio.moc"
