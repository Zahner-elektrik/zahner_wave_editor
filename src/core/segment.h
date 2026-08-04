// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>
#include <cstddef>
#include <utility>
#include <variant>
#include <vector>

namespace zwe {

enum class SegmentType {
    Dc,
    Ramp,
    Sine,
    Square,
    Triangle,
    Pulse,
    Exponential,
    Formula,
    Points,
    Chirp,
    Ricker,
    Window,
    Npv,
    Swv,
    Dpv
};

struct DcParams {
    double value                           = 0.0;

    bool operator==(const DcParams&) const = default;
};

struct RampParams {
    double startValue                        = 0.0;
    double endValue                          = 1.0;

    bool operator==(const RampParams&) const = default;
};

struct SineParams {
    double amplitude                         = 1.0;
    double frequency                         = 1.0;
    double phaseDeg                          = 0.0;
    double offset                            = 0.0;

    bool operator==(const SineParams&) const = default;
};

struct SquareParams {
    double amplitude                           = 1.0;
    double frequency                           = 1.0;
    double phaseDeg                            = 0.0;
    double offset                              = 0.0;
    double duty                                = 0.5;

    bool operator==(const SquareParams&) const = default;
};

struct TriangleParams {
    double amplitude                             = 1.0;
    double frequency                             = 1.0;
    double phaseDeg                              = 0.0;
    double offset                                = 0.0;
    double symmetry                              = 0.5;

    bool operator==(const TriangleParams&) const = default;
};

struct PulseParams {
    double baseValue                          = 0.0;
    double pulseValue                         = 1.0;
    double delay                              = 0.0;
    double width                              = 0.1;

    bool operator==(const PulseParams&) const = default;
};

struct ExponentialParams {
    double startValue                               = 0.0;
    double endValue                                 = 1.0;
    double timeConstant                             = 1.0;

    bool operator==(const ExponentialParams&) const = default;
};

struct ChirpParams {
    enum class Sweep { Linear, Exponential };

    double amplitude                          = 1.0;
    double startFrequency                     = 1.0;
    double endFrequency                       = 10.0;
    double phaseDeg                           = 0.0;
    double offset                             = 0.0;
    Sweep sweep                               = Sweep::Linear;

    bool operator==(const ChirpParams&) const = default;
};

struct RickerParams {
    double amplitude                           = 1.0;
    double centerFrequency                     = 1.0;
    double offset                              = 0.0;

    bool operator==(const RickerParams&) const = default;
};

struct FormulaParams {
    QString expression;
    bool globalTime                             = false;

    bool operator==(const FormulaParams&) const = default;
};

// A window function over one repetition, the piece a Multiply layer needs to
// fade, gate or modulate the layers below it. The shape is defined in
// continuous time on [0, duration] and symmetric around the middle, so the
// samples of a repetition are its periodic sampling: repeating the segment
// yields a seamless burst train rather than a doubled edge sample.
struct WindowParams {
    enum class Shape {
        Rectangular,
        Hann,
        Hamming,
        Blackman,
        BlackmanHarris,
        Tukey,      // uses alpha
        Gauss,      // uses sigma
        Kaiser,     // uses beta
        Trapezoid  // uses rise and fall
    };

    Shape shape                                = Shape::Hann;
    // value = offset + amplitude * shape(tau). Leaving offset at 0 gives the
    // plain 0..1 envelope; offset 0.5 with amplitude 0.5 modulates between
    // half and full, and a negative amplitude turns the window into a notch.
    double amplitude                           = 1.0;
    double offset                              = 0.0;
    // Tukey: the fraction of the duration spent in the two cosine tapers
    // together. 0 is a rectangle, 1 is a Hann window.
    double alpha                               = 0.5;
    // Gauss: standard deviation as a fraction of the half duration, > 0. The
    // window does not reach zero at the edges; ~0.4 stays close to a Hann.
    double sigma                               = 0.4;
    // Kaiser: shape parameter, >= 0. 0 is a rectangle, ~6 resembles a Hann,
    // ~8.6 a Blackman.
    double beta                                = 8.6;
    // Trapezoid: rise and fall time in seconds, >= 0 with
    // rise + fall <= duration; the flat top is what is left of the duration.
    double rise                                = 0.1;
    double fall                                = 0.1;

    bool operator==(const WindowParams&) const = default;
};

struct PointsParams {
    enum class Interp { Linear, Step, Pchip };

    Interp interp = Interp::Linear;
    // (t, value) pairs, t relative to segment start, sorted ascending.
    std::vector<std::pair<double, double>> points = {{0.0, 0.0}, {1.0, 1.0}};

