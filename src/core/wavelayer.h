// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QMetaType>
#include <QString>
#include <cstddef>
#include <vector>

#include "segment.h"

namespace zwe {

// How a node's own curve is combined with the curve accumulated from the nodes
// before it on the same level. Combining runs in list order, which makes the
// order of the structure tree meaningful as soon as one node multiplies.
//
// The mode of the *first enabled* node of a level is ignored: that node starts
// the accumulator. Without that rule a Multiply at the top of a level would
// multiply a zero accumulator and wipe out the whole level, and disabling the
// node above a Multiply would do the same by accident.
enum class LayerMode {
    Add,
    Multiply
};

// What a node contributes outside its own duration, i.e. between its end and
// the end of the document, and before time zero (the plot shows a little of
// that). Symmetric: HoldLast holds the first value before the start and the
// last one after the end.
enum class LayerEdge {
    // 0.0 under Add, 1.0 under Multiply - the value that leaves the
    // accumulated curve untouched. The default, so a short Multiply window
    // does not pull the rest of the signal to zero.
    Neutral,
    Zero,      // 0.0 regardless of the mode
    HoldLast,  // the nearest value of the node's own curve
    Loop       // the node's own curve repeated, period = its duration
};

// A node either holds segments or children, never both.
enum class LayerKind {
    Leaf,  // uses segments
    Group  // uses children
};

struct WaveLayer {
    QString name;
    bool enabled   = true;
    LayerMode mode = LayerMode::Add;
    LayerEdge edge = LayerEdge::Neutral;
    LayerKind kind = LayerKind::Leaf;
    // Time-ordered segments of a Leaf; empty for a Group.
    std::vector<Segment> segments;
    // Sub-nodes of a Group, combined among themselves before the group takes
    // part in its own level; empty for a Leaf.
    std::vector<WaveLayer> children;

    bool operator==(const WaveLayer&) const = default;
};

// Index chain from WaveDocument::layers down through children: {2} is the third
// top-level node, {2, 0} its first child. An empty path addresses no node and
// is how "nothing selected" is spelled throughout the UI.
using LayerPath = std::vector<size_t>;

bool isGroup(const WaveLayer& layer);

// A Leaf: the sum of segmentTotalDuration() over its segments. A Group: the
// longest duration of its *enabled* children, matching documentDuration() one
// level up. Defined regardless of WaveLayer::enabled: a disabled node still has
// a duration, it just does not count toward the duration of its parent.
double layerDuration(const WaveLayer& layer);

// The value that leaves an accumulated curve unchanged under the given mode.
double layerModeNeutral(LayerMode mode);

// accumulated (+|*) value.
double combineLayerValue(double accumulated, LayerMode mode, double value);

// The addressed node, or nullptr for an empty path, an out-of-range index, or a
// path that descends into a Leaf.
const WaveLayer* layerAtPath(const std::vector<WaveLayer>& roots, const LayerPath& path);
WaveLayer* layerAtPath(std::vector<WaveLayer>& roots, const LayerPath& path);

// The list the addressed node lives in: the children of its parent, or roots
// for a top-level node. The node itself need not exist - this is what an insert
// position resolves through - but every ancestor must. nullptr otherwise.
const std::vector<WaveLayer>* layerSiblings(
    const std::vector<WaveLayer>& roots, const LayerPath& path
);
std::vector<WaveLayer>* layerSiblings(std::vector<WaveLayer>& roots, const LayerPath& path);

// True when prefix addresses path itself or one of its ancestors. Used to keep
// a group from being dropped into its own subtree.
bool isLayerPathPrefix(const LayerPath& prefix, const LayerPath& path);

// Paths of all Leaf nodes in depth-first order, disabled ones included. The
// canvas draws one curve per entry, and sampling fills its per-leaf buffers in
// exactly this order.
std::vector<LayerPath> leafPaths(const std::vector<WaveLayer>& roots);

// Number of Leaf nodes in this node's subtree, disabled ones included: 1 for a
// Leaf, the sum over the children for a Group (so an empty Group counts 0).
size_t leafCount(const WaveLayer& layer);

}  // namespace zwe

// Layer paths travel through signals of the structure panel, the canvas and the
// property panel, which QSignalSpy can only record for a declared metatype.
Q_DECLARE_METATYPE(zwe::LayerPath)
