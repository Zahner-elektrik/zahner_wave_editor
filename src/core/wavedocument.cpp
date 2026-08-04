// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "wavedocument.h"

#include <algorithm>

namespace zwe {

double documentDuration(const WaveDocument& document) {
    double maxDuration = 0.0;
    for (const auto& layer : document.layers) {
        if (! layer.enabled) {
            continue;
        }
        maxDuration = std::max(maxDuration, layerDuration(layer));
    }
    return maxDuration;
}

}  // namespace zwe
