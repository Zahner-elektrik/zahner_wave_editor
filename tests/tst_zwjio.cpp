// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include <QTemporaryDir>
#include <QtTest>

#include "core/zwjio.h"

using namespace zwe;

namespace {

QString testDataPath(const QString& fileName) {
    return QStringLiteral(ZWE_TEST_DATA_DIR) + QStringLiteral("/") + fileName;
}

// Touches every segment type and a handful of non-default document/layer
// fields, so the round-trip test exercises every reader/writer code path.
WaveDocument makeSampleDocument() {
    WaveDocument document;
    document.name                             = QStringLiteral("Round-trip sample");
    document.description                      = QStringLiteral("Covers every segment type");
    document.sampleRate                       = 2500.0;
    document.outputDataRate                   = 17.5;
    document.exportSettings.lastExportPath    = QStringLiteral("/tmp/out.bin");
    document.exportSettings.format            = ExportFormat::Binary;
    document.exportSettings.significantDigits = 6;

    WaveLayer layer;
    layer.name                                         = QStringLiteral("All types");
    layer.enabled                                      = true;

    Segment dc                                         = defaultSegment(SegmentType::Dc);
    std::get<DcParams>(dc.params).value                = 0.25;
    dc.duration                                        = 1.0;
    dc.repeat                                          = 2;

    Segment ramp                                       = defaultSegment(SegmentType::Ramp);
    auto& rampParams                                   = std::get<RampParams>(ramp.params);
    rampParams.startValue                              = -1.0;
    rampParams.endValue                                = 1.0;
    ramp.duration                                      = 1.5;

    Segment sine                                       = defaultSegment(SegmentType::Sine);
    auto& sineParams                                   = std::get<SineParams>(sine.params);
    sineParams.amplitude                               = 2.0;
    sineParams.frequency                               = 5.0;
    sineParams.phaseDeg                                = 45.0;
    sineParams.offset                                  = 0.1;
    sine.duration                                      = 1.0;

    Segment square                                     = defaultSegment(SegmentType::Square);
    std::get<SquareParams>(square.params).duty         = 0.3;
    square.duration                                    = 1.0;

    Segment triangle                                   = defaultSegment(SegmentType::Triangle);
    std::get<TriangleParams>(triangle.params).symmetry = 0.2;
    triangle.duration                                  = 1.0;

    Segment pulse                                      = defaultSegment(SegmentType::Pulse);
    auto& pulseParams                                  = std::get<PulseParams>(pulse.params);
    pulseParams.delay                                  = 0.1;
    pulseParams.width                                  = 0.2;
    pulse.duration                                     = 1.0;

    Segment exponential                                = defaultSegment(SegmentType::Exponential);
    std::get<ExponentialParams>(exponential.params).timeConstant = 0.4;
    exponential.duration                                         = 1.0;

    Segment formula              = defaultSegment(SegmentType::Formula);
    auto& formulaParams          = std::get<FormulaParams>(formula.params);
    formulaParams.expression     = QStringLiteral("sin(2*pi*t)*exp(-t)");
    formulaParams.globalTime     = true;
    formula.duration             = 1.0;

    Segment points               = defaultSegment(SegmentType::Points);
    auto& pointsParams           = std::get<PointsParams>(points.params);
    pointsParams.interp          = PointsParams::Interp::Pchip;
    pointsParams.points          = {{0.0, 0.0}, {0.4, 1.0}, {1.0, -0.5}};
    points.duration              = 1.0;

    Segment chirp                = defaultSegment(SegmentType::Chirp);
    auto& chirpParams            = std::get<ChirpParams>(chirp.params);
    chirpParams.amplitude        = 2.5;
    chirpParams.startFrequency   = 3.0;
    chirpParams.endFrequency     = 30.0;
    chirpParams.phaseDeg         = 12.0;
    chirpParams.offset           = -0.2;
    chirpParams.sweep            = ChirpParams::Sweep::Exponential;

    Segment ricker               = defaultSegment(SegmentType::Ricker);
    auto& rickerParams           = std::get<RickerParams>(ricker.params);
    rickerParams.amplitude       = 4.0;
    rickerParams.centerFrequency = 8.0;
    rickerParams.offset          = 0.3;

    Segment window               = defaultSegment(SegmentType::Window);
    auto& windowParams           = std::get<WindowParams>(window.params);
    windowParams.shape           = WindowParams::Shape::Tukey;
    windowParams.amplitude       = 0.5;
    windowParams.offset          = 0.5;
    windowParams.alpha           = 0.3;
    // The parameters of the other shapes are written too, so switching the
    // shape in the panel and back finds what one had typed.
    windowParams.sigma           = 0.25;
    windowParams.beta            = 12.0;
    windowParams.rise            = 0.05;
    windowParams.fall            = 0.15;

    Segment npv                  = defaultSegment(SegmentType::Npv);
    auto& npvParams              = std::get<NpvParams>(npv.params);
    npvParams.baseValue          = -0.35;
    npvParams.startValue         = -0.2;
    npvParams.endValue           = 0.4;
    npvParams.stepValue          = 0.02;
    npvParams.stepTime           = 0.05;
    npvParams.pulseTime          = 0.02;
    npv.duration                 = sweepDuration(npv);

    Segment swv                  = defaultSegment(SegmentType::Swv);
    auto& swvParams              = std::get<SwvParams>(swv.params);
    swvParams.startValue         = 0.1;
    swvParams.endValue           = -0.5;
    swvParams.stepValue          = 0.005;
    swvParams.amplitude          = 0.03;
    swvParams.period             = 0.04;
    swv.duration                 = sweepDuration(swv);
    swv.repeat                   = 2;

    Segment dpv                  = defaultSegment(SegmentType::Dpv);
    auto& dpvParams              = std::get<DpvParams>(dpv.params);
    dpvParams.startValue         = 0.0;
    dpvParams.endValue           = 0.6;
    dpvParams.stepValue          = 0.004;
    dpvParams.pulseValue         = 0.06;
    dpvParams.stepTime           = 0.08;
    dpvParams.pulseTime          = 0.03;
    dpvParams.invertPulse        = true;
    dpv.duration                 = sweepDuration(dpv);

    layer.segments               = {
        dc,
        ramp,
        sine,
        square,
        triangle,
        pulse,
        exponential,
        formula,
        points,
        chirp,
        ricker,
        window,
        npv,
        swv,
        dpv
    };

    WaveLayer disabledLayer;
    disabledLayer.name     = QStringLiteral("Disabled");
    disabledLayer.enabled  = false;
    disabledLayer.segments = {defaultSegment(SegmentType::Dc)};

    // A group with a multiplying, looping child and a nested group inside it,
    // so the round trip covers every layer field the format has.
    WaveLayer windowLayer;
    windowLayer.name     = QStringLiteral("Envelope");
    windowLayer.mode     = LayerMode::Multiply;
    windowLayer.edge     = LayerEdge::Loop;
    windowLayer.segments = {window};

    WaveLayer innerGroup;
    innerGroup.name     = QStringLiteral("Inner");
    innerGroup.kind     = LayerKind::Group;
    innerGroup.edge     = LayerEdge::HoldLast;
    innerGroup.children = {windowLayer};

    WaveLayer outerGroup;
    outerGroup.name     = QStringLiteral("Outer");
    outerGroup.kind     = LayerKind::Group;
    outerGroup.mode     = LayerMode::Multiply;
    outerGroup.edge     = LayerEdge::Zero;
    outerGroup.children = {disabledLayer, innerGroup};

    WaveLayer emptyGroup;
    emptyGroup.name = QStringLiteral("Empty");
    emptyGroup.kind = LayerKind::Group;

    document.layers = {layer, disabledLayer, outerGroup, emptyGroup};
    return document;
}

}  // namespace

