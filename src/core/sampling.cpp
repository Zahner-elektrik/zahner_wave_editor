// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "sampling.h"

#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <deque>
#include <numbers>
#include <type_traits>
#include <unordered_map>

#include "expression.h"
#include "pointinterpolation.h"

namespace zwe::sampling {

namespace {

// Duty and symmetry are user-editable and can arrive outside their documented
// range, e.g. from a property panel spinner passing through an endpoint or
// from a hand-edited .zwj. Square's duty is held strictly inside (0,1) so the
// wave never degenerates into a constant. Triangle's symmetry may be exactly
// 0 or 1: that is the documented sawtooth case, not an error.
constexpr double kDutyEpsilon = 1e-9;

// fractional phase in [0, 1) for Square/Triangle: frequency*tauRep + phase.
double fractionalPhase(double frequency, double tauRep, double phaseDeg) {
    double frac = std::fmod(frequency * tauRep + phaseDeg / 360.0, 1.0);
    if (frac < 0.0) {
        frac += 1.0;
    }
    return frac;
}

// Triangle shape normalized to [-1, +1]: rises over [0, symmetry), falls
// over [symmetry, 1). symmetry == 0 or 1 degenerates into a sawtooth.
double triangleShape(double frac, double symmetry) {
    if (frac < symmetry) {
        return -1.0 + 2.0 * (frac / symmetry);
    }
    return 1.0 - 2.0 * ((frac - symmetry) / (1.0 - symmetry));
}

// Where a time falls on a voltammetry staircase: which step it belongs to, and
// how far into that step it is. steps has to be >= 1 and stepTime > 0. A time
// at or past the end of the sweep stays in the last step, so a segment whose
// duration outlives its sweep holds the final level instead of falling back to
// the baseline.
struct SweepPosition {
    double step       = 0.0;
    double timeInStep = 0.0;
};

SweepPosition sweepPositionAt(double tauRep, double stepTime, size_t steps) {
    const double last = static_cast<double>(steps - 1);
    const double step = std::clamp(std::floor(tauRep / stepTime), 0.0, last);
    return {step, tauRep - step * stepTime};
}

// Whether a position inside a step still falls in the first holdTime of it: the
// pulse for NPV and DPV, the forward half period for SWV.
//
// The subtraction behind timeInStep loses the last bits of a decimal step time -
// 1.4 - 1.0 comes out a shade under 0.4 - so a sample landing exactly on the end
// of a 0.4 s pulse would be counted as part of it, and with step times and
// sample rates both being round numbers that happens on every step of the sweep
// rather than once. The tolerance is relative to the step so it scales with the
// times in use, and it resolves the boundary the way the half-open interval
// [0, holdTime) says: what follows the boundary wins.
bool withinStepHold(const SweepPosition& position, double holdTime, double stepTime) {
    return position.timeInStep < holdTime - stepTime * 1e-9;
}

// Modified Bessel function of the first kind, order zero, from its power
// series. Only the Kaiser window needs it, where beta stays well under 30 and
// the series reaches double precision in a few dozen terms.
double besselI0(double x) {
    const double quarterSquare = x * x / 4.0;
    double term                = 1.0;
    double sum                 = 1.0;
    for (int k = 1; k < 128; ++k) {
        term *= quarterSquare / (static_cast<double>(k) * static_cast<double>(k));
        sum += term;
        if (term <= sum * 1e-17) {
            break;
        }
    }
    return sum;
}

// Compiling a muParser expression (tokenizing plus bytecode) is far too
// expensive to redo for each of potentially millions of samples, so compiled
// expressions are cached by their source text; compilation does not depend on
// the evaluation time. Compile errors are not reported from here. The property
// panel validates while editing by calling Expression::compile() itself, and
// an invalid expression evaluates to 0.0 like an uncompiled one.
const Expression& cachedExpression(const FormulaParams& params) {
    thread_local std::unordered_map<std::string, Expression> cache;
    const std::string key = params.expression.toStdString();
    auto it               = cache.find(key);
    if (it == cache.end()) {
        Expression expr;
        expr.compile(params.expression);
        it = cache.emplace(key, std::move(expr)).first;
    }
    return it->second;
}

// First sample index k (of the series startTime + k / rate) whose time is
// >= time, clamped to [0, count]. Both ends of a segment's index range go
// through this, so consecutive segments partition the indices exactly.
size_t firstSampleIndex(double time, double startTime, double rate, size_t count) {
    const double index = std::ceil((time - startTime) * rate);
    if (! (index > 0.0)) {
        return 0;
    }
    return static_cast<size_t>(std::min(index, static_cast<double>(count)));
}

// ---- scalar evaluation of one node ----
//
// Everything below works on a node's *local* time: 0 is where the node's own
// content starts, which for a top-level node is document time 0. A Loop or
// HoldLast edge maps the incoming time onto that local timeline, and the mapped
// time is what the content sees - segment boundaries and global-time formulas
// alike.

double nodeValueAt(const WaveLayer& node, double local);

double foldValueAt(const std::vector<WaveLayer>& nodes, double local) {
    // The first enabled node starts the accumulator, so its own mode plays no
    // role and a Multiply cannot wipe out the level it opens.
    bool started = false;
    double value = 0.0;
    for (const WaveLayer& node : nodes) {
        if (! node.enabled) {
            continue;
        }
        const double own = nodeValueAt(node, local);
        value            = started ? combineLayerValue(value, node.mode, own) : own;
        started          = true;
    }
    return started ? value : 0.0;
}

// The node's own content at local time, without its edge policy: for a Leaf the
// segment at that time, for a Group the fold of its children.
double innerValueAt(const WaveLayer& node, double local) {
    if (isGroup(node)) {
        return foldValueAt(node.children, local);
    }

    double segmentStart = 0.0;
    for (const Segment& segment : node.segments) {
        const double total = segmentTotalDuration(segment);
        if (local < segmentStart + total) {
            const double tau = local - segmentStart;
            double tauRep    = segment.duration > 0.0 ? std::fmod(tau, segment.duration) : 0.0;
            if (tauRep < 0.0) {
                tauRep += segment.duration;
            }
            return segmentValueAt(segment, tauRep, local);
        }
        segmentStart += total;
    }
    return 0.0;  // Guards float rounding at the very end of the node.
}

// The local time a Loop edge maps time onto: always inside [0, duration).
double loopedTime(double local, double duration) {
    double wrapped = std::fmod(local, duration);
    if (wrapped < 0.0) {
        wrapped += duration;
    }
    return wrapped;
}

double nodeValueAt(const WaveLayer& node, double local) {
    const double duration = layerDuration(node);
    if (duration > 0.0 && local >= 0.0 && local < duration) {
        return innerValueAt(node, local);
    }
    switch (node.edge) {
        case LayerEdge::Zero:
            return 0.0;
        case LayerEdge::HoldLast:
            // A node without content has no value to hold. std::nextafter
            // reaches the last representable time inside the node, which lands
            // in its final segment because layerDuration() sums the same
            // durations in the same order.
            return duration > 0.0
                       ? innerValueAt(node, local < 0.0 ? 0.0 : std::nextafter(duration, 0.0))
                       : 0.0;
        case LayerEdge::Loop:
            return duration > 0.0 ? innerValueAt(node, loopedTime(local, duration)) : 0.0;
        case LayerEdge::Neutral:
            break;
    }
    return layerModeNeutral(node.mode);
}

// ---- windowed evaluation ----

struct FoldContext {
    double startTime = 0.0;
    double rate      = 0.0;
    size_t count     = 0;
    // When set, every Leaf node also writes its own curve into
    // leafBuffers[leafIndex] and disabled nodes are evaluated instead of being
    // skipped - what the plot needs and an export does not.
    std::vector<std::vector<double>>* leafBuffers = nullptr;
    // One reusable buffer per recursion depth. A deque because a deeper level
    // may add a buffer while an outer level still holds a reference to its own.
    std::deque<std::vector<double>> scratch;