    bool operator==(const PointsParams&) const    = default;
};

// The three pulse voltammetry methods of electrochemistry. What they have in
// common is a staircase: the sweep walks from startValue towards endValue in
// steps of stepValue and spends the same time on every step, which fixes both
// how many levels it holds and how long one sweep takes - see sweepStepCount(),
// sweepLevelCount() and sweepDuration(). The scan direction follows from start
// and end alone, so stepValue, pulseValue and amplitude are taken by their
// magnitude and are kept positive; a cathodic sweep is written as
// endValue < startValue.
//
// The duration stays the segment's own property. The property panel keeps it at
// sweepDuration() while the parameters are edited, but a shorter duration cuts
// the sweep off and a longer one holds its last level, exactly like every other
// segment type - and a repeat count runs the whole sweep again.

// Normal pulse voltammetry: the waveform rests on baseValue and a pulse opens
// every step. The first pulse is startValue itself and each following one
// stands one stepValue closer to endValue, which the last pulse reaches; after
// each pulse the waveform drops back to baseValue.
//
// Baseline and pulse train are separate values because the pulses have to be
// able to run towards the baseline as well as away from it: a sweep resting at
// 0 whose pulses walk from 500 mV down to 10 mV is an ordinary descending scan,
// and it is only expressible while startValue names the first pulse rather than
// the level between them.
struct NpvParams {
    // The level held between the pulses, for the whole sweep. Free of any
    // relation to the pulse train: it may sit below it, above it, or inside it.
    double baseValue                        = 0.0;
    // The first pulse and the last one. The train walks from one to the other,
    // so it holds one pulse more than sweepStepCount() counts steps - both ends
    // of the span are pulses of their own. See sweepLevelCount().
    double startValue                       = 0.01;
    double endValue                         = 0.5;
    // Height added per step, > 0.
    double stepValue                        = 0.01;
    // Time one step occupies, pulse included, > 0.
    double stepTime                         = 0.1;
    // How long the pulse holds at the beginning of a step; the rest of the step
    // returns to baseValue. > 0 and <= stepTime, where stepTime leaves no
    // baseline at all.
    double pulseTime                        = 0.05;

    bool operator==(const NpvParams&) const = default;
};

// Square wave voltammetry: a symmetric square wave of +-amplitude around a
// staircase that walks by stepValue each period. The raised half period comes
// first, whichever way the sweep runs.
struct SwvParams {
    double startValue                       = 0.0;
    double endValue                         = 0.5;
    double stepValue                        = 0.01;
    // Half amplitude of the square wave around the staircase level, > 0.
    double amplitude                        = 0.025;
    // Time one staircase step occupies, > 0. Its two half periods are the
    // forward and the reverse pulse.
    double period                           = 0.1;

    bool operator==(const SwvParams&) const = default;
};

// Differential pulse voltammetry: a staircase walking by stepValue, with a
// pulse of pulseValue opening every step. Because the pulse comes first it sits
// on the level held just before it, which is the preceding step's - so the
// pulse rises pulseValue above the old level and the step then settles
// stepValue above it.
struct DpvParams {
    double startValue                       = 0.0;
    double endValue                         = 0.5;
    double stepValue                        = 0.01;
    // Height of the pulse above the level it starts from, > 0.
    double pulseValue                       = 0.05;
    double stepTime                         = 0.1;
    // > 0 and <= stepTime.
    double pulseTime                        = 0.02;
    // Send the pulse against the scan direction instead of with it.
    bool invertPulse                        = false;

    bool operator==(const DpvParams&) const = default;
};

using SegmentParams = std::variant<
    DcParams,
    RampParams,
    SineParams,
    SquareParams,
    TriangleParams,
    PulseParams,
    ExponentialParams,
    FormulaParams,
    PointsParams,
    ChirpParams,
    RickerParams,
    WindowParams,
    NpvParams,
    SwvParams,
    DpvParams>;

struct Segment {
    double duration                       = 1.0;  // seconds, of ONE repetition, > 0
    int repeat                            = 1;    // >= 1
    SegmentParams params                  = DcParams{};

    bool operator==(const Segment&) const = default;
};

// The SegmentType corresponding to the currently active params alternative.
SegmentType segmentType(const Segment& segment);

// Total time this segment occupies including all repeats: duration * repeat.
double segmentTotalDuration(const Segment& segment);

// A segment of the given type with default duration/repeat and the
// parameter struct's default values. A voltammetry segment starts out as long
// as one sweep of its default parameters needs rather than at the generic
// default duration.
Segment defaultSegment(SegmentType type);

// The longest staircase a voltammetry segment may describe. A span divided by a
// vanishingly small step value would otherwise ask for a sweep no machine can
// sample, and the duration derived from it would swamp the time axis;
// sweepStepCount() clamps to this and zwjio rejects parameters beyond it.
constexpr std::size_t MaximumSweepSteps = 1'000'000;

// How many steps a staircase from startValue to endValue in steps of stepValue
// walks: the quotient of the span and the step magnitude, rounded down, with a
// quotient within a rounding error of a whole number counting as that whole
// number. 0 when stepValue is 0 or the span is shorter than a single step -
// there is no sweep to draw then - and never more than MaximumSweepSteps.
std::size_t sweepStepCount(double startValue, double endValue, double stepValue);

// The same for a voltammetry segment (Npv, Swv, Dpv), 0 for every other type.
std::size_t sweepStepCount(const Segment& segment);

// How many levels a voltammetry segment's staircase actually holds, each of
// them occupying one sweepStepTime(). That is sweepStepCount() for Swv and Dpv,
// whose staircase opens one step past startValue, and one more for Npv, whose
// first pulse is startValue itself so that both ends of the span are pulses. 0
// wherever sweepStepCount() is, and for every other segment type.
std::size_t sweepLevelCount(const Segment& segment);

// The time one staircase step of a voltammetry segment occupies: stepTime for
// Npv and Dpv, one full period for Swv. 0.0 for every other segment type and
// for a non-positive time, which likewise leaves nothing to draw.
double sweepStepTime(const Segment& segment);

// The duration one full sweep needs, sweepLevelCount() * sweepStepTime(). 0.0
// when there is no sweep to walk, so a caller fitting Segment::duration to it
// has to keep the duration it had in that case.
double sweepDuration(const Segment& segment);

}  // namespace zwe
