// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "segment.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <type_traits>

namespace zwe {

namespace {

// The staircase the three voltammetry types share, so their step count and
// timing are derived in one place instead of once per type. std::nullopt for
// every segment that is not a sweep.
struct SweepStaircase {
    double startValue = 0.0;
    double endValue   = 0.0;
    double stepValue  = 0.0;
    double stepTime   = 0.0;
};

std::optional<SweepStaircase> sweepStaircase(const Segment& segment) {
    return std::visit(
        [](const auto& params) -> std::optional<SweepStaircase> {
            using Params = std::decay_t<decltype(params)>;
            if constexpr (std::is_same_v<Params, NpvParams> || std::is_same_v<Params, DpvParams>) {
                return SweepStaircase{
                    params.startValue, params.endValue, params.stepValue, params.stepTime
                };
            } else if constexpr (std::is_same_v<Params, SwvParams>) {
                return SweepStaircase{
                    params.startValue, params.endValue, params.stepValue, params.period
                };
            } else {
                return std::nullopt;
            }
        },
        segment.params
    );
}

}  // namespace

SegmentType segmentType(const Segment& segment) {
    return std::visit(
        [](const auto& params) {
            using T = std::decay_t<decltype(params)>;
            if constexpr (std::is_same_v<T, DcParams>)
                return SegmentType::Dc;
            else if constexpr (std::is_same_v<T, RampParams>)
                return SegmentType::Ramp;
            else if constexpr (std::is_same_v<T, SineParams>)
                return SegmentType::Sine;
            else if constexpr (std::is_same_v<T, SquareParams>)
                return SegmentType::Square;
            else if constexpr (std::is_same_v<T, TriangleParams>)
                return SegmentType::Triangle;
            else if constexpr (std::is_same_v<T, PulseParams>)
                return SegmentType::Pulse;
            else if constexpr (std::is_same_v<T, ExponentialParams>)
                return SegmentType::Exponential;
            else if constexpr (std::is_same_v<T, FormulaParams>)
                return SegmentType::Formula;
            else if constexpr (std::is_same_v<T, PointsParams>)
                return SegmentType::Points;
            else if constexpr (std::is_same_v<T, ChirpParams>)
                return SegmentType::Chirp;
            else if constexpr (std::is_same_v<T, RickerParams>)
                return SegmentType::Ricker;
            else if constexpr (std::is_same_v<T, WindowParams>)
                return SegmentType::Window;
            else if constexpr (std::is_same_v<T, NpvParams>)
                return SegmentType::Npv;
            else if constexpr (std::is_same_v<T, SwvParams>)
                return SegmentType::Swv;
            else
                return SegmentType::Dpv;
        },
        segment.params
    );
}

double segmentTotalDuration(const Segment& segment) {
    return segment.duration * segment.repeat;
}

Segment defaultSegment(SegmentType type) {
    Segment segment;
    switch (type) {
        case SegmentType::Dc:
            segment.params = DcParams{};
            break;
        case SegmentType::Ramp:
            segment.params = RampParams{};
            break;
        case SegmentType::Sine:
            segment.params = SineParams{};
            break;
        case SegmentType::Square:
            segment.params = SquareParams{};
            break;
        case SegmentType::Triangle:
            segment.params = TriangleParams{};
            break;
        case SegmentType::Pulse:
            segment.params = PulseParams{};
            break;
        case SegmentType::Exponential:
            segment.params = ExponentialParams{};
            break;
        case SegmentType::Formula:
            segment.params = FormulaParams{};
            break;
        case SegmentType::Points:
            segment.params = PointsParams{};
            break;
        case SegmentType::Chirp:
            segment.params = ChirpParams{};
            break;
        case SegmentType::Ricker:
            segment.params = RickerParams{};
            break;
        case SegmentType::Window:
            segment.params = WindowParams{};
            break;
        case SegmentType::Npv:
            segment.params = NpvParams{};
            break;
        case SegmentType::Swv:
            segment.params = SwvParams{};
            break;
        case SegmentType::Dpv:
            segment.params = DpvParams{};
            break;
    }
    // A sweep's length follows from its parameters, so a fresh voltammetry
    // segment is exactly one sweep long instead of holding the generic default
    // duration and cutting itself off.
    if (const double sweep = sweepDuration(segment); sweep > 0.0) {
        segment.duration = sweep;
    }
    return segment;
}

std::size_t sweepStepCount(double startValue, double endValue, double stepValue) {
    const double step = std::abs(stepValue);
    if (! (step > 0.0)) {
        return 0;
    }
    const double quotient = std::abs(endValue - startValue) / step;
    // A quotient a rounding error short of a whole number is meant as that whole
    // number: span and step are decimals somebody typed, 0.5 in steps of 0.01 is
    // 50 steps, and shifting a sweep onto its neighbour's value must not cost it
    // the last step. The tolerance is relative so it stays negligible against
    // the step count itself.
    const double steps = std::floor(quotient + 1e-9 * std::max(1.0, quotient));
    if (! (steps >= 1.0)) {
        return 0;
    }
    return static_cast<std::size_t>(std::min(steps, static_cast<double>(MaximumSweepSteps)));
}

std::size_t sweepStepCount(const Segment& segment) {
    const std::optional<SweepStaircase> staircase = sweepStaircase(segment);
    return staircase
               ? sweepStepCount(staircase->startValue, staircase->endValue, staircase->stepValue)
               : 0;
}

std::size_t sweepLevelCount(const Segment& segment) {
    const std::size_t steps = sweepStepCount(segment);
    if (steps == 0) {
        return 0;
    }
    // An Npv pulse train opens on startValue instead of one step past it, so
    // the two ends of the span are pulses of their own and the sweep holds one
    // level more than it walks steps.
    return std::holds_alternative<NpvParams>(segment.params) ? steps + 1 : steps;
}

double sweepStepTime(const Segment& segment) {
    const std::optional<SweepStaircase> staircase = sweepStaircase(segment);
    return staircase && staircase->stepTime > 0.0 ? staircase->stepTime : 0.0;
}

double sweepDuration(const Segment& segment) {
    return static_cast<double>(sweepLevelCount(segment)) * sweepStepTime(segment);
}

}  // namespace zwe