    double timeAt(size_t k) const { return startTime + static_cast<double>(k) / rate; }

    size_t indexAt(double time) const { return firstSampleIndex(time, startTime, rate, count); }

    std::vector<double>& scratchAt(size_t depth) {
        while (scratch.size() <= depth) {
            scratch.emplace_back(count, 0.0);
        }
        return scratch[depth];
    }
};

void foldLevel(
    const std::vector<WaveLayer>& nodes,
    double origin,
    size_t kFirst,
    size_t kEnd,
    std::vector<double>& out,
    FoldContext& ctx,
    size_t depth,
    size_t leafIndex
);

// out[kFirst, kEnd) from the node's segments, whose chain starts at time origin.
// One std::visit per segment instead of per sample; Formula additionally
// resolves its compiled expression only once, and Points computes its Pchip
// tangents (O(points)) once instead of per sample.
void fillSegments(
    const WaveLayer& leaf,
    double origin,
    size_t kFirst,
    size_t kEnd,
    std::vector<double>& out,
    const FoldContext& ctx
) {
    // The scratch buffers are reused between siblings, so no index of the range
    // may keep an older value even if a boundary rounds unfavorably below.
    std::fill(out.begin() + static_cast<std::ptrdiff_t>(kFirst), out.begin() + static_cast<std::ptrdiff_t>(kEnd), 0.0);

    double segmentStart = 0.0;
    for (size_t index = 0; index < leaf.segments.size(); ++index) {
        const Segment& segment  = leaf.segments[index];
        const double segmentEnd = segmentStart + segmentTotalDuration(segment);
        // The outermost boundaries follow the requested range rather than their
        // own rounding: the caller has already established that the range lies
        // inside the node, and a sample must not fall between two segments.
        const size_t from = index == 0 ? kFirst
                                       : std::max(kFirst, ctx.indexAt(origin + segmentStart));
        const size_t to   = index + 1 == leaf.segments.size()
                                ? kEnd
                                : std::min(kEnd, ctx.indexAt(origin + segmentEnd));
        const double duration = segment.duration;

        std::visit(
            [&](const auto& params) {
                using Params = std::decay_t<decltype(params)>;
                std::vector<double> tangents;
                if constexpr (std::is_same_v<Params, PointsParams>) {
                    if (params.interp == PointsParams::Interp::Pchip) {
                        tangents = pointinterpolation::pchipTangents(params.points);
                    }
                }
                for (size_t k = from; k < to; ++k) {
                    const double local = ctx.timeAt(k) - origin;
                    double tauRep =
                        duration > 0.0 ? std::fmod(local - segmentStart, duration) : 0.0;
                    if (tauRep < 0.0) {
                        tauRep += duration;
                    }
                    if constexpr (std::is_same_v<Params, FormulaParams>) {
                        out[k] =
                            cachedExpression(params).evaluate(params.globalTime ? local : tauRep);
                    } else if constexpr (std::is_same_v<Params, PointsParams>) {
                        out[k] = pointinterpolation::evaluate(params, tauRep, tangents);
                    } else {
                        out[k] = segmentValueAt(segment, tauRep, local);
                    }
                }
            },
            segment.params
        );
        segmentStart = segmentEnd;
    }
}

// out[kFirst, kEnd) from the node's own content, its edge policy left aside.
// The node's local time zero sits at time origin.
void fillInner(
    const WaveLayer& node,
    double origin,
    size_t kFirst,
    size_t kEnd,
    std::vector<double>& out,
    FoldContext& ctx,
    size_t depth,
    size_t leafIndex
) {
    if (isGroup(node)) {
        foldLevel(node.children, origin, kFirst, kEnd, out, ctx, depth, leafIndex);
        return;
    }
    fillSegments(node, origin, kFirst, kEnd, out, ctx);
}

void fillNode(
    const WaveLayer& node,
    double origin,
    size_t kFirst,
    size_t kEnd,
    std::vector<double>& out,
    FoldContext& ctx,
    size_t depth,
    size_t leafIndex
);

// out[from, to) for samples outside the node, all of them before its start when
// before is true and all of them after its end otherwise.
void fillEdge(
    const WaveLayer& node,
    double origin,
    double duration,
    size_t from,
    size_t to,
    bool before,
    std::vector<double>& out,
    FoldContext& ctx,
    size_t depth,
    size_t leafIndex
) {
    if (from >= to) {
        return;
    }
    const auto begin = out.begin() + static_cast<std::ptrdiff_t>(from);
    const auto end   = out.begin() + static_cast<std::ptrdiff_t>(to);

    if (node.edge == LayerEdge::Loop && duration > 0.0) {
        // Every sample of the range belongs to one repetition of the node, and
        // each repetition is filled through the same segment-major path as the
        // node's own time span. Grouping the indices by repetition instead of
        // deriving index ranges from times keeps the two in step even where a
        // repetition boundary rounds onto a sample.
        size_t k = from;
        while (k < to) {
            const double period = std::floor((ctx.timeAt(k) - origin) / duration);
            size_t tileEnd      = k + 1;
            while (tileEnd < to &&
                   std::floor((ctx.timeAt(tileEnd) - origin) / duration) == period) {
                ++tileEnd;
            }
            fillInner(node, origin + period * duration, k, tileEnd, out, ctx, depth, leafIndex);
            k = tileEnd;
        }
        return;
    }

    double value = 0.0;
    switch (node.edge) {
        case LayerEdge::Neutral:
            value = layerModeNeutral(node.mode);
            break;
        case LayerEdge::Zero:
            value = 0.0;
            break;
        case LayerEdge::HoldLast:
            value = duration > 0.0
                        ? innerValueAt(node, before ? 0.0 : std::nextafter(duration, 0.0))
                        : 0.0;
            break;
        case LayerEdge::Loop:
            // A node without content has nothing to repeat.
            value = 0.0;
            break;
    }
    std::fill(begin, end, value);
}

void fillNode(
    const WaveLayer& node,
    double origin,
    size_t kFirst,
    size_t kEnd,
    std::vector<double>& out,
    FoldContext& ctx,
    size_t depth,
    size_t leafIndex
) {
    if (kFirst >= kEnd) {
        return;
    }
    const double duration = layerDuration(node);
    const size_t inFirst  = std::clamp(ctx.indexAt(origin), kFirst, kEnd);
    const size_t inEnd    = std::clamp(ctx.indexAt(origin + duration), kFirst, kEnd);

    if (duration > 0.0 && inFirst < inEnd) {
        fillInner(node, origin, inFirst, inEnd, out, ctx, depth, leafIndex);
    }
    fillEdge(node, origin, duration, kFirst, inFirst, true, out, ctx, depth, leafIndex);
    fillEdge(node, origin, duration, inEnd, kEnd, false, out, ctx, depth, leafIndex);

    if (ctx.leafBuffers && ! isGroup(node) && leafIndex < ctx.leafBuffers->size()) {
        std::vector<double>& buffer = (*ctx.leafBuffers)[leafIndex];
        std::copy(
            out.begin() + static_cast<std::ptrdiff_t>(kFirst),
            out.begin() + static_cast<std::ptrdiff_t>(kEnd),
            buffer.begin() + static_cast<std::ptrdiff_t>(kFirst)
        );
    }
}

void foldLevel(
    const std::vector<WaveLayer>& nodes,
    double origin,
    size_t kFirst,
    size_t kEnd,
    std::vector<double>& out,
    FoldContext& ctx,
    size_t depth,
    size_t leafIndex
) {
    if (kFirst >= kEnd) {
        return;
    }
    bool started = false;
    size_t leaf  = leafIndex;
    for (const WaveLayer& node : nodes) {
        const size_t leaves = leafCount(node);
        // A disabled node contributes nothing; only the plot, which draws its
        // own curve anyway, has a reason to evaluate it at all.
        if (! node.enabled && ! ctx.leafBuffers) {
            leaf += leaves;
            continue;
        }
        std::vector<double>& own = ctx.scratchAt(depth);
        fillNode(node, origin, kFirst, kEnd, own, ctx, depth + 1, leaf);
        if (node.enabled) {
            if (! started) {
                std::copy(
                    own.begin() + static_cast<std::ptrdiff_t>(kFirst),
                    own.begin() + static_cast<std::ptrdiff_t>(kEnd),
                    out.begin() + static_cast<std::ptrdiff_t>(kFirst)
                );
                started = true;
            } else {
                for (size_t k = kFirst; k < kEnd; ++k) {
                    out[k] = combineLayerValue(out[k], node.mode, own[k]);
                }
            }
        }
        leaf += leaves;
    }
    if (! started) {
        std::fill(
            out.begin() + static_cast<std::ptrdiff_t>(kFirst),
            out.begin() + static_cast<std::ptrdiff_t>(kEnd),
            0.0
        );
    }
}

constexpr size_t ChunkSize = size_t{64} * 1024;

}  // namespace

size_t sampleCount(const WaveDocument& document) {
    const double duration = documentDuration(document);
    if (! (duration > 0.0) || ! (document.sampleRate > 0.0)) {
        return 0;
    }
    return static_cast<size_t>(std::llround(duration * document.sampleRate));
}

double sampleTime(const WaveDocument& document, size_t k) {
    return static_cast<double>(k) / document.sampleRate;
}

namespace {

// windowShapeAt() without the final clamp; see there.
double rawWindowShape(const WindowParams& params, double clamped, double duration) {
    const double x    = clamped / duration;
    const double turn = 2.0 * std::numbers::pi * x;

    switch (params.shape) {
        case WindowParams::Shape::Rectangular:
            return 1.0;
        case WindowParams::Shape::Hann:
            return 0.5 - 0.5 * std::cos(turn);
        case WindowParams::Shape::Hamming:
            return 0.54 - 0.46 * std::cos(turn);
        case WindowParams::Shape::Blackman:
            return 0.42 - 0.5 * std::cos(turn) + 0.08 * std::cos(2.0 * turn);
        case WindowParams::Shape::BlackmanHarris:
            return 0.35875 - 0.48829 * std::cos(turn) + 0.14128 * std::cos(2.0 * turn) -
                   0.01168 * std::cos(3.0 * turn);
        case WindowParams::Shape::Tukey: {
            const double taper = std::clamp(params.alpha, 0.0, 1.0) / 2.0;
            if (! (taper > 0.0)) {
                return 1.0;
            }
            if (x < taper) {
                return 0.5 - 0.5 * std::cos(std::numbers::pi * x / taper);
            }
            if (x > 1.0 - taper) {
                return 0.5 - 0.5 * std::cos(std::numbers::pi * (1.0 - x) / taper);
            }
            return 1.0;
        }
        case WindowParams::Shape::Gauss: {
            if (! (params.sigma > 0.0)) {
                // A sigma of zero is a single point at the middle, which no
                // sample hits; a rectangle is the closer of the two limits.
                return 1.0;
            }
            const double z = (x - 0.5) / (params.sigma * 0.5);
            return std::exp(-0.5 * z * z);
        }
        case WindowParams::Shape::Kaiser: {
            const double beta = std::max(0.0, params.beta);
            const double edge = 2.0 * x - 1.0;
            return besselI0(beta * std::sqrt(std::max(0.0, 1.0 - edge * edge))) / besselI0(beta);
        }
        case WindowParams::Shape::Trapezoid: {
            double rise = std::max(0.0, params.rise);
            double fall = std::max(0.0, params.fall);
            // A shortened duration or a hand-edited file can ask for more ramp
            // than there is time. Scaling both keeps their ratio, which is what
            // dragging a segment boundary shorter should preserve.
            if (rise + fall > duration) {
                const double factor = duration / (rise + fall);
                rise *= factor;
                fall *= factor;
            }
            if (clamped < rise) {
                return clamped / rise;
            }
            if (clamped > duration - fall) {
                return fall > 0.0 ? (duration - clamped) / fall : 1.0;
            }
            return 1.0;
        }
    }
    return 1.0;  // unreachable
}

}  // namespace

double windowShapeAt(const WindowParams& params, double tau, double duration) {
    if (! (duration > 0.0)) {
        return 1.0;
    }
    // The cosine sums evaluate to a few 1e-17 either side of zero at the edges
    // of the repetition. Clamping keeps the documented 0 .. 1 range exact, so a
    // window can never contribute a negative factor to a multiplying layer.
    return std::clamp(rawWindowShape(params, std::clamp(tau, 0.0, duration), duration), 0.0, 1.0);
}

double segmentValueAt(const Segment& segment, double tauRep, double tLayer) {
    return std::visit(
        [&](const auto& params) -> double {
            using Params = std::decay_t<decltype(params)>;

            if constexpr (std::is_same_v<Params, DcParams>) {
                return params.value;
            } else if constexpr (std::is_same_v<Params, RampParams>) {
                const double fraction = segment.duration > 0.0 ? tauRep / segment.duration : 0.0;
                return params.startValue + (params.endValue - params.startValue) * fraction;
            } else if constexpr (std::is_same_v<Params, SineParams>) {
                const double phaseRad = params.phaseDeg * std::numbers::pi / 180.0;
                return params.offset +
                       params.amplitude *
                           std::sin(2.0 * std::numbers::pi * params.frequency * tauRep + phaseRad);
            } else if constexpr (std::is_same_v<Params, SquareParams>) {
                const double duty = std::clamp(params.duty, kDutyEpsilon, 1.0 - kDutyEpsilon);
                const double frac = fractionalPhase(params.frequency, tauRep, params.phaseDeg);
                return params.offset + (frac < duty ? params.amplitude : -params.amplitude);
            } else if constexpr (std::is_same_v<Params, TriangleParams>) {
                const double symmetry = std::clamp(params.symmetry, 0.0, 1.0);
                const double frac     = fractionalPhase(params.frequency, tauRep, params.phaseDeg);
                return params.offset + params.amplitude * triangleShape(frac, symmetry);
            } else if constexpr (std::is_same_v<Params, PulseParams>) {
                const bool high = tauRep >= params.delay && tauRep < params.delay + params.width;
                return high ? params.pulseValue : params.baseValue;
            } else if constexpr (std::is_same_v<Params, ExponentialParams>) {
                return params.endValue + (params.startValue - params.endValue) *
                                             std::exp(-tauRep / params.timeConstant);
            } else if constexpr (std::is_same_v<Params, FormulaParams>) {
                return cachedExpression(params).evaluate(params.globalTime ? tLayer : tauRep);
            } else if constexpr (std::is_same_v<Params, PointsParams>) {
                return pointinterpolation::evaluate(params, tauRep);
            } else if constexpr (std::is_same_v<Params, ChirpParams>) {
                double cycles = 0.0;
                if (params.sweep == ChirpParams::Sweep::Exponential &&
                    params.startFrequency > 0.0 && params.endFrequency > 0.0) {
                    const double ratio = params.endFrequency / params.startFrequency;
                    if (qFuzzyCompare(ratio, 1.0)) {
                        cycles = params.startFrequency * tauRep;
                    } else {
                        cycles = params.startFrequency * segment.duration *
                                 (std::pow(ratio, tauRep / segment.duration) - 1.0) /
                                 std::log(ratio);
                    }
                } else {
                    cycles = params.startFrequency * tauRep +
                             (params.endFrequency - params.startFrequency) * tauRep * tauRep /
                                 (2.0 * segment.duration);
                }
                const double phaseRad = params.phaseDeg * std::numbers::pi / 180.0;
                return params.offset +
                       params.amplitude * std::sin(2.0 * std::numbers::pi * cycles + phaseRad);
            } else if constexpr (std::is_same_v<Params, RickerParams>) {
                const double x =
                    std::numbers::pi * params.centerFrequency * (tauRep - segment.duration / 2.0);
                const double x2 = x * x;
                return params.offset + params.amplitude * (1.0 - 2.0 * x2) * std::exp(-x2);
            } else if constexpr (std::is_same_v<Params, WindowParams>) {
                return params.offset +
                       params.amplitude * windowShapeAt(params, tauRep, segment.duration);
            } else if constexpr (std::is_same_v<Params, NpvParams>) {
                // One level per pulse, the first and the last one included. A
                // sweep too short to walk has no pulses at all and leaves the
                // baseline it rests on.
                const size_t pulses = sweepLevelCount(segment);
                if (pulses == 0 || ! (params.stepTime > 0.0)) {
                    return params.baseValue;
                }
                const SweepPosition position = sweepPositionAt(tauRep, params.stepTime, pulses);
                if (! withinStepHold(position, params.pulseTime, params.stepTime)) {
                    return params.baseValue;
                }
                // Every pulse opens its step from the same baseline. The first
                // one is startValue itself and each next one stands one step
                // value closer to endValue, which the last one reaches.
                const double direction = params.startValue < params.endValue ? 1.0 : -1.0;
                return params.startValue + direction * std::abs(params.stepValue) * position.step;
            } else if constexpr (std::is_same_v<Params, SwvParams>) {
                const size_t steps =
                    sweepStepCount(params.startValue, params.endValue, params.stepValue);
                if (steps == 0 || ! (params.period > 0.0)) {
                    return params.startValue;
                }
                const SweepPosition position = sweepPositionAt(tauRep, params.period, steps);
                const double direction       = params.startValue < params.endValue ? 1.0 : -1.0;
                const double base =
                    params.startValue + direction * std::abs(params.stepValue) * position.step;
                // Forward pulse over the first half of the period, reverse pulse
                // over the second.
                return withinStepHold(position, params.period / 2.0, params.period)
                           ? base + std::abs(params.amplitude)
                           : base - std::abs(params.amplitude);
            } else if constexpr (std::is_same_v<Params, DpvParams>) {
                const size_t steps =
                    sweepStepCount(params.startValue, params.endValue, params.stepValue);
                if (steps == 0 || ! (params.stepTime > 0.0)) {
                    return params.startValue;
                }
                const SweepPosition position = sweepPositionAt(tauRep, params.stepTime, steps);
                const bool upwards           = params.startValue < params.endValue;
                const double stepHeight      = (upwards ? 1.0 : -1.0) * std::abs(params.stepValue);
                if (! withinStepHold(position, params.pulseTime, params.stepTime)) {
                    return params.startValue + stepHeight * (position.step + 1.0);
                }
                // The pulse opens the step, so it rises from the level that was
                // held just before it - the preceding step's, one step below the
                // one this step settles on.
                const double pulseHeight =
                    (upwards != params.invertPulse ? 1.0 : -1.0) * std::abs(params.pulseValue);
                return params.startValue + stepHeight * position.step + pulseHeight;
            }
        },
        segment.params
    );
}

double layerValueAt(const WaveLayer& layer, double t) {
    return nodeValueAt(layer, t);
}

double documentValueAt(const WaveDocument& document, double t) {
    return foldValueAt(document.layers, t);
}

std::vector<double> sampleLayerWindow(
    const WaveLayer& layer, double startTime, double rate, size_t count
) {
    std::vector<double> values(count, 0.0);
    if (count == 0 || ! (rate > 0.0)) {
        return values;
    }
    FoldContext context;
    context.startTime = startTime;
    context.rate      = rate;
    context.count     = count;
    fillNode(layer, 0.0, 0, count, values, context, 0, 0);
    return values;
}

std::vector<double> sampleDocumentWindow(
    const WaveDocument& document,
    double startTime,
    double rate,
    size_t count,
    const Progress& progress
) {
    std::vector<double> total(count, 0.0);
    if (! (rate > 0.0)) {
        return total;
    }

    // Chunked so progress stays responsive and the per-node scratch buffers
    // stay small even for exports with hundreds of millions of samples.
    for (size_t chunkStart = 0; chunkStart < count; chunkStart += ChunkSize) {
        const size_t chunkCount = std::min(ChunkSize, count - chunkStart);
        FoldContext context;
        context.startTime = startTime + static_cast<double>(chunkStart) / rate;
        context.rate      = rate;
        context.count     = chunkCount;
        std::vector<double> chunk(chunkCount, 0.0);
        foldLevel(document.layers, 0.0, 0, chunkCount, chunk, context, 0, 0);
        std::copy(
            chunk.begin(), chunk.end(), total.begin() + static_cast<std::ptrdiff_t>(chunkStart)
        );
        if (progress) {
            progress(chunkStart + chunkCount, count);
        }
    }
    if (progress && count == 0) {
        progress(0, 0);
    }
    return total;
}

WindowSamples sampleDocumentWindowWithLeaves(
    const WaveDocument& document, double startTime, double rate, size_t count
) {
    WindowSamples samples;
    const size_t leaves = leafPaths(document.layers).size();
    samples.total.assign(count, 0.0);
    samples.leaves.assign(leaves, std::vector<double>(count, 0.0));
    if (count == 0 || ! (rate > 0.0)) {
        return samples;
    }

    FoldContext context;
    context.startTime   = startTime;
    context.rate        = rate;
    context.count       = count;
    context.leafBuffers = &samples.leaves;
    foldLevel(document.layers, 0.0, 0, count, samples.total, context, 0, 0);
    return samples;
}

std::vector<double> sampleLayer(const WaveDocument& document, const LayerPath& path) {
    const size_t n         = sampleCount(document);
    const WaveLayer* layer = layerAtPath(document.layers, path);
    if (! layer) {
        return std::vector<double>(n, 0.0);
    }
    return sampleLayerWindow(*layer, 0.0, document.sampleRate, n);
}

std::vector<double> sampleDocument(const WaveDocument& document, const Progress& progress) {
    return sampleDocumentWindow(
        document, 0.0, document.sampleRate, sampleCount(document), progress
    );
}

}  // namespace zwe::sampling
