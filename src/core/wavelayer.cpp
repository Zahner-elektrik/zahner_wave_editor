// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "wavelayer.h"

#include <algorithm>

namespace zwe {

namespace {

void collectLeafPaths(
    const std::vector<WaveLayer>& nodes, LayerPath& prefix, std::vector<LayerPath>& out
) {
    for (size_t index = 0; index < nodes.size(); ++index) {
        prefix.push_back(index);
        if (isGroup(nodes[index])) {
            collectLeafPaths(nodes[index].children, prefix, out);
        } else {
            out.push_back(prefix);
        }
        prefix.pop_back();
    }
}

}  // namespace

bool isGroup(const WaveLayer& layer) {
    return layer.kind == LayerKind::Group;
}

double layerDuration(const WaveLayer& layer) {
    if (isGroup(layer)) {
        double longest = 0.0;
        for (const WaveLayer& child : layer.children) {
            if (child.enabled) {
                longest = std::max(longest, layerDuration(child));
            }
        }
        return longest;
    }

    double total = 0.0;
    for (const auto& segment : layer.segments) {
        total += segmentTotalDuration(segment);
    }
    return total;
}

double layerModeNeutral(LayerMode mode) {
    return mode == LayerMode::Multiply ? 1.0 : 0.0;
}

double combineLayerValue(double accumulated, LayerMode mode, double value) {
    return mode == LayerMode::Multiply ? accumulated * value : accumulated + value;
}

const WaveLayer* layerAtPath(const std::vector<WaveLayer>& roots, const LayerPath& path) {
    const std::vector<WaveLayer>* level = &roots;
    const WaveLayer* node               = nullptr;
    for (const size_t index : path) {
        // A path that continues past a leaf addresses nothing, which is how a
        // stale selection from before an ungroup reports itself.
        if (! level || index >= level->size()) {
            return nullptr;
        }
        node  = &(*level)[index];
        level = isGroup(*node) ? &node->children : nullptr;
    }
    return node;
}

WaveLayer* layerAtPath(std::vector<WaveLayer>& roots, const LayerPath& path) {
    return const_cast<WaveLayer*>(
        layerAtPath(static_cast<const std::vector<WaveLayer>&>(roots), path)
    );
}

const std::vector<WaveLayer>* layerSiblings(
    const std::vector<WaveLayer>& roots, const LayerPath& path
) {
    if (path.empty()) {
        return nullptr;
    }
    if (path.size() == 1) {
        return &roots;
    }
    const WaveLayer* parent = layerAtPath(roots, LayerPath(path.begin(), path.end() - 1));
    if (! parent || ! isGroup(*parent)) {
        return nullptr;
    }
    return &parent->children;
}

std::vector<WaveLayer>* layerSiblings(std::vector<WaveLayer>& roots, const LayerPath& path) {
    return const_cast<std::vector<WaveLayer>*>(
        layerSiblings(static_cast<const std::vector<WaveLayer>&>(roots), path)
    );
}

bool isLayerPathPrefix(const LayerPath& prefix, const LayerPath& path) {
    return prefix.size() <= path.size() &&
           std::equal(prefix.begin(), prefix.end(), path.begin(), path.begin() + prefix.size());
}

std::vector<LayerPath> leafPaths(const std::vector<WaveLayer>& roots) {
    std::vector<LayerPath> paths;
    LayerPath prefix;
    collectLeafPaths(roots, prefix, paths);
    return paths;
}

size_t leafCount(const WaveLayer& layer) {
    if (! isGroup(layer)) {
        return 1;
    }
    size_t total = 0;
    for (const WaveLayer& child : layer.children) {
        total += leafCount(child);
    }
    return total;
}

}  // namespace zwe
