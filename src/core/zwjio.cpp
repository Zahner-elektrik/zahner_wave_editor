// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "zwjio.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <cmath>
#include <type_traits>
#include <utility>

#include "pointinterpolation.h"

namespace zwe::zwjio {

namespace {

// Version 2 introduced layer groups, the layer modes and edge policies, and the
// window segment. Every field it added is optional with the default that
// reproduces version 1, so a version-1 file still loads unchanged.
//
// Version 3 gave the npv segment a baseline of its own, base_value, and with it
// a new reading of start_value: the first pulse rather than the level between
// the pulses. No default reproduces the old meaning, so base_value is required
// and an npv segment written before it is refused rather than sampled as a
// waveform nobody described. The version number is what stops the reverse - an
// older build dropping base_value and doing exactly that.
constexpr int kFormatVersion = 3;
// A group may nest arbitrarily deep as far as the editor is concerned, but a
// hand-edited or generated file must not be able to recurse the reader to death.
constexpr int kMaxLayerDepth = 32;
const QString kFormatMagic   = QStringLiteral("zahner-wave");

QString joinPath(const QString& parentPath, const QString& key) {
    return parentPath.isEmpty() ? key : parentPath + QStringLiteral(".") + key;
}

Error errorAt(const QString& path, const QString& reason) {
    return Error{QStringLiteral("%1: %2").arg(path, reason)};
}

// ---- low-level JSON field readers, each naming its own JSON path on error ----

bool readNumber(
    const QJsonObject& obj, const QString& key, const QString& parentPath, double& out, Error& error
) {
    const QString fieldPath = joinPath(parentPath, key);
    if (! obj.contains(key)) {
        error = errorAt(fieldPath, QStringLiteral("missing required field"));
        return false;
    }
    const QJsonValue value = obj.value(key);
    if (! value.isDouble()) {
        error = errorAt(fieldPath, QStringLiteral("must be a number"));
        return false;
    }
    const double number = value.toDouble();
    if (! std::isfinite(number)) {
        error = errorAt(fieldPath, QStringLiteral("must be finite"));
        return false;
    }
    out = number;
    return true;
}

bool readInt(
    const QJsonObject& obj, const QString& key, const QString& parentPath, int& out, Error& error
) {
    double number = 0.0;
    if (! readNumber(obj, key, parentPath, number, error)) {
        return false;
    }
    out = static_cast<int>(std::llround(number));
    return true;
}

// A number that may be absent: out keeps its default then. Used for fields
// added after version 1 and for the shape parameters of a window, where only
// one of them is ever meaningful at a time.
bool readOptionalNumber(
    const QJsonObject& obj, const QString& key, const QString& parentPath, double& out, Error& error
) {
    if (! obj.contains(key)) {
        return true;
    }
    return readNumber(obj, key, parentPath, out, error);
}

bool readString(
    const QJsonObject& obj,
    const QString& key,
    const QString& parentPath,
    QString& out,
    Error& error
) {
    const QString fieldPath = joinPath(parentPath, key);
    if (! obj.contains(key)) {
        error = errorAt(fieldPath, QStringLiteral("missing required field"));
        return false;
    }
    const QJsonValue value = obj.value(key);
    if (! value.isString()) {
        error = errorAt(fieldPath, QStringLiteral("must be a string"));
        return false;
    }
    out = value.toString();
    return true;
}

bool readBool(
    const QJsonObject& obj, const QString& key, const QString& parentPath, bool& out, Error& error
) {
    const QString fieldPath = joinPath(parentPath, key);
    if (! obj.contains(key)) {
        error = errorAt(fieldPath, QStringLiteral("missing required field"));
        return false;
    }
    const QJsonValue value = obj.value(key);
    if (! value.isBool()) {
        error = errorAt(fieldPath, QStringLiteral("must be a boolean"));
        return false;
    }
    out = value.toBool();
    return true;
}

// ---- SegmentType <-> JSON "type" string ----

QString segmentTypeToString(SegmentType type) {
    switch (type) {
        case SegmentType::Dc:
            return QStringLiteral("dc");
        case SegmentType::Ramp:
            return QStringLiteral("ramp");
        case SegmentType::Sine:
            return QStringLiteral("sine");
        case SegmentType::Square:
            return QStringLiteral("square");
        case SegmentType::Triangle:
            return QStringLiteral("triangle");
        case SegmentType::Pulse:
            return QStringLiteral("pulse");
        case SegmentType::Exponential:
            return QStringLiteral("exponential");
        case SegmentType::Formula:
            return QStringLiteral("formula");
        case SegmentType::Points:
            return QStringLiteral("points");
        case SegmentType::Chirp:
            return QStringLiteral("chirp");
        case SegmentType::Ricker:
            return QStringLiteral("ricker");
        case SegmentType::Window:
            return QStringLiteral("window");
        // The three voltammetry types keep the short names their IM7 jobs and
        // their JSON parameters are known by.
        case SegmentType::Npv:
            return QStringLiteral("npv");
        case SegmentType::Swv:
            return QStringLiteral("swv");
        case SegmentType::Dpv:
            return QStringLiteral("dpv");
    }
    return QString();  // unreachable
}

std::optional<SegmentType> segmentTypeFromString(const QString& s) {
    if (s == QStringLiteral("dc"))
        return SegmentType::Dc;
    if (s == QStringLiteral("ramp"))
        return SegmentType::Ramp;
    if (s == QStringLiteral("sine"))
        return SegmentType::Sine;
    if (s == QStringLiteral("square"))
        return SegmentType::Square;
    if (s == QStringLiteral("triangle"))
        return SegmentType::Triangle;
    if (s == QStringLiteral("pulse"))
        return SegmentType::Pulse;
    if (s == QStringLiteral("exponential"))
        return SegmentType::Exponential;
    if (s == QStringLiteral("formula"))
        return SegmentType::Formula;
    if (s == QStringLiteral("points"))
        return SegmentType::Points;
    if (s == QStringLiteral("chirp"))
        return SegmentType::Chirp;
    if (s == QStringLiteral("ricker"))
        return SegmentType::Ricker;
    if (s == QStringLiteral("window"))
        return SegmentType::Window;
    if (s == QStringLiteral("npv"))
        return SegmentType::Npv;
    if (s == QStringLiteral("swv"))
        return SegmentType::Swv;
    if (s == QStringLiteral("dpv"))
        return SegmentType::Dpv;
    return std::nullopt;
}

// ---- LayerMode / LayerEdge / LayerKind <-> JSON strings ----

QString layerModeToString(LayerMode mode) {
    switch (mode) {
        case LayerMode::Add:
            return QStringLiteral("add");
        case LayerMode::Multiply:
            return QStringLiteral("multiply");
    }
    return QString();  // unreachable
}

std::optional<LayerMode> layerModeFromString(const QString& s) {
    if (s == QStringLiteral("add"))
        return LayerMode::Add;
    if (s == QStringLiteral("multiply"))
        return LayerMode::Multiply;
    return std::nullopt;
}

QString layerEdgeToString(LayerEdge edge) {
    switch (edge) {
        case LayerEdge::Neutral:
            return QStringLiteral("neutral");
        case LayerEdge::Zero:
            return QStringLiteral("zero");
        case LayerEdge::HoldLast:
            return QStringLiteral("hold");
        case LayerEdge::Loop:
            return QStringLiteral("loop");
    }
    return QString();  // unreachable
}

std::optional<LayerEdge> layerEdgeFromString(const QString& s) {
    if (s == QStringLiteral("neutral"))
        return LayerEdge::Neutral;
    if (s == QStringLiteral("zero"))
        return LayerEdge::Zero;
    if (s == QStringLiteral("hold"))
        return LayerEdge::HoldLast;
    if (s == QStringLiteral("loop"))
        return LayerEdge::Loop;
    return std::nullopt;
}

QString layerKindToString(LayerKind kind) {
    return kind == LayerKind::Group ? QStringLiteral("group") : QStringLiteral("layer");
}

std::optional<LayerKind> layerKindFromString(const QString& s) {
    if (s == QStringLiteral("layer"))
        return LayerKind::Leaf;
    if (s == QStringLiteral("group"))
        return LayerKind::Group;
    return std::nullopt;
}

// ---- ExportFormat <-> JSON "export.format" string ----

QString exportFormatToString(ExportFormat format) {
    switch (format) {
        case ExportFormat::Csv:
            return QStringLiteral("csv");
        case ExportFormat::Binary:
            return QStringLiteral("binary");
    }
    return QString();  // unreachable
}

std::optional<ExportFormat> exportFormatFromString(const QString& s) {
    if (s == QStringLiteral("csv"))
        return ExportFormat::Csv;
    if (s == QStringLiteral("binary"))
        return ExportFormat::Binary;
    return std::nullopt;
}

// ---- per-type params readers ----

bool readDcParams(const QJsonObject& obj, const QString& path, DcParams& out, Error& error) {
    return readNumber(obj, QStringLiteral("value"), path, out.value, error);
}

bool readRampParams(const QJsonObject& obj, const QString& path, RampParams& out, Error& error) {
    if (! readNumber(obj, QStringLiteral("start_value"), path, out.startValue, error))
        return false;
    if (! readNumber(obj, QStringLiteral("end_value"), path, out.endValue, error))
        return false;
    return true;
}

bool readSineLikeCommon(
    const QJsonObject& obj,
    const QString& path,
    double& amplitude,
    double& frequency,
    double& phaseDeg,
    double& offset,
    Error& error
) {
    if (! readNumber(obj, QStringLiteral("amplitude"), path, amplitude, error))
        return false;
    if (! readNumber(obj, QStringLiteral("frequency"), path, frequency, error))
        return false;
    if (! readNumber(obj, QStringLiteral("phase_deg"), path, phaseDeg, error))
        return false;
    if (! readNumber(obj, QStringLiteral("offset"), path, offset, error))
        return false;
    return true;
}

bool readSineParams(const QJsonObject& obj, const QString& path, SineParams& out, Error& error) {
    return readSineLikeCommon(
        obj, path, out.amplitude, out.frequency, out.phaseDeg, out.offset, error
    );
}

bool readSquareParams(
    const QJsonObject& obj, const QString& path, SquareParams& out, Error& error
) {
    if (! readSineLikeCommon(
            obj, path, out.amplitude, out.frequency, out.phaseDeg, out.offset, error
        ))
        return false;
    if (! readNumber(obj, QStringLiteral("duty"), path, out.duty, error))
        return false;
    if (! (out.duty > 0.0 && out.duty < 1.0)) {
        error =
            errorAt(joinPath(path, QStringLiteral("duty")), QStringLiteral("must be in (0, 1)"));
        return false;
    }
    return true;
}

bool readTriangleParams(
    const QJsonObject& obj, const QString& path, TriangleParams& out, Error& error
) {
    if (! readSineLikeCommon(
            obj, path, out.amplitude, out.frequency, out.phaseDeg, out.offset, error
        ))
        return false;
    if (! readNumber(obj, QStringLiteral("symmetry"), path, out.symmetry, error))
        return false;
    if (! (out.symmetry >= 0.0 && out.symmetry <= 1.0)) {
        error = errorAt(
            joinPath(path, QStringLiteral("symmetry")), QStringLiteral("must be in [0, 1]")
        );
        return false;
    }
    return true;
}

bool readPulseParams(
    const QJsonObject& obj, const QString& path, double duration, PulseParams& out, Error& error
) {
    if (! readNumber(obj, QStringLiteral("base_value"), path, out.baseValue, error))
        return false;
    if (! readNumber(obj, QStringLiteral("pulse_value"), path, out.pulseValue, error))
        return false;
    if (! readNumber(obj, QStringLiteral("delay"), path, out.delay, error))
        return false;
    if (! readNumber(obj, QStringLiteral("width"), path, out.width, error))
        return false;
    if (! (out.delay >= 0.0)) {
        error = errorAt(joinPath(path, QStringLiteral("delay")), QStringLiteral("must be >= 0"));
        return false;
    }
    if (! (out.width > 0.0)) {
        error = errorAt(joinPath(path, QStringLiteral("width")), QStringLiteral("must be > 0"));
        return false;
    }
    if (! (out.delay + out.width <= duration)) {
        error = errorAt(
            path,
            QStringLiteral("delay + width (%1) must be <= duration (%2)")
                .arg(out.delay + out.width)
                .arg(duration)
        );
        return false;
    }
    return true;
}

bool readExponentialParams(
    const QJsonObject& obj, const QString& path, ExponentialParams& out, Error& error
) {
    if (! readNumber(obj, QStringLiteral("start_value"), path, out.startValue, error))
        return false;
    if (! readNumber(obj, QStringLiteral("end_value"), path, out.endValue, error))
        return false;
    if (! readNumber(obj, QStringLiteral("time_constant"), path, out.timeConstant, error))
        return false;
    if (! (out.timeConstant > 0.0)) {
        error =
            errorAt(joinPath(path, QStringLiteral("time_constant")), QStringLiteral("must be > 0"));
        return false;
    }
    return true;
}

bool readChirpParams(const QJsonObject& obj, const QString& path, ChirpParams& out, Error& error) {
    if (! readNumber(obj, QStringLiteral("amplitude"), path, out.amplitude, error) ||
        ! readNumber(obj, QStringLiteral("start_frequency"), path, out.startFrequency, error) ||
        ! readNumber(obj, QStringLiteral("end_frequency"), path, out.endFrequency, error) ||
        ! readNumber(obj, QStringLiteral("phase_deg"), path, out.phaseDeg, error) ||
        ! readNumber(obj, QStringLiteral("offset"), path, out.offset, error)) {
        return false;
    }
    QString sweep;
    if (! readString(obj, QStringLiteral("sweep"), path, sweep, error))
        return false;
    if (sweep == QStringLiteral("linear")) {
        out.sweep = ChirpParams::Sweep::Linear;
        if (out.startFrequency < 0.0 || out.endFrequency < 0.0) {
            error = errorAt(path, QStringLiteral("linear sweep frequencies must be >= 0"));
            return false;
        }
    } else if (sweep == QStringLiteral("exponential")) {
        out.sweep = ChirpParams::Sweep::Exponential;
        if (! (out.startFrequency > 0.0 && out.endFrequency > 0.0)) {
            error = errorAt(path, QStringLiteral("exponential sweep frequencies must be > 0"));
            return false;
        }
    } else {
        error = errorAt(
            joinPath(path, QStringLiteral("sweep")),
            QStringLiteral("must be \"linear\" or \"exponential\"")
        );
        return false;
    }
    return true;
}

bool readRickerParams(
    const QJsonObject& obj, const QString& path, RickerParams& out, Error& error
) {
    if (! readNumber(obj, QStringLiteral("amplitude"), path, out.amplitude, error) ||
        ! readNumber(obj, QStringLiteral("center_frequency"), path, out.centerFrequency, error) ||
        ! readNumber(obj, QStringLiteral("offset"), path, out.offset, error)) {
        return false;
    }
    if (! (out.centerFrequency > 0.0)) {
        error = errorAt(
            joinPath(path, QStringLiteral("center_frequency")), QStringLiteral("must be > 0")
        );
        return false;
    }
    return true;
}

// The staircase all three voltammetry types are built on, with the constraint
// they share: a positive step magnitude, and a span that holds at least one
// step and no more steps than the editor will walk. The scan direction comes
// from start and end, so a negative step_value is a mistake rather than a
// cathodic sweep and is rejected as one.
bool readSweepStaircase(
    const QJsonObject& obj,
    const QString& path,
    double& startValue,
    double& endValue,
    double& stepValue,
    Error& error
) {
    if (! readNumber(obj, QStringLiteral("start_value"), path, startValue, error) ||
        ! readNumber(obj, QStringLiteral("end_value"), path, endValue, error) ||
        ! readNumber(obj, QStringLiteral("step_value"), path, stepValue, error)) {
        return false;
    }
    if (! (stepValue > 0.0)) {
        error =
            errorAt(joinPath(path, QStringLiteral("step_value")), QStringLiteral("must be > 0"));
        return false;
    }
    // The lower bound goes through the same helper the editor walks the sweep
    // with, so a file is accepted exactly when it describes a sweep. The upper
    // bound needs the raw quotient instead: sweepStepCount() would clamp it, and
    // silently sampling a different waveform than the file asks for is worse
    // than refusing to load it.
    const double span = std::abs(endValue - startValue);
    if (sweepStepCount(startValue, endValue, stepValue) == 0) {
        error = errorAt(
            path,
            QStringLiteral("|end_value - start_value| (%1) must be at least one step_value (%2)")
                .arg(span)
                .arg(stepValue)
        );
        return false;
    }
    const double quotient = span / stepValue;
    if (quotient > static_cast<double>(MaximumSweepSteps)) {
        error = errorAt(
            path,
            QStringLiteral("the sweep asks for %1 steps, at most %2 are supported")
                .arg(std::floor(quotient))
                .arg(static_cast<qulonglong>(MaximumSweepSteps))
        );
        return false;
    }
    return true;
}

// The timing NPV and DPV share. A pulse cannot outlast the step it opens: a
// step spent entirely on the pulse has no base level left to return to.
bool readSweepPulseTiming(
    const QJsonObject& obj, const QString& path, double& stepTime, double& pulseTime, Error& error
) {
    if (! readNumber(obj, QStringLiteral("step_time"), path, stepTime, error) ||
        ! readNumber(obj, QStringLiteral("pulse_time"), path, pulseTime, error)) {
        return false;
    }
    if (! (stepTime > 0.0)) {
        error = errorAt(joinPath(path, QStringLiteral("step_time")), QStringLiteral("must be > 0"));
        return false;
    }
    if (! (pulseTime > 0.0)) {
        error =
            errorAt(joinPath(path, QStringLiteral("pulse_time")), QStringLiteral("must be > 0"));
        return false;
    }
    if (! (pulseTime <= stepTime)) {
        error = errorAt(
            path,
            QStringLiteral("pulse_time (%1) must be <= step_time (%2)").arg(pulseTime).arg(stepTime)
        );
        return false;
    }
    return true;
}

// base_value is required rather than defaulted: a file written before it names
// the baseline in start_value, and reading start_value as the first pulse would
// turn such a sweep into a different waveform without a word.
bool readNpvParams(const QJsonObject& obj, const QString& path, NpvParams& out, Error& error) {
    return readNumber(obj, QStringLiteral("base_value"), path, out.baseValue, error) &&
           readSweepStaircase(obj, path, out.startValue, out.endValue, out.stepValue, error) &&
           readSweepPulseTiming(obj, path, out.stepTime, out.pulseTime, error);
}

bool readSwvParams(const QJsonObject& obj, const QString& path, SwvParams& out, Error& error) {
    if (! readSweepStaircase(obj, path, out.startValue, out.endValue, out.stepValue, error) ||
        ! readNumber(obj, QStringLiteral("amplitude"), path, out.amplitude, error) ||
        ! readNumber(obj, QStringLiteral("period"), path, out.period, error)) {
        return false;
    }
    if (! (out.amplitude > 0.0)) {
        error = errorAt(joinPath(path, QStringLiteral("amplitude")), QStringLiteral("must be > 0"));
        return false;
    }
    if (! (out.period > 0.0)) {
        error = errorAt(joinPath(path, QStringLiteral("period")), QStringLiteral("must be > 0"));
        return false;
    }
    return true;
}

bool readDpvParams(const QJsonObject& obj, const QString& path, DpvParams& out, Error& error) {
    if (! readSweepStaircase(obj, path, out.startValue, out.endValue, out.stepValue, error) ||
        ! readSweepPulseTiming(obj, path, out.stepTime, out.pulseTime, error) ||
        ! readNumber(obj, QStringLiteral("pulse_value"), path, out.pulseValue, error) ||
        ! readBool(obj, QStringLiteral("invert_pulse"), path, out.invertPulse, error)) {
        return false;
    }
    if (! (out.pulseValue > 0.0)) {
        error =
            errorAt(joinPath(path, QStringLiteral("pulse_value")), QStringLiteral("must be > 0"));
        return false;
    }
    return true;
}

QString windowShapeToString(WindowParams::Shape shape) {
    switch (shape) {
        case WindowParams::Shape::Rectangular:
            return QStringLiteral("rectangular");
        case WindowParams::Shape::Hann:
            return QStringLiteral("hann");
        case WindowParams::Shape::Hamming:
            return QStringLiteral("hamming");
        case WindowParams::Shape::Blackman:
            return QStringLiteral("blackman");
        case WindowParams::Shape::BlackmanHarris:
            return QStringLiteral("blackman-harris");
        case WindowParams::Shape::Tukey:
            return QStringLiteral("tukey");
        case WindowParams::Shape::Gauss:
            return QStringLiteral("gauss");
        case WindowParams::Shape::Kaiser:
            return QStringLiteral("kaiser");
        case WindowParams::Shape::Trapezoid:
            return QStringLiteral("trapezoid");
    }
    return QString();  // unreachable
}

std::optional<WindowParams::Shape> windowShapeFromString(const QString& s) {
    if (s == QStringLiteral("rectangular"))
        return WindowParams::Shape::Rectangular;
    if (s == QStringLiteral("hann"))
        return WindowParams::Shape::Hann;
    if (s == QStringLiteral("hamming"))
        return WindowParams::Shape::Hamming;
    if (s == QStringLiteral("blackman"))
        return WindowParams::Shape::Blackman;
    if (s == QStringLiteral("blackman-harris"))
        return WindowParams::Shape::BlackmanHarris;
    if (s == QStringLiteral("tukey"))
        return WindowParams::Shape::Tukey;
    if (s == QStringLiteral("gauss"))
        return WindowParams::Shape::Gauss;
    if (s == QStringLiteral("kaiser"))
        return WindowParams::Shape::Kaiser;
    if (s == QStringLiteral("trapezoid"))
        return WindowParams::Shape::Trapezoid;
    return std::nullopt;
}

bool readWindowParams(
    const QJsonObject& obj, const QString& path, WindowParams& out, Error& error
) {
    QString shape;
    if (! readString(obj, QStringLiteral("shape"), path, shape, error))
        return false;
    const std::optional<WindowParams::Shape> parsed = windowShapeFromString(shape);
    if (! parsed) {
        error = errorAt(
            joinPath(path, QStringLiteral("shape")),
            QStringLiteral("unknown window shape \"%1\"").arg(shape)
        );
        return false;
    }
    out.shape = *parsed;
    if (! readNumber(obj, QStringLiteral("amplitude"), path, out.amplitude, error))
        return false;
    if (! readNumber(obj, QStringLiteral("offset"), path, out.offset, error))
        return false;
    // Each shape reads one of these at most, so a file only has to carry the
    // one it uses.
    if (! readOptionalNumber(obj, QStringLiteral("alpha"), path, out.alpha, error) ||
        ! readOptionalNumber(obj, QStringLiteral("sigma"), path, out.sigma, error) ||
        ! readOptionalNumber(obj, QStringLiteral("beta"), path, out.beta, error) ||
        ! readOptionalNumber(obj, QStringLiteral("rise"), path, out.rise, error) ||
        ! readOptionalNumber(obj, QStringLiteral("fall"), path, out.fall, error)) {
        return false;
    }
    if (! (out.alpha >= 0.0 && out.alpha <= 1.0)) {
        error =
            errorAt(joinPath(path, QStringLiteral("alpha")), QStringLiteral("must be in [0, 1]"));
        return false;
    }
    if (! (out.sigma > 0.0)) {
        error = errorAt(joinPath(path, QStringLiteral("sigma")), QStringLiteral("must be > 0"));
        return false;
    }
    if (! (out.beta >= 0.0)) {
        error = errorAt(joinPath(path, QStringLiteral("beta")), QStringLiteral("must be >= 0"));
        return false;
    }
    // rise and fall are not checked against the duration: shortening a segment
    // on the canvas leaves them alone, and the window scales them down to what
    // fits rather than turning the document into one that cannot be reloaded.
    if (! (out.rise >= 0.0)) {
        error = errorAt(joinPath(path, QStringLiteral("rise")), QStringLiteral("must be >= 0"));
        return false;
    }
    if (! (out.fall >= 0.0)) {
        error = errorAt(joinPath(path, QStringLiteral("fall")), QStringLiteral("must be >= 0"));
        return false;
    }
    return true;
}

bool readFormulaParams(
    const QJsonObject& obj, const QString& path, FormulaParams& out, Error& error
) {
    if (! readString(obj, QStringLiteral("expression"), path, out.expression, error))
        return false;
    QString timeRef;
    if (! readString(obj, QStringLiteral("time_reference"), path, timeRef, error))
        return false;
    if (timeRef == QStringLiteral("local")) {
        out.globalTime = false;
    } else if (timeRef == QStringLiteral("global")) {
        out.globalTime = true;
    } else {
        error = errorAt(
            joinPath(path, QStringLiteral("time_reference")),
            QStringLiteral("must be \"local\" or \"global\", got \"%1\"").arg(timeRef)
        );
        return false;
    }
    return true;
}

bool readPointsParams(
    const QJsonObject& obj, const QString& path, double duration, PointsParams& out, Error& error
) {
    QString interpStr;
    if (! readString(obj, QStringLiteral("interpolation"), path, interpStr, error))
        return false;
    if (interpStr == QStringLiteral("linear")) {
        out.interp = PointsParams::Interp::Linear;
    } else if (interpStr == QStringLiteral("step")) {
        out.interp = PointsParams::Interp::Step;
    } else if (interpStr == QStringLiteral("pchip")) {
        out.interp = PointsParams::Interp::Pchip;
    } else {
        error = errorAt(
            joinPath(path, QStringLiteral("interpolation")),
            QStringLiteral("must be \"linear\", \"step\" or \"pchip\", got \"%1\"").arg(interpStr)
        );
        return false;
    }

    const QString pointsPath = joinPath(path, QStringLiteral("points"));
    if (! obj.contains(QStringLiteral("points")) ||
        ! obj.value(QStringLiteral("points")).isArray()) {
        error = errorAt(pointsPath, QStringLiteral("missing or not an array"));
        return false;
    }
    const QJsonArray pointsArray = obj.value(QStringLiteral("points")).toArray();

    std::vector<std::pair<double, double>> points;
    points.reserve(static_cast<size_t>(pointsArray.size()));
    for (qsizetype i = 0; i < pointsArray.size(); ++i) {
        const QString entryPath = QStringLiteral("%1[%2]").arg(pointsPath).arg(i);
        const QJsonValue entry  = pointsArray.at(i);
        if (! entry.isArray() || entry.toArray().size() != 2) {
            error = errorAt(entryPath, QStringLiteral("must be a [t, value] pair"));
            return false;
        }
        const QJsonArray pair = entry.toArray();
        if (! pair.at(0).isDouble() || ! pair.at(1).isDouble()) {
            error = errorAt(entryPath, QStringLiteral("t and value must be numbers"));
            return false;
        }
        const double t = pair.at(0).toDouble();
        const double v = pair.at(1).toDouble();
        if (! std::isfinite(t) || ! std::isfinite(v)) {
            error = errorAt(entryPath, QStringLiteral("t and value must be finite"));
            return false;
        }
        points.emplace_back(t, v);
    }

    if (! pointinterpolation::arePointsValid(points)) {
        error =
            errorAt(pointsPath, QStringLiteral("must have >= 2 points with strictly increasing t"));
        return false;
    }
    if (points.front().first < 0.0 || points.back().first > duration) {
        error =
            errorAt(pointsPath, QStringLiteral("t must be within [0, duration=%1]").arg(duration));
        return false;
    }

    out.points = std::move(points);
    return true;
}

bool readSegment(const QJsonValue& value, const QString& path, Segment& out, Error& error) {
    if (! value.isObject()) {
        error = errorAt(path, QStringLiteral("must be an object"));
        return false;
    }
    const QJsonObject obj = value.toObject();

    QString typeStr;
    if (! readString(obj, QStringLiteral("type"), path, typeStr, error))
        return false;
    const std::optional<SegmentType> type = segmentTypeFromString(typeStr);
    if (! type) {
        error = errorAt(
            joinPath(path, QStringLiteral("type")),
            QStringLiteral("unknown segment type \"%1\"").arg(typeStr)
        );
        return false;
    }

    double duration = 0.0;
    if (! readNumber(obj, QStringLiteral("duration"), path, duration, error))
        return false;
    if (! (duration > 0.0)) {
        error = errorAt(joinPath(path, QStringLiteral("duration")), QStringLiteral("must be > 0"));
        return false;
    }

    int repeat = 0;
    if (! readInt(obj, QStringLiteral("repeat"), path, repeat, error))
        return false;
    if (repeat < 1) {
        error = errorAt(joinPath(path, QStringLiteral("repeat")), QStringLiteral("must be >= 1"));
        return false;
    }

    const QString paramsPath = joinPath(path, QStringLiteral("params"));
    if (! obj.contains(QStringLiteral("params")) ||
        ! obj.value(QStringLiteral("params")).isObject()) {
        error = errorAt(paramsPath, QStringLiteral("missing or not an object"));
        return false;
    }
    const QJsonObject paramsObj = obj.value(QStringLiteral("params")).toObject();

    Segment segment;
    segment.duration = duration;
    segment.repeat   = repeat;

    switch (*type) {
        case SegmentType::Dc: {
            DcParams p;
            if (! readDcParams(paramsObj, paramsPath, p, error))
                return false;
            segment.params = p;
            break;
        }
        case SegmentType::Ramp: {
            RampParams p;
            if (! readRampParams(paramsObj, paramsPath, p, error))
                return false;
            segment.params = p;
            break;
        }
        case SegmentType::Sine: {
            SineParams p;
            if (! readSineParams(paramsObj, paramsPath, p, error))
                return false;
            segment.params = p;
            break;
        }
        case SegmentType::Square: {
            SquareParams p;
            if (! readSquareParams(paramsObj, paramsPath, p, error))
                return false;
            segment.params = p;
            break;
        }
        case SegmentType::Triangle: {
            TriangleParams p;
            if (! readTriangleParams(paramsObj, paramsPath, p, error))
                return false;
            segment.params = p;
            break;
        }
        case SegmentType::Pulse: {
            PulseParams p;
            if (! readPulseParams(paramsObj, paramsPath, duration, p, error))
                return false;
            segment.params = p;
            break;
        }
        case SegmentType::Exponential: {
            ExponentialParams p;
            if (! readExponentialParams(paramsObj, paramsPath, p, error))
                return false;
            segment.params = p;
            break;
        }
        case SegmentType::Formula: {
            FormulaParams p;
            if (! readFormulaParams(paramsObj, paramsPath, p, error))
                return false;
            segment.params = p;
            break;
        }
        case SegmentType::Points: {
            PointsParams p;
            if (! readPointsParams(paramsObj, paramsPath, duration, p, error))
                return false;
            segment.params = p;
            break;
        }
        case SegmentType::Chirp: {
            ChirpParams p;
            if (! readChirpParams(paramsObj, paramsPath, p, error))
                return false;
            segment.params = p;
            break;
        }
        case SegmentType::Ricker: {
            RickerParams p;
            if (! readRickerParams(paramsObj, paramsPath, p, error))
                return false;
            segment.params = p;
            break;
        }
        case SegmentType::Window: {
            WindowParams p;
            if (! readWindowParams(paramsObj, paramsPath, p, error))
                return false;
            segment.params = p;
            break;
        }
        case SegmentType::Npv: {
            NpvParams p;
            if (! readNpvParams(paramsObj, paramsPath, p, error))
                return false;
            segment.params = p;
            break;
        }
        case SegmentType::Swv: {
            SwvParams p;
            if (! readSwvParams(paramsObj, paramsPath, p, error))
                return false;
            segment.params = p;
            break;
        }
        case SegmentType::Dpv: {
            DpvParams p;
            if (! readDpvParams(paramsObj, paramsPath, p, error))
                return false;
            segment.params = p;
            break;
        }
    }

    out = segment;
    return true;
}

bool readLayer(
    const QJsonValue& value, const QString& path, int depth, WaveLayer& out, Error& error
) {
    if (depth > kMaxLayerDepth) {
        error = errorAt(
            path, QStringLiteral("groups nested deeper than %1 levels").arg(kMaxLayerDepth)
        );
        return false;
    }
    if (! value.isObject()) {
        error = errorAt(path, QStringLiteral("must be an object"));
        return false;
    }
    const QJsonObject obj = value.toObject();

    WaveLayer layer;
    if (! readString(obj, QStringLiteral("name"), path, layer.name, error))
        return false;
    if (! readBool(obj, QStringLiteral("enabled"), path, layer.enabled, error))
        return false;

    QString modeStr;
    if (! readString(obj, QStringLiteral("mode"), path, modeStr, error))
        return false;
    const std::optional<LayerMode> mode = layerModeFromString(modeStr);
    if (! mode) {
        error = errorAt(
            joinPath(path, QStringLiteral("mode")),
            QStringLiteral("unknown mode \"%1\"").arg(modeStr)
        );
        return false;
    }
    layer.mode = *mode;

    // Absent in version-1 files, where a layer was always additive and
    // contributed nothing outside its own duration - which is what "neutral"
    // amounts to under LayerMode::Add.
    if (obj.contains(QStringLiteral("edge"))) {
        QString edgeStr;
        if (! readString(obj, QStringLiteral("edge"), path, edgeStr, error))
            return false;
        const std::optional<LayerEdge> edge = layerEdgeFromString(edgeStr);
        if (! edge) {
            error = errorAt(
                joinPath(path, QStringLiteral("edge")),
                QStringLiteral("unknown edge policy \"%1\"").arg(edgeStr)
            );
            return false;
        }
        layer.edge = *edge;
    }

    // Version-1 files carry no kind: everything in them holds segments.
    if (obj.contains(QStringLiteral("kind"))) {
        QString kindStr;
        if (! readString(obj, QStringLiteral("kind"), path, kindStr, error))
            return false;
        const std::optional<LayerKind> kind = layerKindFromString(kindStr);
        if (! kind) {
            error = errorAt(
                joinPath(path, QStringLiteral("kind")),
                QStringLiteral("must be \"layer\" or \"group\", got \"%1\"").arg(kindStr)
            );
            return false;
        }
        layer.kind = *kind;
    } else if (obj.contains(QStringLiteral("children"))) {
        layer.kind = LayerKind::Group;
    }

    if (layer.kind == LayerKind::Group) {
        const QString childrenPath = joinPath(path, QStringLiteral("children"));
        if (obj.contains(QStringLiteral("segments")) &&
            ! obj.value(QStringLiteral("segments")).toArray().isEmpty()) {
            error = errorAt(
                joinPath(path, QStringLiteral("segments")),
                QStringLiteral("a group holds children, not segments")
            );
            return false;
        }
        // An empty group is a legitimate, freshly added one, so a missing
        // children array is not an error.
        if (obj.contains(QStringLiteral("children"))) {
            if (! obj.value(QStringLiteral("children")).isArray()) {
                error = errorAt(childrenPath, QStringLiteral("must be an array"));
                return false;
            }
            const QJsonArray childrenArray = obj.value(QStringLiteral("children")).toArray();
            layer.children.reserve(static_cast<size_t>(childrenArray.size()));
            for (qsizetype i = 0; i < childrenArray.size(); ++i) {
                WaveLayer child;
                const QString childPath = QStringLiteral("%1[%2]").arg(childrenPath).arg(i);
                if (! readLayer(childrenArray.at(i), childPath, depth + 1, child, error))
                    return false;
                layer.children.push_back(std::move(child));
            }
        }
        out = layer;
        return true;
    }

    const QString segmentsPath = joinPath(path, QStringLiteral("segments"));
    if (! obj.contains(QStringLiteral("segments")) ||
        ! obj.value(QStringLiteral("segments")).isArray()) {
        error = errorAt(segmentsPath, QStringLiteral("missing or not an array"));
        return false;
    }
    const QJsonArray segmentsArray = obj.value(QStringLiteral("segments")).toArray();
    layer.segments.reserve(static_cast<size_t>(segmentsArray.size()));
    for (qsizetype i = 0; i < segmentsArray.size(); ++i) {
        Segment segment;
        const QString segmentPath = QStringLiteral("%1[%2]").arg(segmentsPath).arg(i);
        if (! readSegment(segmentsArray.at(i), segmentPath, segment, error))
            return false;
        layer.segments.push_back(segment);
    }

    out = layer;
    return true;
}

// ---- per-type params writers (mirror the readers) ----

QJsonObject writeDcParams(const DcParams& p) {
    QJsonObject obj;
    obj[QStringLiteral("value")] = p.value;
    return obj;
}

QJsonObject writeRampParams(const RampParams& p) {
    QJsonObject obj;
    obj[QStringLiteral("start_value")] = p.startValue;
    obj[QStringLiteral("end_value")]   = p.endValue;
    return obj;
}

void writeSineLikeCommon(
    QJsonObject& obj, double amplitude, double frequency, double phaseDeg, double offset
) {
    obj[QStringLiteral("amplitude")] = amplitude;
    obj[QStringLiteral("frequency")] = frequency;
    obj[QStringLiteral("phase_deg")] = phaseDeg;
    obj[QStringLiteral("offset")]    = offset;
}

QJsonObject writeSineParams(const SineParams& p) {
    QJsonObject obj;
    writeSineLikeCommon(obj, p.amplitude, p.frequency, p.phaseDeg, p.offset);
    return obj;
}

QJsonObject writeSquareParams(const SquareParams& p) {
    QJsonObject obj;
    writeSineLikeCommon(obj, p.amplitude, p.frequency, p.phaseDeg, p.offset);
    obj[QStringLiteral("duty")] = p.duty;
    return obj;
}

QJsonObject writeTriangleParams(const TriangleParams& p) {
    QJsonObject obj;
    writeSineLikeCommon(obj, p.amplitude, p.frequency, p.phaseDeg, p.offset);
    obj[QStringLiteral("symmetry")] = p.symmetry;
    return obj;
}

QJsonObject writePulseParams(const PulseParams& p) {
    QJsonObject obj;
    obj[QStringLiteral("base_value")]  = p.baseValue;
    obj[QStringLiteral("pulse_value")] = p.pulseValue;
    obj[QStringLiteral("delay")]       = p.delay;
    obj[QStringLiteral("width")]       = p.width;
    return obj;
}

QJsonObject writeExponentialParams(const ExponentialParams& p) {
    QJsonObject obj;
    obj[QStringLiteral("start_value")]   = p.startValue;
    obj[QStringLiteral("end_value")]     = p.endValue;
    obj[QStringLiteral("time_constant")] = p.timeConstant;
    return obj;
}

QJsonObject writeChirpParams(const ChirpParams& p) {
    QJsonObject obj;
    obj[QStringLiteral("amplitude")]       = p.amplitude;
    obj[QStringLiteral("start_frequency")] = p.startFrequency;
    obj[QStringLiteral("end_frequency")]   = p.endFrequency;
    obj[QStringLiteral("phase_deg")]       = p.phaseDeg;
    obj[QStringLiteral("offset")]          = p.offset;
    obj[QStringLiteral("sweep")]           = p.sweep == ChirpParams::Sweep::Linear
                                                 ? QStringLiteral("linear")
                                                 : QStringLiteral("exponential");
    return obj;
}

QJsonObject writeRickerParams(const RickerParams& p) {
    QJsonObject obj;
    obj[QStringLiteral("amplitude")]        = p.amplitude;
    obj[QStringLiteral("center_frequency")] = p.centerFrequency;
    obj[QStringLiteral("offset")]           = p.offset;
    return obj;
}

QJsonObject writeWindowParams(const WindowParams& p) {
    QJsonObject obj;
    obj[QStringLiteral("shape")]     = windowShapeToString(p.shape);
    obj[QStringLiteral("amplitude")] = p.amplitude;
    obj[QStringLiteral("offset")]    = p.offset;
    // All four shape parameters are written even though a shape uses at most
    // one of them: switching the shape in the property panel and back has to
    // find the value one had typed, which a save in between must not drop.
    obj[QStringLiteral("alpha")]     = p.alpha;
    obj[QStringLiteral("sigma")]     = p.sigma;
    obj[QStringLiteral("beta")]      = p.beta;
    obj[QStringLiteral("rise")]      = p.rise;
    obj[QStringLiteral("fall")]      = p.fall;
    return obj;
}

// The staircase of a voltammetry sweep, under the same key names its IM7 job
// parameters use.
void writeSweepStaircase(QJsonObject& obj, double startValue, double endValue, double stepValue) {
    obj[QStringLiteral("start_value")] = startValue;
    obj[QStringLiteral("end_value")]   = endValue;
    obj[QStringLiteral("step_value")]  = stepValue;
}

QJsonObject writeNpvParams(const NpvParams& p) {
    QJsonObject obj;
    obj[QStringLiteral("base_value")] = p.baseValue;
    writeSweepStaircase(obj, p.startValue, p.endValue, p.stepValue);
    obj[QStringLiteral("step_time")]  = p.stepTime;
    obj[QStringLiteral("pulse_time")] = p.pulseTime;
    return obj;
}

QJsonObject writeSwvParams(const SwvParams& p) {
    QJsonObject obj;
    writeSweepStaircase(obj, p.startValue, p.endValue, p.stepValue);
    obj[QStringLiteral("amplitude")] = p.amplitude;
    obj[QStringLiteral("period")]    = p.period;
    return obj;
}

QJsonObject writeDpvParams(const DpvParams& p) {
    QJsonObject obj;
    writeSweepStaircase(obj, p.startValue, p.endValue, p.stepValue);
    obj[QStringLiteral("pulse_value")]  = p.pulseValue;
    obj[QStringLiteral("step_time")]    = p.stepTime;
    obj[QStringLiteral("pulse_time")]   = p.pulseTime;
    obj[QStringLiteral("invert_pulse")] = p.invertPulse;
    return obj;
}

QJsonObject writeFormulaParams(const FormulaParams& p) {
    QJsonObject obj;
    obj[QStringLiteral("expression")] = p.expression;
    obj[QStringLiteral("time_reference")] =
        p.globalTime ? QStringLiteral("global") : QStringLiteral("local");
    return obj;
}

QJsonObject writePointsParams(const PointsParams& p) {
    QJsonObject obj;
    switch (p.interp) {
        case PointsParams::Interp::Linear:
            obj[QStringLiteral("interpolation")] = QStringLiteral("linear");
            break;
        case PointsParams::Interp::Step:
            obj[QStringLiteral("interpolation")] = QStringLiteral("step");
            break;
        case PointsParams::Interp::Pchip:
            obj[QStringLiteral("interpolation")] = QStringLiteral("pchip");
            break;
    }
    QJsonArray points;
    for (const auto& [t, v] : p.points) {
        QJsonArray pair;
        pair.append(t);
        pair.append(v);
        points.append(pair);
    }
    obj[QStringLiteral("points")] = points;
    return obj;
}

QJsonObject writeSegment(const Segment& segment) {
    QJsonObject obj;
    obj[QStringLiteral("type")]     = segmentTypeToString(segmentType(segment));
    obj[QStringLiteral("duration")] = segment.duration;
    obj[QStringLiteral("repeat")]   = segment.repeat;
    obj[QStringLiteral("params")]   = std::visit(
        [](const auto& p) -> QJsonObject {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, DcParams>) {
                return writeDcParams(p);
            } else if constexpr (std::is_same_v<T, RampParams>) {
                return writeRampParams(p);
            } else if constexpr (std::is_same_v<T, SineParams>) {
                return writeSineParams(p);
            } else if constexpr (std::is_same_v<T, SquareParams>) {
                return writeSquareParams(p);
            } else if constexpr (std::is_same_v<T, TriangleParams>) {
                return writeTriangleParams(p);
            } else if constexpr (std::is_same_v<T, PulseParams>) {
                return writePulseParams(p);
            } else if constexpr (std::is_same_v<T, ExponentialParams>) {
                return writeExponentialParams(p);
            } else if constexpr (std::is_same_v<T, FormulaParams>) {
                return writeFormulaParams(p);
            } else if constexpr (std::is_same_v<T, PointsParams>) {
                return writePointsParams(p);
            } else if constexpr (std::is_same_v<T, ChirpParams>) {
                return writeChirpParams(p);
            } else if constexpr (std::is_same_v<T, RickerParams>) {
                return writeRickerParams(p);
            } else if constexpr (std::is_same_v<T, WindowParams>) {
                return writeWindowParams(p);
            } else if constexpr (std::is_same_v<T, NpvParams>) {
                return writeNpvParams(p);
            } else if constexpr (std::is_same_v<T, SwvParams>) {
                return writeSwvParams(p);
            } else if constexpr (std::is_same_v<T, DpvParams>) {
                return writeDpvParams(p);
            }
        },
        segment.params
    );
    return obj;
}

QJsonObject writeLayer(const WaveLayer& layer) {
    QJsonObject obj;
    obj[QStringLiteral("name")]    = layer.name;
    obj[QStringLiteral("enabled")] = layer.enabled;
    obj[QStringLiteral("kind")]    = layerKindToString(layer.kind);
    obj[QStringLiteral("mode")]    = layerModeToString(layer.mode);
    obj[QStringLiteral("edge")]    = layerEdgeToString(layer.edge);
    if (isGroup(layer)) {
        QJsonArray children;
        for (const WaveLayer& child : layer.children) {
            children.append(writeLayer(child));
        }
        obj[QStringLiteral("children")] = children;
        return obj;
    }
    QJsonArray segments;
    for (const Segment& segment : layer.segments) {
        segments.append(writeSegment(segment));
    }
    obj[QStringLiteral("segments")] = segments;
    return obj;
}

QJsonObject writeDocument(const WaveDocument& document) {
    QJsonObject obj;
    obj[QStringLiteral("format")]           = kFormatMagic;
    obj[QStringLiteral("version")]          = kFormatVersion;
    if (! document.id.isEmpty()) {
        obj[QStringLiteral("id")] = document.id;
    }
    obj[QStringLiteral("name")]             = document.name;
    obj[QStringLiteral("description")]      = document.description;
    obj[QStringLiteral("sample_rate")]      = document.sampleRate;
    obj[QStringLiteral("output_data_rate")] = document.outputDataRate;
    QJsonObject exportObj;
    exportObj[QStringLiteral("last_csv_path")]      = document.exportSettings.lastExportPath;
    exportObj[QStringLiteral("format")]  = exportFormatToString(document.exportSettings.format);
    exportObj[QStringLiteral("significant_digits")] = document.exportSettings.significantDigits;
    obj[QStringLiteral("export")]                   = exportObj;

    QJsonArray layers;
    for (const WaveLayer& layer : document.layers) {
        layers.append(writeLayer(layer));
    }
    obj[QStringLiteral("layers")] = layers;

    return obj;
}

}  // namespace