class TestZwjIo : public QObject {
    Q_OBJECT

private slots:
    void loadsExampleDocument();
    void roundTripIsByteIdentical();
    void rejectsWrongFormat();
    void rejectsTooNewVersion();
    void rejectsUnknownSegmentType();
    void rejectsNonFiniteNumbers();
    void rejectsMissingRequiredField();
    void ignoresUnknownExtraKeys();
    void defaultsMissingExportFormatToCsvAndRejectsUnknownOnes();
    void rejectsUnknownMode();
    void rejectsOutOfRangeDuty();
    void rejectsInvalidAdvancedFrequencies();
    void rejectsInvalidSweepParameters();
    void versionOneFilesLoadAsAdditiveLeafLayers();
    void readsLayerModeEdgeAndNestedChildren();
    void rejectsBrokenLayerFields();
    void rejectsBrokenWindowParams();
};

void TestZwjIo::loadsExampleDocument() {
    const zwjio::LoadResult result = zwjio::load(testDataPath(QStringLiteral("example.zwj")));
    QVERIFY2(! result.error.has_value(), qPrintable(result.error.value_or(zwjio::Error{}).message));
    QVERIFY(result.document.has_value());

    const WaveDocument& doc = *result.document;
    QCOMPARE(doc.name, QStringLiteral("Ramp with ripple"));
    QCOMPARE(doc.sampleRate, 10000.0);
    QCOMPARE(doc.outputDataRate, 1.0);  // legacy v1 default
    QCOMPARE(doc.layers.size(), size_t(2));
    QCOMPARE(doc.layers[0].name, QStringLiteral("Sweep"));
    QCOMPARE(doc.layers[0].segments.size(), size_t(2));
    QCOMPARE(segmentType(doc.layers[0].segments[0]), SegmentType::Dc);
    QCOMPARE(segmentType(doc.layers[0].segments[1]), SegmentType::Ramp);
    QCOMPARE(doc.layers[1].name, QStringLiteral("Ripple"));
    QCOMPARE(segmentType(doc.layers[1].segments[0]), SegmentType::Sine);
}

