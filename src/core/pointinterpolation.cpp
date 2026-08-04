// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "pointinterpolation.h"

#include <cmath>
#include <cstddef>

namespace zwe::pointinterpolation {

namespace {

using Points = std::vector<std::pair<double, double>>;

double sign(double x) {
    return x > 0.0 ? 1.0 : (x < 0.0 ? -1.0 : 0.0);
}

// Fritsch-Carlson shape-preserving one-sided three-point endpoint tangent,
// clamped so it cannot introduce an overshoot the adjacent secant doesn't
// have.
double endpointTangent(double h0, double h1, double delta0, double delta1) {
    double d0 = ((2.0 * h0 + h1) * delta0 - h0 * delta1) / (h0 + h1);
    if (sign(d0) != sign(delta0)) {
        d0 = 0.0;
    } else if (sign(delta0) != sign(delta1) && std::abs(d0) > 3.0 * std::abs(delta0)) {
        d0 = 3.0 * delta0;
    }
    return d0;
}

}  // namespace

// Fritsch-Carlson monotone tangents: zero at sign changes / flat secants
// (this is what prevents the spline from overshooting between knots).
std::vector<double> pchipTangents(const Points& pts) {
    const size_t n = pts.size();
    std::vector<double> d(n, 0.0);
    if (n < 2) {
        return d;
    }

    std::vector<double> h(n - 1);
    std::vector<double> delta(n - 1);
    for (size_t i = 0; i < n - 1; ++i) {
        h[i]     = pts[i + 1].first - pts[i].first;
        delta[i] = (pts[i + 1].second - pts[i].second) / h[i];
    }

    if (n == 2) {
        d[0] = d[1] = delta[0];
        return d;
    }

    for (size_t i = 1; i < n - 1; ++i) {
        if (sign(delta[i - 1]) != sign(delta[i]) || delta[i - 1] == 0.0) {
            d[i] = 0.0;
        } else {
            const double w1 = 2.0 * h[i] + h[i - 1];
            const double w2 = h[i] + 2.0 * h[i - 1];
            d[i]            = (w1 + w2) / (w1 / delta[i - 1] + w2 / delta[i]);
        }
    }

    d[0]     = endpointTangent(h[0], h[1], delta[0], delta[1]);
    d[n - 1] = endpointTangent(h[n - 2], h[n - 3], delta[n - 2], delta[n - 3]);
    return d;
}

namespace {

double hermiteAt(double x0, double x1, double y0, double y1, double d0, double d1, double x) {
    const double h   = x1 - x0;
    const double t   = (x - x0) / h;
    const double t2  = t * t;
    const double t3  = t2 * t;
    const double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
    const double h10 = t3 - 2.0 * t2 + t;
    const double h01 = -2.0 * t3 + 3.0 * t2;
    const double h11 = t3 - t2;
    return h00 * y0 + h10 * h * d0 + h01 * y1 + h11 * h * d1;
}

// Index i such that pts[i].first <= tau < pts[i+1].first. Precondition:
// pts.front().first < tau < pts.back().first (checked by the caller).
size_t intervalIndex(const Points& pts, double tau) {
    size_t lo = 0;
    size_t hi = pts.size() - 1;
    while (hi - lo > 1) {
        const size_t mid = lo + (hi - lo) / 2;
        if (pts[mid].first <= tau) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return lo;
}

}  // namespace

double evaluate(const PointsParams& params, double tau) {
    if (params.interp == PointsParams::Interp::Pchip) {
        return evaluate(params, tau, pchipTangents(params.points));
    }
    return evaluate(params, tau, {});
}

double evaluate(const PointsParams& params, double tau, const std::vector<double>& tangents) {
    const Points& pts = params.points;
    if (pts.empty()) {
        return 0.0;
    }
    if (pts.size() == 1 || tau <= pts.front().first) {
        return pts.front().second;
    }
    if (tau >= pts.back().first) {
        return pts.back().second;
    }

    const size_t i       = intervalIndex(pts, tau);
    const auto& [x0, y0] = pts[i];
    const auto& [x1, y1] = pts[i + 1];

    switch (params.interp) {
        case PointsParams::Interp::Step:
            return y0;
        case PointsParams::Interp::Linear:
            return y0 + (y1 - y0) * ((tau - x0) / (x1 - x0));
        case PointsParams::Interp::Pchip:
            return hermiteAt(x0, x1, y0, y1, tangents[i], tangents[i + 1], tau);
    }
    return 0.0;  // unreachable; silences -Wreturn-type for a hypothetical new enumerator
}

bool arePointsValid(const std::vector<std::pair<double, double>>& points) {
    if (points.size() < 2) {
        return false;
    }
    for (size_t i = 0; i < points.size(); ++i) {
        if (! std::isfinite(points[i].first) || ! std::isfinite(points[i].second)) {
            return false;
        }
        if (i > 0 && points[i].first <= points[i - 1].first) {
            return false;  // duplicate or out-of-order t
        }
    }
    return true;
}

}  // namespace zwe::pointinterpolation
