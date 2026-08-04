// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "segmentcontinuity.h"

#include <algorithm>
#include <optional>
#include <type_traits>

#include "sampling.h"

namespace zwe {

namespace {

double segmentStartTime(const WaveLayer& layer, size_t index) {
    double start = 0.0;
    for (size_t current = 0; current < std::min(index, layer.segments.size()); ++current) {
        start += segmentTotalDuration(layer.segments[current]);
    }
    return start;
}

template <typename Params>
void alignOscillatorOffset(Params& params, const Segment& segment, double target) {
    const double currentStart = sampling::segmentValueAt(segment, 0.0, 0.0);
    params.offset += target - currentStart;
}

// A voltammetry sweep is moved as a whole: shifting every one of its values by
// the same amount keeps the span, and with it the step count and the duration
// the sweep needs. What is put on the neighbour's value differs by method. NPV
// has a baseline of its own and spends most of its time on it, so that resting
// level is what continues the preceding segment - its pulses keep their height
// above the baseline and stand out from the joint the same way they stand out
// from the rest of the sweep. SWV and DPV have no such level; they open
// straight into their staircase, so there it is the value the first sample
// really has that is matched.
template <typename Params>
void alignSweep(Params& params, const Segment& segment, double target) {
    double shift = 0.0;
    if constexpr (std::is_same_v<Params, NpvParams>) {
        shift            = target - params.baseValue;
        params.baseValue = target;
    } else {
        shift = target - sampling::segmentValueAt(segment, 0.0, 0.0);
    }
    params.startValue += shift;
    params.endValue += shift;
}

}  // namespace

Segment initializeSegmentContinuity(
    const WaveLayer& layer, size_t insertionIndex, Segment segment
) {
    insertionIndex             = std::min(insertionIndex, layer.segments.size());
    const double insertionTime = segmentStartTime(layer, insertionIndex);
    std::optional<double> precedingValue;
    std::optional<double> followingValue;

    if (insertionIndex > 0) {
        const Segment& preceding = layer.segments[insertionIndex - 1];
        precedingValue = sampling::segmentValueAt(preceding, preceding.duration, insertionTime);
    }
    if (insertionIndex < layer.segments.size()) {
        const Segment& following = layer.segments[insertionIndex];
        followingValue           = sampling::segmentValueAt(following, 0.0, insertionTime);
    }

    const std::optional<double> start = precedingValue ? precedingValue : followingValue;
    const std::optional<double> end   = followingValue ? followingValue : precedingValue;
    if (! start && ! end) {
        return segment;
    }

    std::visit(
        [&](auto& params) {
            using Params = std::decay_t<decltype(params)>;
            if constexpr (std::is_same_v<Params, DcParams>) {
                params.value = *start;
            } else if constexpr (std::is_same_v<Params, RampParams> ||
                                 std::is_same_v<Params, ExponentialParams>) {
                params.startValue = *start;
                params.endValue   = *end;
            } else if constexpr (std::is_same_v<Params, SineParams> ||
                                 std::is_same_v<Params, SquareParams> ||
                                 std::is_same_v<Params, TriangleParams> ||
                                 std::is_same_v<Params, ChirpParams> ||
                                 std::is_same_v<Params, RickerParams>) {
                alignOscillatorOffset(params, segment, *start);
            } else if constexpr (std::is_same_v<Params, NpvParams> ||
                                 std::is_same_v<Params, SwvParams> ||
                                 std::is_same_v<Params, DpvParams>) {
                alignSweep(params, segment, *start);
            }
        },
        segment.params
    );
    return segment;
}

}  // namespace zwe