void TestZwjIo::roundTripIsByteIdentical() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const WaveDocument original = makeSampleDocument();
    const QString path1         = dir.filePath(QStringLiteral("a.zwj"));
    const QString path2         = dir.filePath(QStringLiteral("b.zwj"));

    QVERIFY(! zwjio::save(original, path1).has_value());

    const zwjio::LoadResult loaded = zwjio::load(path1);
    QVERIFY2(! loaded.error.has_value(), qPrintable(loaded.error.value_or(zwjio::Error{}).message));
    QVERIFY(loaded.document.has_value());
    QCOMPARE(*loaded.document, original);  // full deep equality, not just bytes

    QVERIFY(! zwjio::save(*loaded.document, path2).has_value());

    QFile file1(path1);
    QFile file2(path2);
    QVERIFY(file1.open(QIODevice::ReadOnly));
    QVERIFY(file2.open(QIODevice::ReadOnly));
    const QByteArray saved = file1.readAll();
    QVERIFY(! saved.contains("display_unit"));
    QCOMPARE(saved, file2.readAll());
}

void TestZwjIo::rejectsWrongFormat() {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("bad.zwj"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"format": "something-else", "version": 1, "name": "", "description": "",
                   "sample_rate": 1000.0, "display_unit": "V", "layers": []})");
    file.close();

    const zwjio::LoadResult result = zwjio::load(path);
    QVERIFY(! result.document.has_value());
    QVERIFY(result.error.has_value());
    QVERIFY(result.error->message.contains(QStringLiteral("format")));
}

void TestZwjIo::rejectsTooNewVersion() {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("bad.zwj"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"format": "zahner-wave", "version": 999, "name": "", "description": "",
                   "sample_rate": 1000.0, "display_unit": "V", "layers": []})");
    file.close();

    const zwjio::LoadResult result = zwjio::load(path);
    QVERIFY(! result.document.has_value());
    QVERIFY(result.error.has_value());
    QVERIFY(result.error->message.contains(QStringLiteral("version")));
}

void TestZwjIo::rejectsUnknownSegmentType() {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("bad.zwj"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"format": "zahner-wave", "version": 1, "name": "", "description": "",
                   "sample_rate": 1000.0, "display_unit": "V", "layers": [
                       {"name": "L", "enabled": true, "mode": "add", "segments": [
                           {"type": "bogus", "duration": 1.0, "repeat": 1, "params": {}}
                       ]}
                   ]})");
    file.close();

    const zwjio::LoadResult result = zwjio::load(path);
    QVERIFY(! result.document.has_value());
    QVERIFY(result.error.has_value());
    QVERIFY(result.error->message.contains(QStringLiteral("layers[0].segments[0].type")));
}

