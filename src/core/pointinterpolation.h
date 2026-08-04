// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <utility>
#include <vector>

#include "segment.h"

namespace zwe::pointinterpolation {

// Evaluates params.points at segment-local time tau, using params.interp
// (Linear / Step / Pchip). tau before the first point holds the first
// point's value; tau after the last point holds the last point's value.
// Callers are expected to only pass a valid point list (see
// arePointsValid()); this function does not itself validate.
double evaluate(const PointsParams& params, double tau);

// Fritsch-Carlson monotone tangents for Pchip evaluation, one per point.
// Computing them is O(points); when evaluating many samples of one segment,
// compute them once and use the tangent-taking evaluate() overload below; the
// plain evaluate() recomputes them on every call.
std::vector<double> pchipTangents(const std::vector<std::pair<double, double>>& points);

// As evaluate(), but with precomputed pchipTangents(); `tangents` is only
// read when params.interp is Pchip (pass an empty vector otherwise).
double evaluate(const PointsParams& params, double tau, const std::vector<double>& tangents);

// True iff points has >= 2 entries, every t/value is finite, and t is
// strictly increasing. Both duplicate and out-of-order t values are rejected:
// the model keeps points pre-sorted, it never silently re-sorts them.
bool arePointsValid(const std::vector<std::pair<double, double>>& points);

}  // namespace zwe::pointinterpolation