LoadResult load(const QString& filePath) {
    QFile file(filePath);
    if (! file.open(QIODevice::ReadOnly)) {
        return LoadResult{
            std::nullopt, Error{QStringLiteral("cannot open \"%1\" for reading").arg(filePath)}
        };
    }
    const QByteArray data = file.readAll();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return LoadResult{
            std::nullopt, Error{QStringLiteral("invalid JSON: %1").arg(parseError.errorString())}
        };
    }
    if (! doc.isObject()) {
        return LoadResult{std::nullopt, Error{QStringLiteral("top level must be a JSON object")}};
    }
    const QJsonObject root = doc.object();

    Error error;
    QString format;
    if (! readString(root, QStringLiteral("format"), QString(), format, error)) {
        return LoadResult{std::nullopt, error};
    }
    if (format != kFormatMagic) {
        return LoadResult{
            std::nullopt,
            errorAt(
                QStringLiteral("format"),
                QStringLiteral("must be \"zahner-wave\", got \"%1\"").arg(format)
            )
        };
    }

    int version = 0;
    if (! readInt(root, QStringLiteral("version"), QString(), version, error)) {
        return LoadResult{std::nullopt, error};
    }
    if (version > kFormatVersion) {
        return LoadResult{
            std::nullopt,
            errorAt(
                QStringLiteral("version"),
                QStringLiteral("unsupported version %1 (reader supports up to %2)")
                    .arg(version)
                    .arg(kFormatVersion)
            )
        };
    }

    WaveDocument document;
    if (root.contains(QStringLiteral("id"))) {
        if (! readString(root, QStringLiteral("id"), QString(), document.id, error)) {
            return LoadResult{std::nullopt, error};
        }
    }
    if (! readString(root, QStringLiteral("name"), QString(), document.name, error)) {
        return LoadResult{std::nullopt, error};
    }
    if (! readString(root, QStringLiteral("description"), QString(), document.description, error)) {
        return LoadResult{std::nullopt, error};
    }
    if (! readNumber(root, QStringLiteral("sample_rate"), QString(), document.sampleRate, error)) {
        return LoadResult{std::nullopt, error};
    }
    if (! (document.sampleRate > 0.0)) {
        return LoadResult{
            std::nullopt, errorAt(QStringLiteral("sample_rate"), QStringLiteral("must be > 0"))
        };
    }

    if (root.contains(QStringLiteral("output_data_rate"))) {
        if (! readNumber(
                root, QStringLiteral("output_data_rate"), QString(), document.outputDataRate, error
            )) {
            return LoadResult{std::nullopt, error};
        }
        if (! (document.outputDataRate > 0.0)) {
            return LoadResult{
                std::nullopt,
                errorAt(QStringLiteral("output_data_rate"), QStringLiteral("must be > 0"))
            };
        }
    }

    // Version-1 files may contain the former V/A display hint. Values have
    // always been unit-neutral, so accept and ignore that legacy field.

    if (root.contains(QStringLiteral("export"))) {
        if (! root.value(QStringLiteral("export")).isObject()) {
            return LoadResult{
                std::nullopt, errorAt(QStringLiteral("export"), QStringLiteral("must be an object"))
            };
        }
        const QJsonObject exportObj = root.value(QStringLiteral("export")).toObject();
        if (exportObj.contains(QStringLiteral("last_csv_path"))) {
            if (! readString(
                    exportObj,
                    QStringLiteral("last_csv_path"),
                    QStringLiteral("export"),
                    document.exportSettings.lastExportPath,
                    error
                )) {
                return LoadResult{std::nullopt, error};
            }
        }
        // Absent in files written before the binary export existed; those
        // documents exported CSV, which is the default.
        if (exportObj.contains(QStringLiteral("format"))) {
            QString formatStr;
            if (! readString(
                    exportObj, QStringLiteral("format"), QStringLiteral("export"), formatStr, error
                )) {
                return LoadResult{std::nullopt, error};
            }
            const std::optional<ExportFormat> format = exportFormatFromString(formatStr);
            if (! format) {
                return LoadResult{
                    std::nullopt,
                    errorAt(
                        QStringLiteral("export.format"),
                        QStringLiteral("unknown export format '%1'").arg(formatStr)
                    )
                };
            }
            document.exportSettings.format = *format;
        }
        if (exportObj.contains(QStringLiteral("significant_digits"))) {
            if (! readInt(
                    exportObj,
                    QStringLiteral("significant_digits"),
                    QStringLiteral("export"),
                    document.exportSettings.significantDigits,
                    error
                )) {
                return LoadResult{std::nullopt, error};
            }
        }
    }

    if (! root.contains(QStringLiteral("layers")) ||
        ! root.value(QStringLiteral("layers")).isArray()) {
        return LoadResult{
            std::nullopt,
            errorAt(QStringLiteral("layers"), QStringLiteral("missing or not an array"))
        };
    }
    const QJsonArray layersArray = root.value(QStringLiteral("layers")).toArray();
    document.layers.reserve(static_cast<size_t>(layersArray.size()));
    for (qsizetype i = 0; i < layersArray.size(); ++i) {
        WaveLayer layer;
        const QString layerPath = QStringLiteral("layers[%1]").arg(i);
        if (! readLayer(layersArray.at(i), layerPath, 0, layer, error)) {
            return LoadResult{std::nullopt, error};
        }
        document.layers.push_back(layer);
    }

    return LoadResult{document, std::nullopt};
}

std::optional<Error> save(const WaveDocument& document, const QString& filePath) {
    const QJsonDocument doc(writeDocument(document));
    const QByteArray data = doc.toJson(QJsonDocument::Indented);

    QFile file(filePath);
    if (! file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return Error{QStringLiteral("cannot open \"%1\" for writing").arg(filePath)};
    }
    if (file.write(data) != data.size()) {
        return Error{QStringLiteral("failed writing to \"%1\"").arg(filePath)};
    }
    return std::nullopt;
}

}  // namespace zwe::zwjio
