// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "wavedocument.h"

namespace zwe {

// Scales the resulting waveform: its values by amplitudeFactor and its time axis
// by timeFactor. Every node is time-scaled, but on each level only the first
// enabled node and the Add nodes are amplitude-scaled; a Multiply node keeps its
// amplitude, or the result would scale once per multiplying node. Returns the
// document unchanged for a non-finite factor or a timeFactor <= 0.
WaveDocument scaleDocument(const WaveDocument& document, double amplitudeFactor, double timeFactor);

}  // namespace zwe