void TestZwjIo::rejectsNonFiniteNumbers() {
    // A literal large enough to overflow a double (here the sample rate) must
    // not be silently accepted. Qt's JSON tokenizer already refuses a numeric
    // token that would overflow to +-inf and never hands us a non-finite
    // QJsonValue for a top-level number, so this hits the "invalid JSON" path
    // rather than the std::isfinite() guard in readNumber(). Either way the
    // file has to come back rejected with a real error.
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("bad.zwj"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"format": "zahner-wave", "version": 1, "name": "", "description": "",
                   "sample_rate": 1e309, "display_unit": "V", "layers": []})");
    file.close();

    const zwjio::LoadResult result = zwjio::load(path);
    QVERIFY(! result.document.has_value());
    QVERIFY(result.error.has_value());
    QVERIFY(! result.error->message.isEmpty());
}

void TestZwjIo::rejectsMissingRequiredField() {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("bad.zwj"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"format": "zahner-wave", "version": 1, "name": "", "description": "",
                   "display_unit": "V", "layers": []})");  // sample_rate missing
    file.close();

    const zwjio::LoadResult result = zwjio::load(path);
    QVERIFY(! result.document.has_value());
    QVERIFY(result.error.has_value());
    QVERIFY(result.error->message.contains(QStringLiteral("sample_rate")));
}

void TestZwjIo::ignoresUnknownExtraKeys() {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("extra.zwj"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"format": "zahner-wave", "version": 1, "name": "N", "description": "",
                   "sample_rate": 1000.0, "display_unit": "V", "future_field": 42, "layers": [
                       {"name": "L", "enabled": true, "mode": "add", "unexpected": "x", "segments": [
                           {"type": "dc", "duration": 1.0, "repeat": 1,
                            "params": {"value": 0.5, "extra_param": 1}}
                       ]}
                   ]})");
    file.close();

    const zwjio::LoadResult result = zwjio::load(path);
    QVERIFY2(! result.error.has_value(), qPrintable(result.error.value_or(zwjio::Error{}).message));
    QVERIFY(result.document.has_value());
    QCOMPARE(result.document->name, QStringLiteral("N"));
    QCOMPARE(std::get<DcParams>(result.document->layers[0].segments[0].params).value, 0.5);
}

void TestZwjIo::defaultsMissingExportFormatToCsvAndRejectsUnknownOnes() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Files written before the binary export existed carry no export.format
    // and always meant CSV.
    const QString legacyPath = dir.filePath(QStringLiteral("legacy.zwj"));
    QFile legacy(legacyPath);
    QVERIFY(legacy.open(QIODevice::WriteOnly));
    legacy.write(R"({"format": "zahner-wave", "version": 1, "name": "", "description": "",
                     "sample_rate": 1000.0, "export": {"last_csv_path": "out.csv",
                     "significant_digits": 5}, "layers": []})");
    legacy.close();

    const zwjio::LoadResult legacyResult = zwjio::load(legacyPath);
    QVERIFY2(
        ! legacyResult.error.has_value(),
        qPrintable(legacyResult.error.value_or(zwjio::Error{}).message)
    );
    QVERIFY(legacyResult.document.has_value());
    QCOMPARE(legacyResult.document->exportSettings.format, ExportFormat::Csv);
    QCOMPARE(legacyResult.document->exportSettings.lastExportPath, QStringLiteral("out.csv"));

    const QString badPath = dir.filePath(QStringLiteral("bad-format.zwj"));
    QFile bad(badPath);
    QVERIFY(bad.open(QIODevice::WriteOnly));
    bad.write(R"({"format": "zahner-wave", "version": 1, "name": "", "description": "",
                  "sample_rate": 1000.0, "export": {"format": "hdf5"}, "layers": []})");
    bad.close();

    const zwjio::LoadResult badResult = zwjio::load(badPath);
    QVERIFY(! badResult.document.has_value());
    QVERIFY(badResult.error.has_value());
    QVERIFY(badResult.error->message.contains(QStringLiteral("export.format")));
}

void TestZwjIo::rejectsUnknownMode() {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("bad.zwj"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"format": "zahner-wave", "version": 1, "name": "", "description": "",
                   "sample_rate": 1000.0, "display_unit": "V", "layers": [
                       {"name": "L", "enabled": true, "mode": "subtract", "segments": []}
                   ]})");
    file.close();

    const zwjio::LoadResult result = zwjio::load(path);
    QVERIFY(! result.document.has_value());
    QVERIFY(result.error.has_value());
    QVERIFY(result.error->message.contains(QStringLiteral("layers[0].mode")));
}

