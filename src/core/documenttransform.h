// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "wavedocument.h"

namespace zwe {

WaveDocument scaleDocument(const WaveDocument& document, double amplitudeFactor, double timeFactor);

}  // namespace zwe
