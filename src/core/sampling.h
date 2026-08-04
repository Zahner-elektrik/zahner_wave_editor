// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstddef>
#include <functional>
#include <vector>

#include "wavedocument.h"

namespace zwe::sampling {

// Invoked periodically by the document samplers with (samplesDone,
// samplesTotal) so long recalculations can report progress. Called from the
// sampling thread; the final call is always (samplesTotal, samplesTotal).
using Progress = std::function<void(size_t, size_t)>;

// N = llround(documentDuration(document) * document.sampleRate).
// 0 if the document is empty, has no enabled layer, or sampleRate <= 0.
size_t sampleCount(const WaveDocument& document);

// Sample time of index k (0-based): k / document.sampleRate.
double sampleTime(const WaveDocument& document, size_t k);

// Value of a single segment at the given repeat-normalized segment-local time
// (tauRep, in [0, segment.duration)) and the time of its own layer's timeline
// (tLayer, used by SegmentType::Formula with time_reference "global"). tLayer is
// the document time for a layer that is neither looped nor held; see
// layerValueAt().
double segmentValueAt(const Segment& segment, double tauRep, double tLayer);

// The bare window shape of a WindowParams at segment-local time tau of a
// repetition lasting duration, amplitude and offset not applied: 0 .. 1 for
// every shape. tau outside [0, duration] is clamped in, a duration of 0 gives 1.
double windowShapeAt(const WindowParams& params, double tau, double duration);

// Value of one node's own curve at time t of the timeline it sits on, its edge
// policy applied but its own mode not (that one belongs to the level it is
// combined in), and ignoring WaveLayer::enabled so a disabled node can still be
// previewed. A Group folds its enabled children first.
//
// For a top-level node the timeline is the document's, so t is document time.
// LayerEdge::Loop and LayerEdge::HoldLast map t onto the node's own timeline
// before its content is evaluated, which is also the time a Formula segment with
// time_reference "global" then sees: such a node replays or freezes as a whole,
// formulas included.
double layerValueAt(const WaveLayer& layer, double t);

// Value of the whole document at time t: the enabled top-level nodes combined
// in list order, starting from the first of them.
double documentValueAt(const WaveDocument& document, double t);

// count values of a single node's own curve at the times startTime + k / rate
// for k in [0, count), independent of WaveLayer::enabled. Evaluates
// segment-major (one pass over a leaf's segments), so it stays fast for
// documents with many segments and compiles each formula only once.
std::vector<double> sampleLayerWindow(
    const WaveLayer& layer, double startTime, double rate, size_t count
);

// count values of the combined total signal - the *enabled* nodes of every
// level folded in list order - at the times startTime + k / rate for k in
// [0, count).
std::vector<double> sampleDocumentWindow(
    const WaveDocument& document,
    double startTime,
    double rate,
    size_t count,
    const Progress& progress = {}
);

// The combined total plus the own curve of every Leaf node, which is what the
// plot draws. leaves is ordered like leafPaths(document.layers) and includes
// disabled nodes; a leaf inside a looping or holding group is captured the way
// it contributes there, not the way it would look on its own.
struct WindowSamples {
    std::vector<double> total;
    std::vector<std::vector<double>> leaves;
};

WindowSamples sampleDocumentWindowWithLeaves(
    const WaveDocument& document, double startTime, double rate, size_t count
);

// sampleCount(document) values of a single node's own curve at the document's
// sample times, independent of WaveLayer::enabled. Returns all zeros for a path
// that addresses no node.
std::vector<double> sampleLayer(const WaveDocument& document, const LayerPath& path);

// sampleCount(document) values of the combined total signal.
std::vector<double> sampleDocument(const WaveDocument& document, const Progress& progress = {});

}  // namespace zwe::sampling