void TestZwjIo::rejectsOutOfRangeDuty() {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("bad.zwj"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"format": "zahner-wave", "version": 1, "name": "", "description": "",
                   "sample_rate": 1000.0, "display_unit": "V", "layers": [
                       {"name": "L", "enabled": true, "mode": "add", "segments": [
                           {"type": "square", "duration": 1.0, "repeat": 1, "params": {
                               "amplitude": 1.0, "frequency": 1.0, "phase_deg": 0.0,
                               "offset": 0.0, "duty": 1.5
                           }}
                       ]}
                   ]})");
    file.close();

    const zwjio::LoadResult result = zwjio::load(path);
    QVERIFY(! result.document.has_value());
    QVERIFY(result.error.has_value());
    QVERIFY(result.error->message.contains(QStringLiteral("duty")));
}

void TestZwjIo::rejectsInvalidAdvancedFrequencies() {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("bad.zwj"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"format":"zahner-wave","version":1,"name":"","description":"",
        "sample_rate":1000,"output_data_rate":1,"layers":[{"name":"L","enabled":true,
        "mode":"add","segments":[{"type":"chirp","duration":1,"repeat":1,"params":{
        "amplitude":1,"start_frequency":0,"end_frequency":2,"phase_deg":0,"offset":0,
        "sweep":"exponential"}}]}]})");
    file.close();
    auto result = zwjio::load(path);
    QVERIFY(result.error.has_value());
    QVERIFY(result.error->message.contains(QStringLiteral("frequencies")));

    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(R"({"format":"zahner-wave","version":1,"name":"","description":"",
        "sample_rate":1000,"output_data_rate":1,"layers":[{"name":"L","enabled":true,
        "mode":"add","segments":[{"type":"ricker","duration":1,"repeat":1,"params":{
        "amplitude":1,"center_frequency":0,"offset":0}}]}]})");
    file.close();
    result = zwjio::load(path);
    QVERIFY(result.error.has_value());
    QVERIFY(result.error->message.contains(QStringLiteral("center_frequency")));
}

