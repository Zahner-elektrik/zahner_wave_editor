// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstddef>

#include "wavelayer.h"

namespace zwe {

// Initializes a new segment from the immediate neighbors at insertionIndex.
// The layer is the state before insertion; out-of-range indices are treated as
// insertion at the end. Segments whose shape is the point of them - Pulse,
// Formula, Points and Window - are returned unchanged: a window that no longer
// starts at zero would not be a window.
Segment initializeSegmentContinuity(const WaveLayer& layer, size_t insertionIndex, Segment segment);

}  // namespace zwe