void TestZwjIo::rejectsInvalidSweepParameters() {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("bad.zwj"));
    QFile file(path);

    // Every case writes one segment whose params are otherwise valid, so the
    // named field is the only reason the load can fail.
    const auto loadWith = [&](const QByteArray& type, const QByteArray& params) {
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(
            R"({"format":"zahner-wave","version":2,"name":"","description":"",
                "sample_rate":1000,"layers":[{"name":"L","enabled":true,"mode":"add",
                "segments":[{"type":")" +
            type + R"(","duration":1,"repeat":1,"params":)" + params + R"(}]}]})"
        );
        file.close();
    };

    // An npv segment without a baseline of its own is one written before
    // start_value came to mean the first pulse. Reading it would sample a
    // waveform its author never described, so it is refused instead.
    loadWith("npv", R"({"start_value":0.01,"end_value":0.5,"step_value":0.01,
                        "step_time":0.1,"pulse_time":0.05})");
    auto result = zwjio::load(path);
    QVERIFY(result.error.has_value());
    QVERIFY(result.error->message.contains(QStringLiteral("base_value")));

    // The scan direction comes from start and end, so a signed step is an error
    // rather than a downward sweep.
    loadWith("npv", R"({"base_value":0,"start_value":0,"end_value":0.5,"step_value":-0.01,
                        "step_time":0.1,"pulse_time":0.05})");
    result = zwjio::load(path);
    QVERIFY(result.error.has_value());
    QVERIFY(result.error->message.contains(QStringLiteral("step_value")));

    // A span shorter than a single step has no staircase to walk.
    loadWith("npv", R"({"base_value":0,"start_value":0,"end_value":0.005,"step_value":0.01,
                        "step_time":0.1,"pulse_time":0.05})");
    result = zwjio::load(path);
    QVERIFY(result.error.has_value());
    QVERIFY(result.error->message.contains(QStringLiteral("at least one step_value")));

    // And a vanishing step asks for more steps than the editor will walk.
    loadWith("npv", R"({"base_value":0,"start_value":0,"end_value":1,"step_value":1e-9,
                        "step_time":0.1,"pulse_time":0.05})");
    result = zwjio::load(path);
    QVERIFY(result.error.has_value());
    QVERIFY(result.error->message.contains(QStringLiteral("steps")));

    // A baseline anywhere against the pulse train is legal, including one the
    // pulses walk towards rather than away from.
    loadWith("npv", R"({"base_value":0,"start_value":0.5,"end_value":0.01,"step_value":0.01,
                        "step_time":0.1,"pulse_time":0.05})");
    result = zwjio::load(path);
    QVERIFY2(! result.error.has_value(), qPrintable(result.error.value_or(zwjio::Error{}).message));

    // A pulse filling its step is allowed, one outlasting it is not - there
    // would be no base level left to return to.
    loadWith("dpv", R"({"start_value":0,"end_value":0.5,"step_value":0.01,
                        "pulse_value":0.05,"step_time":0.1,"pulse_time":0.1,
                        "invert_pulse":false})");
    result = zwjio::load(path);
    QVERIFY2(! result.error.has_value(), qPrintable(result.error.value_or(zwjio::Error{}).message));

    loadWith("dpv", R"({"start_value":0,"end_value":0.5,"step_value":0.01,
                        "pulse_value":0.05,"step_time":0.1,"pulse_time":0.2,
                        "invert_pulse":false})");
    result = zwjio::load(path);
    QVERIFY(result.error.has_value());
    QVERIFY(result.error->message.contains(QStringLiteral("pulse_time")));

    loadWith("dpv", R"({"start_value":0,"end_value":0.5,"step_value":0.01,
                        "pulse_value":0,"step_time":0.1,"pulse_time":0.05,
                        "invert_pulse":false})");
    result = zwjio::load(path);
    QVERIFY(result.error.has_value());
    QVERIFY(result.error->message.contains(QStringLiteral("pulse_value")));

    loadWith("swv", R"({"start_value":0,"end_value":0.5,"step_value":0.01,
                        "amplitude":0,"period":0.1})");
    result = zwjio::load(path);
    QVERIFY(result.error.has_value());
    QVERIFY(result.error->message.contains(QStringLiteral("amplitude")));

    loadWith("swv", R"({"start_value":0,"end_value":0.5,"step_value":0.01,
                        "amplitude":0.02,"period":0})");
    result = zwjio::load(path);
    QVERIFY(result.error.has_value());
    QVERIFY(result.error->message.contains(QStringLiteral("period")));

    // A duration that is not the sweep's own is legal: it cuts the sweep short
    // or holds its last level, which is what every segment type does.
    loadWith("swv", R"({"start_value":0,"end_value":0.5,"step_value":0.01,
                        "amplitude":0.02,"period":0.1})");
    result = zwjio::load(path);
    QVERIFY2(! result.error.has_value(), qPrintable(result.error.value_or(zwjio::Error{}).message));
    QCOMPARE(result.document->layers[0].segments[0].duration, 1.0);
}

void TestZwjIo::versionOneFilesLoadAsAdditiveLeafLayers() {
    // Version 1 knew neither kind nor edge. Its layers held segments and
    // contributed nothing outside their duration, which is exactly a leaf with
    // the neutral edge under LayerMode::Add.
    const zwjio::LoadResult result = zwjio::load(testDataPath(QStringLiteral("example.zwj")));
    QVERIFY2(! result.error.has_value(), qPrintable(result.error.value_or(zwjio::Error{}).message));
    for (const WaveLayer& layer : result.document->layers) {
        QCOMPARE(layer.kind, LayerKind::Leaf);
        QCOMPARE(layer.mode, LayerMode::Add);
        QCOMPARE(layer.edge, LayerEdge::Neutral);
        QVERIFY(layer.children.empty());
    }

    // Saving such a document writes the current version - the reader of an
    // older build rejects it, which is better than it silently ignoring a
    // multiply or an npv baseline it does not know about.
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("current.zwj"));
    QVERIFY(! zwjio::save(*result.document, path).has_value());
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray saved = file.readAll();
    QVERIFY(saved.contains("\"version\": 3"));
    QVERIFY(saved.contains("\"kind\": \"layer\""));
    QVERIFY(saved.contains("\"edge\": \"neutral\""));
}

void TestZwjIo::readsLayerModeEdgeAndNestedChildren() {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("nested.zwj"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"format":"zahner-wave","version":2,"name":"","description":"",
        "sample_rate":1000,"layers":[
            {"name":"Carrier","enabled":true,"mode":"add","segments":[
                {"type":"dc","duration":1,"repeat":1,"params":{"value":1}}]},
            {"name":"Group","enabled":true,"mode":"multiply","edge":"hold","children":[
                {"name":"Child","enabled":false,"mode":"multiply","edge":"loop","kind":"layer",
                 "segments":[{"type":"window","duration":1,"repeat":1,
                              "params":{"shape":"trapezoid","amplitude":1,"offset":0,
                                        "rise":0.25,"fall":0.5}}]}]},
            {"name":"Bare group","enabled":true,"mode":"add","kind":"group"}
        ]})");
    file.close();

    const zwjio::LoadResult result = zwjio::load(path);
    QVERIFY2(! result.error.has_value(), qPrintable(result.error.value_or(zwjio::Error{}).message));
    const WaveDocument& document = *result.document;
    QCOMPARE(document.layers.size(), size_t{3});
    QCOMPARE(document.layers[0].kind, LayerKind::Leaf);

    // A layer with children is a group even without an explicit kind.
    const WaveLayer& group = document.layers[1];
    QCOMPARE(group.kind, LayerKind::Group);
    QCOMPARE(group.mode, LayerMode::Multiply);
    QCOMPARE(group.edge, LayerEdge::HoldLast);
    QCOMPARE(group.children.size(), size_t{1});
    QVERIFY(group.segments.empty());

    const WaveLayer& child = group.children[0];
    QCOMPARE(child.name, QStringLiteral("Child"));
    QVERIFY(! child.enabled);
    QCOMPARE(child.mode, LayerMode::Multiply);
    QCOMPARE(child.edge, LayerEdge::Loop);
    QCOMPARE(child.segments.size(), size_t{1});
    const auto& window = std::get<WindowParams>(child.segments[0].params);
    QCOMPARE(window.shape, WindowParams::Shape::Trapezoid);
    QCOMPARE(window.rise, 0.25);
    QCOMPARE(window.fall, 0.5);
    // The shape parameters a file leaves out keep their defaults.
    QCOMPARE(window.alpha, WindowParams{}.alpha);
    QCOMPARE(window.beta, WindowParams{}.beta);

    // "kind": "group" without children is the empty group one just added.
    QCOMPARE(document.layers[2].kind, LayerKind::Group);
    QVERIFY(document.layers[2].children.empty());

    // The path helpers reach the nested layer, and it round-trips unchanged.
    QCOMPARE(layerAtPath(document.layers, {1, 0})->name, QStringLiteral("Child"));
    const QString again = dir.filePath(QStringLiteral("again.zwj"));
    QVERIFY(! zwjio::save(document, again).has_value());
    const zwjio::LoadResult reloaded = zwjio::load(again);
    QVERIFY(reloaded.document.has_value());
    QCOMPARE(*reloaded.document, document);
}

void TestZwjIo::rejectsBrokenLayerFields() {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("bad.zwj"));
    QFile file(path);

    // Returns the rejection message, or a placeholder that matches none of the
    // expected fragments when the file was accepted after all.
    const auto errorFor = [&](const QByteArray& json) -> QString {
        if (! file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return QStringLiteral("<cannot write>");
        }
        file.write(json);
        file.close();
        const zwjio::LoadResult result = zwjio::load(path);
        if (result.document.has_value() || ! result.error.has_value()) {
            return QStringLiteral("<accepted>");
        }
        return result.error->message;
    };
    const auto head = QByteArray(
        R"({"format":"zahner-wave","version":2,"name":"","description":"","sample_rate":1000,)"
    );

    QVERIFY(errorFor(head + R"("layers":[{"name":"L","enabled":true,"mode":"add",
        "edge":"wrap","segments":[]}]})")
                .contains(QStringLiteral("layers[0].edge")));
    QVERIFY(errorFor(head + R"("layers":[{"name":"L","enabled":true,"mode":"add",
        "kind":"folder","segments":[]}]})")
                .contains(QStringLiteral("layers[0].kind")));
    // A group holds children, not segments; saying both is a mistake worth
    // naming rather than silently dropping one of them.
    QVERIFY(errorFor(head + R"("layers":[{"name":"G","enabled":true,"mode":"add",
        "kind":"group","segments":[{"type":"dc","duration":1,"repeat":1,"params":{"value":0}}]}]})")
                .contains(QStringLiteral("layers[0].segments")));
    QVERIFY(errorFor(head + R"("layers":[{"name":"G","enabled":true,"mode":"add",
        "children":{"nope":1}}]})")
                .contains(QStringLiteral("layers[0].children")));
    // A missing field inside a nested child names the full path.
    QVERIFY(errorFor(head + R"("layers":[{"name":"G","enabled":true,"mode":"add","children":[
        {"name":"C","enabled":true,"mode":"add","segments":[
            {"type":"dc","duration":1,"repeat":1,"params":{}}]}]}]})")
                .contains(QStringLiteral("layers[0].children[0].segments[0].params.value")));

    // Nesting has a limit, so a generated or hand-edited file cannot recurse
    // the reader to death.
    QByteArray deep;
    constexpr int depth = 40;
    for (int level = 0; level < depth; ++level) {
        deep += R"({"name":"G","enabled":true,"mode":"add","children":[)";
    }
    deep += R"({"name":"L","enabled":true,"mode":"add","segments":[]})";
    for (int level = 0; level < depth; ++level) {
        deep += "]}";
    }
    QVERIFY(errorFor(head + R"("layers":[)" + deep + "]}").contains(QStringLiteral("nested")));
}

void TestZwjIo::rejectsBrokenWindowParams() {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("bad.zwj"));
    QFile file(path);

    const auto errorFor = [&](const QByteArray& params) -> QString {
        if (! file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return QStringLiteral("<cannot write>");
        }
        file.write(
            R"({"format":"zahner-wave","version":2,"name":"","description":"","sample_rate":1000,
                "layers":[{"name":"L","enabled":true,"mode":"add","segments":[
                    {"type":"window","duration":1,"repeat":1,"params":)" +
            params + R"(}]}]})"
        );
        file.close();
        const zwjio::LoadResult result = zwjio::load(path);
        if (result.document.has_value() || ! result.error.has_value()) {
            return QStringLiteral("<accepted>");
        }
        return result.error->message;
    };

    QVERIFY(errorFor(R"({"shape":"bartlett","amplitude":1,"offset":0})")
                .contains(QStringLiteral("shape")));
    QVERIFY(errorFor(R"({"shape":"hann","offset":0})").contains(QStringLiteral("amplitude")));
    QVERIFY(errorFor(R"({"shape":"tukey","amplitude":1,"offset":0,"alpha":1.5})")
                .contains(QStringLiteral("alpha")));
    QVERIFY(errorFor(R"({"shape":"gauss","amplitude":1,"offset":0,"sigma":0})")
                .contains(QStringLiteral("sigma")));
    QVERIFY(errorFor(R"({"shape":"kaiser","amplitude":1,"offset":0,"beta":-1})")
                .contains(QStringLiteral("beta")));
    QVERIFY(errorFor(R"({"shape":"trapezoid","amplitude":1,"offset":0,"rise":-0.1})")
                .contains(QStringLiteral("rise")));

    // A rise and fall longer than the segment is accepted, not rejected:
    // dragging a boundary shorter leaves them alone, and a document that
    // cannot be reloaded would be the worse outcome. The window scales them.
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(
        R"({"format":"zahner-wave","version":2,"name":"","description":"","sample_rate":1000,
            "layers":[{"name":"L","enabled":true,"mode":"add","segments":[
                {"type":"window","duration":1,"repeat":1,"params":{"shape":"trapezoid",
                 "amplitude":1,"offset":0,"rise":3,"fall":1}}]}]})"
    );
    file.close();
    const zwjio::LoadResult result = zwjio::load(path);
    QVERIFY2(! result.error.has_value(), qPrintable(result.error.value_or(zwjio::Error{}).message));
}

QTEST_GUILESS_MAIN(TestZwjIo)
#include "tst_zwjio.moc"
