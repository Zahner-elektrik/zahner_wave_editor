// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "canvaswidget.h"

#include <QElapsedTimer>
#include <QEvent>
#include <QKeyEvent>
#include <QLoggingCategory>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QPalette>
#include <QResizeEvent>
#include <QStringList>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>

#include "core/sampling.h"
#include "plotscale.h"
#include "theme.h"

// Performance tracing, disabled by default. Enable with the environment
// variable QT_LOGGING_RULES="zwe.perf.debug=true": every display-sample
// rebuild and paint then logs its duration. Add
// "%{time yyyy-MM-dd hh:mm:ss.zzz}" to QT_MESSAGE_PATTERN for timestamps.
Q_LOGGING_CATEGORY(zwePerf, "zwe.perf", QtWarningMsg)

namespace zwe {

namespace {

constexpr int LeftMargin         = 78;
constexpr int RightMargin        = 14;
constexpr int TopMargin          = 16;
constexpr int BottomMargin       = 38;
constexpr double MarginFraction  = 0.06;
constexpr double MinimumDuration = 1e-9;
constexpr double PointHitRadius  = 8.0;
// Wider than the painted curve, so selecting one does not require hitting a
// one-pixel line exactly.
constexpr double CurveHitRadius    = 18.0;
constexpr double BoundaryHitRadius = 7.0;

// The scale helpers are shared with the spectrum plot, which labels its axes
// the same way.
using plotscale::niceStep;
using plotscale::siLabel;
using plotscale::translucent;

// The nearest multiple of step. A step of zero means "do not snap this axis",
// which is what an empty fixed step in the grid bar amounts to.
double snapToStep(double value, double step) {
    return step > 0.0 ? std::round(value / step) * step : value;
}

// The next multiple of step beyond value, in the given direction. Nudging walks
// the grid rather than carrying an off-grid offset along, so repeatedly pressing
// an arrow key ends up on round values even when the point started between two
// lines.
double nudgeToStep(double value, double step, int direction) {
    if (! (step > 0.0) || direction == 0) {
        return value;
    }
    // The tolerance absorbs the rounding error of an already snapped value:
    // round(v / step) * step is not exactly representable, and without it a
    // point sitting on a line could fail to advance a full step.
    constexpr double Tolerance = 1e-6;
    const double index         = value / step;
    return direction > 0 ? (std::floor(index + Tolerance) + 1.0) * step
                         : (std::ceil(index - Tolerance) - 1.0) * step;
}

// Grid lines closer together than this read as a solid fill and hide the curve,
// so a subdivision finer than that is snapped to but not drawn.
constexpr double MinimumFineGridSpacing = 4.0;

// The pen width of the selected layer's curve, against 1.0 for the others. Just
// enough to pick it out of a bundle of curves - a heavier line would read as a
// second result curve.
constexpr double EmphasizedCurveWidth = 1.5;

}  // namespace

CanvasWidget::CanvasWidget(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(360, 240);
}

void CanvasWidget::setDocument(const WaveDocument& document, bool resetView) {
    document_ = document;
    if (selectedLayerPath_.empty() && ! document_.layers.empty()) {
        selectedLayerPath_ = {0};
    }
    if (! selectedLayer()) {
        selectedLayerPath_.clear();
        selectedSegmentIndex_.reset();
        setPointSelection(std::nullopt);
    }
    const Segment* segment = selectedSegment();
    if (! segment) {
        selectedSegmentIndex_.reset();
        setPointSelection(std::nullopt);
    } else if (const auto* points = std::get_if<PointsParams>(&segment->params);
               ! points || ! selectedPointIndex_ || *selectedPointIndex_ >= points->points.size()) {
        setPointSelection(std::nullopt);
    }
    if (resetView) {
        view_ = {};
    }
    rebuildDisplaySamples();
    if (resetView) {
        fitToDocument();
    }
    notifySnapStep();
    update();
}

void CanvasWidget::setSnapSettings(const SnapSettings& settings) {
    SnapSettings applied = settings;
    applied.divisions    = std::clamp(applied.divisions, 1, 100);
    if (snap_ == applied) {
        return;
    }
    snap_ = applied;
    notifySnapStep();
    update();
}

void CanvasWidget::setSelectedPointIndex(std::optional<size_t> pointIndex) {
    const Segment* segment = selectedSegment();
    const auto* params     = segment ? std::get_if<PointsParams>(&segment->params) : nullptr;
    if (! params || (pointIndex && *pointIndex >= params->points.size())) {
        pointIndex.reset();
    }
    setPointSelection(pointIndex);
    update();
}

const WaveLayer* CanvasWidget::selectedLayer() const {
    return layerAtPath(document_.layers, selectedLayerPath_);
}

const Segment* CanvasWidget::selectedSegment() const {
    const WaveLayer* layer = selectedLayer();
    if (! layer || isGroup(*layer) || ! selectedSegmentIndex_ ||
        *selectedSegmentIndex_ >= layer->segments.size()) {
        return nullptr;
    }
    return &layer->segments[*selectedSegmentIndex_];
}

const Segment* CanvasWidget::segmentAt(const SegmentLocation& location) const {
    const WaveLayer* layer = layerAtPath(document_.layers, location.layerPath);
    if (! layer || isGroup(*layer) || location.segmentIndex >= layer->segments.size()) {
        return nullptr;
    }
    return &layer->segments[location.segmentIndex];
}

bool CanvasWidget::leafEmphasized(size_t index) const {
    // The selected node itself, not its subtree: a selected group would
    // otherwise pick out every curve below it at once, which says nothing about
    // which one was clicked.
    return ! selectedLayerPath_.empty() && index < leafPaths_.size() &&
           leafPaths_[index] == selectedLayerPath_;
}

bool CanvasWidget::anyLeafEmphasized() const {
    for (size_t index = 0; index < leafPaths_.size(); ++index) {
        if (leafEmphasized(index)) {
            return true;
        }
    }
    return false;
}

bool CanvasWidget::leafContributes(const LayerPath& layerPath) const {
    const std::vector<WaveLayer>* level = &document_.layers;
    for (const size_t index : layerPath) {
        if (! level || index >= level->size()) {
            return false;
        }
        const WaveLayer& node = (*level)[index];
        if (! node.enabled) {
            return false;
        }
        level = isGroup(node) ? &node.children : nullptr;
    }
    return true;
}

void CanvasWidget::setPointSelection(std::optional<size_t> pointIndex) {
    if (selectedPointIndex_ == pointIndex) {
        return;
    }
    selectedPointIndex_ = pointIndex;
    emit selectedPointChanged(pointIndex ? static_cast<int>(*pointIndex) : -1);
}

void CanvasWidget::setLegendVisible(bool visible) {
    if (legendVisible_ == visible) {
        return;
    }
    legendVisible_ = visible;
    update();
}

void CanvasWidget::setSelectedLayerPath(const LayerPath& layerPath) {
    LayerPath applied = layerPath;
    if (! layerAtPath(document_.layers, applied)) {
        applied.clear();
    }
    if (selectedLayerPath_ != applied) {
        selectedSegmentIndex_.reset();
        setPointSelection(std::nullopt);
    }
    selectedLayerPath_ = applied;
    update();
}

void CanvasWidget::setSelectedSegmentIndex(std::optional<size_t> segmentIndex) {
    const WaveLayer* layer = selectedLayer();
    if (! layer || isGroup(*layer) || ! segmentIndex ||
        *segmentIndex >= layer->segments.size()) {
        segmentIndex.reset();
    }
    if (selectedSegmentIndex_ != segmentIndex) {
        setPointSelection(std::nullopt);
    }
    selectedSegmentIndex_ = segmentIndex;
    update();
}

QRect CanvasWidget::plotRect() const {
    return rect().adjusted(LeftMargin, TopMargin, -RightMargin, -BottomMargin);
}

CanvasWidget::AxisRegion CanvasWidget::axisAtPosition(const QPointF& position) const {
    const QRect area   = plotRect();
    const QPoint point = position.toPoint();
    if (area.contains(point)) {
        return AxisRegion::None;
    }
    // Only the strip that runs alongside the plot counts. The corner where the
    // two margins meet carries no scale, so it scales nothing.
    if (point.x() < area.left() && point.y() >= area.top() && point.y() <= area.bottom()) {
        return AxisRegion::Value;
    }
    if (point.y() > area.bottom() && point.x() >= area.left() && point.x() <= area.right()) {
        return AxisRegion::Time;
    }
    return AxisRegion::None;
}

CanvasWidget::GridStep CanvasWidget::gridStep(const QRect& area) const {
    return {
        niceStep(view_.xMax - view_.xMin, std::max(2, area.width() / 90)),
        niceStep(view_.yMax - view_.yMin, std::max(2, area.height() / 58))
    };
}

CanvasWidget::GridStep CanvasWidget::snapStep(const QRect& area) const {
    if (snap_.fixedStep) {
        return {snap_.timeStep, snap_.valueStep};
    }
    const GridStep grid   = gridStep(area);
    const double division = static_cast<double>(std::max(1, snap_.divisions));
    return {grid.x / division, grid.y / division};
}

bool CanvasWidget::snappingActive(Qt::KeyboardModifiers modifiers) const {
    // Ctrl - Command on macOS - inverts the setting for one edit: a single point
    // can be placed off the grid without opening a menu, and one can be pulled
    // onto the grid while snapping is switched off.
    return snap_.enabled != modifiers.testFlag(Qt::ControlModifier);
}

void CanvasWidget::notifySnapStep() {
    const GridStep step = snapStep(plotRect());
    if (step.x == notifiedSnapStep_.x && step.y == notifiedSnapStep_.y) {
        return;
    }
    notifiedSnapStep_ = step;
    emit snapStepChanged(step.x, step.y);
}

void CanvasWidget::rebuildDisplaySamples() {
    const QRect area      = plotRect();
    const double rate     = document_.sampleRate;
    const size_t total    = sampling::sampleCount(document_);
    const double viewSpan = view_.xMax - view_.xMin;
    if (total == 0 || ! (rate > 0.0) || area.width() <= 0 || ! (viewSpan > 0.0)) {
        displayStart_ = 0.0;
        displayRate_  = 0.0;
        totalSamples_.clear();
        leafSamples_.clear();
        leafPaths_.clear();
        return;
    }

    // The plot shows what an export writes: value k is output at k / rate and
    // held until the next one takes over, so only the document's own sample
    // grid is evaluated - never a time in between that no export contains -
    // and paintEvent() draws each value as a step. Only the values whose hold
    // interval reaches into the visible window are sampled, which bounds a
    // rebuild to the plot's pixel width regardless of document length, value
    // rate or zoom level.
    const double lastIndex = static_cast<double>(total - 1);
    const auto first =
        static_cast<size_t>(std::clamp(std::floor(view_.xMin * rate), 0.0, lastIndex));
    const auto last =
        static_cast<size_t>(std::clamp(std::floor(view_.xMax * rate), 0.0, lastIndex));
    const size_t count = last - first + 1;
    displayStart_      = static_cast<double>(first) / rate;

    // Zoomed out, far more values fall into a pixel column than can be drawn.
    // The window is then divided into buckets of two per pixel, each reduced to
    // the [min, max] pair of its values and stored interleaved at
    // displayRate_ = 2 * rate / bucketSize, which the paint code draws as one
    // vertical line per column. A bucket of more than MaxSubsPerBucket values
    // is represented by every stride-th of them: a subset of the grid, so the
    // envelope still only shows values the export actually contains.
    constexpr size_t MaxSubsPerBucket = 64;
    const double valuesPerBucket      = rate * viewSpan / (2.0 * area.width());
    size_t stride                     = 1;
    size_t subsPerBucket              = 1;
    if (valuesPerBucket > 1.0) {
        stride = static_cast<size_t>(std::ceil(valuesPerBucket / MaxSubsPerBucket));
        subsPerBucket =
            static_cast<size_t>(std::ceil(valuesPerBucket / static_cast<double>(stride)));
    }
    const size_t bucketSize = stride * subsPerBucket;
    const double subRate    = rate / static_cast<double>(stride);
    const size_t subCount   = (count + stride - 1) / stride;

    // Sampled in chunks so a slow rebuild (e.g. an expensive formula) can
    // report progress while it runs. The first report waits 200 ms so
    // ordinary rebuilds never flash a progress bar.
    QElapsedTimer timer;
    timer.start();
    qint64 lastReport = 0;
    bool reported     = false;
    leafPaths_        = leafPaths(document_.layers);
    std::vector<std::vector<double>> leafSubs(leafPaths_.size());
    for (auto& samples : leafSubs) {
        samples.reserve(subCount);
    }
    // The total comes out of the same pass as the individual curves: with
    // multiplying layers and groups it can no longer be derived from them
    // afterwards, and it has to be combined per value, before any min/max
    // reduction, because the envelope of a combination is not the combination
    // of the envelopes.
    std::vector<double> totalSubs;
    totalSubs.reserve(subCount);
    constexpr size_t ChunkSize = 4096;
    for (size_t chunkStart = 0; chunkStart < subCount; chunkStart += ChunkSize) {
        // A subset of the grid can lock onto a signal commensurate with it and,
        // say, land every value on a zero crossing, making the curve vanish.
        // Each chunk therefore starts at its own offset into the stride.
        const double phase =
            std::fmod(static_cast<double>(chunkStart / ChunkSize) * 0.618033988749, 1.0);
        const size_t offset     = static_cast<size_t>(phase * static_cast<double>(stride));
        const size_t chunkFirst = chunkStart * stride + offset;
        if (chunkFirst >= count) {
            break;
        }
        const size_t chunkCount = std::min(ChunkSize, (count - chunkFirst + stride - 1) / stride);
        const double chunkTime  = static_cast<double>(first + chunkFirst) / rate;
        const sampling::WindowSamples samples =
            sampling::sampleDocumentWindowWithLeaves(document_, chunkTime, subRate, chunkCount);
        totalSubs.insert(totalSubs.end(), samples.total.begin(), samples.total.end());
        for (size_t index = 0; index < leafSubs.size() && index < samples.leaves.size(); ++index) {
            leafSubs[index].insert(
                leafSubs[index].end(), samples.leaves[index].begin(), samples.leaves[index].end()
            );
        }
        if (timer.elapsed() > 200 && timer.elapsed() - lastReport > 100) {
            lastReport = timer.elapsed();
            reported   = true;
            emit recalculationProgress(
                static_cast<int>(std::min<size_t>((chunkStart + chunkCount) * 100 / subCount, 99))
            );
        }
    }

    const size_t evaluated = totalSubs.size();
    if (bucketSize > 1) {
        displayRate_                = 2.0 * rate / static_cast<double>(bucketSize);
        // The last bucket may hold fewer values than the others: the window ends
        // where the document does, not on a bucket boundary.
        const auto reduceToEnvelope = [&](const std::vector<double>& subs) {
            std::vector<double> envelope;
            envelope.reserve(2 * ((subs.size() + subsPerBucket - 1) / subsPerBucket));
            for (size_t begin = 0; begin < subs.size(); begin += subsPerBucket) {
                const size_t end = std::min(begin + subsPerBucket, subs.size());
                const auto from  = subs.begin() + static_cast<ptrdiff_t>(begin);
                const auto to    = subs.begin() + static_cast<ptrdiff_t>(end);
                const auto [minimum, maximum] = std::minmax_element(from, to);
                envelope.push_back(*minimum);
                envelope.push_back(*maximum);
            }
            return envelope;
        };
        leafSamples_.assign(leafSubs.size(), {});
        for (size_t index = 0; index < leafSubs.size(); ++index) {
            leafSamples_[index] = reduceToEnvelope(leafSubs[index]);
        }
        totalSamples_ = reduceToEnvelope(totalSubs);
    } else {
        displayRate_  = rate;
        leafSamples_  = std::move(leafSubs);
        totalSamples_ = std::move(totalSubs);
    }

    if (reported) {
        emit recalculationProgress(100);
    }
    qCDebug(zwePerf) << "rebuildDisplaySamples:" << timer.elapsed() << "ms," << evaluated
                     << "values x" << leafPaths_.size() << "leaf layers," << subsPerBucket
                     << "per bucket, stride" << stride;
}

void CanvasWidget::fitToDocument() {
    fitTimeAxis();
    fitValueAxis();
    notifySnapStep();
    update();
}

void CanvasWidget::fitTimeAxis() {
    const double duration = documentDuration(document_);
    if (! (duration > 0.0)) {
        view_.xMin = ViewRange{}.xMin;
        view_.xMax = ViewRange{}.xMax;
    } else {
        const double margin = std::max(duration * MarginFraction, duration / 1000.0);
        view_.xMin          = -margin;
        view_.xMax          = duration + margin;
    }
    // Sampling density depends on the visible range. Rebuild here so a caller
    // that derives the vertical extent next sees the final samples, and so a
    // direct QAction invocation is immediately final.
    rebuildDisplaySamples();
}

void CanvasWidget::fitValueAxis() {
    // Display samples cover the visible time window, so this fits the values that
    // are on screen. Fitting the whole document is what fitToDocument() does, by
    // widening the time window first.
    if (totalSamples_.empty()) {
        view_.yMin = ViewRange{}.yMin;
        view_.yMax = ViewRange{}.yMax;
        return;
    }

    const auto [minimum, maximum] = std::minmax_element(totalSamples_.begin(), totalSamples_.end());
    const double span             = *maximum - *minimum;
    const double margin =
        span > 0.0 ? span * MarginFraction : std::max(std::abs(*minimum) * 0.1, 1.0);
    view_.yMin = *minimum - margin;
    view_.yMax = *maximum + margin;
}

void CanvasWidget::ensureDocumentVisible() {
    const double duration = documentDuration(document_);
    if (duration > view_.xMax || 0.0 < view_.xMin) {
        fitToDocument();
    }
}

double CanvasWidget::xToPixel(double time, const QRect& area) const {
    return area.left() + (time - view_.xMin) / (view_.xMax - view_.xMin) * area.width();
}

double CanvasWidget::yToPixel(double value, const QRect& area) const {
    return area.bottom() - (value - view_.yMin) / (view_.yMax - view_.yMin) * area.height();
}

double CanvasWidget::pixelToX(double pixel, const QRect& area) const {
    return view_.xMin + (pixel - area.left()) / area.width() * (view_.xMax - view_.xMin);
}

double CanvasWidget::pixelToY(double pixel, const QRect& area) const {
    return view_.yMin + (area.bottom() - pixel) / area.height() * (view_.yMax - view_.yMin);
}

double CanvasWidget::valueAtTime(double time) const {
    // Inside the exported stretch the value on output at that time: the one of
    // the last sample at or before it, which is what the plot draws there.
    const size_t count = sampling::sampleCount(document_);
    if (count > 0 && time >= 0.0) {
        const double index = std::floor(time * document_.sampleRate);
        if (index < static_cast<double>(count)) {
            return sampling::documentValueAt(
                document_, sampling::sampleTime(document_, static_cast<size_t>(index))
            );
        }
    }
    return sampling::documentValueAt(document_, time);
}

bool CanvasWidget::displayedAsSteps(const QRect& area) const {
    // Enveloped samples never are: their entries are at most about half a pixel
    // apart.
    const double span = view_.xMax - view_.xMin;
    return displayRate_ > 0.0 && span > 0.0 &&
           static_cast<double>(area.width()) / (span * displayRate_) >= 1.0;
}

std::optional<double> CanvasWidget::displayedLeafValue(
    size_t index, double time, double targetValue
) const {
    if (index >= leafSamples_.size() || ! (displayRate_ > 0.0)) {
        return std::nullopt;
    }
    const std::vector<double>& samples = leafSamples_[index];
    if (samples.empty()) {
        return std::nullopt;
    }
    const double position = (time - displayStart_) * displayRate_;
    if (position < -1.0 || position > static_cast<double>(samples.size())) {
        return std::nullopt;
    }
    const auto valueAt = [&](std::ptrdiff_t index) -> std::optional<double> {
        if (index < 0 || index >= static_cast<std::ptrdiff_t>(samples.size())) {
            return std::nullopt;
        }
        return samples[static_cast<size_t>(index)];
    };
    std::optional<double> closest;
    const auto consider = [&](double value) {
        if (! closest || std::abs(value - targetValue) < std::abs(*closest - targetValue)) {
            closest = value;
        }
    };
    // A value is held from its own sample time until the next one's.
    const auto held  = static_cast<std::ptrdiff_t>(std::floor(position));
    const QRect area = plotRect();
    if (displayedAsSteps(area)) {
        if (const auto value = valueAt(held)) {
            consider(*value);
        }
        // The risers at either end of the held stretch are curve as well, all
        // the way from one value to the next, as long as the pointer is near
        // enough to them that it may have aimed at one.
        const double pointer = xToPixel(time, area);
        for (const std::ptrdiff_t edge : {held, held + 1}) {
            const double edgeTime = displayStart_ + static_cast<double>(edge) / displayRate_;
            const auto before     = valueAt(edge - 1);
            const auto after      = valueAt(edge);
            if (before && after && std::abs(xToPixel(edgeTime, area) - pointer) <= CurveHitRadius) {
                consider(std::clamp(
                    targetValue, std::min(*before, *after), std::max(*before, *after)
                ));
            }
        }
        return closest;
    }
    // The neighbours count too: zoomed out, a pixel column holds the minimum
    // and the maximum of a whole bucket, and both are curve the eye sees there.
    // The one nearest the cursor is the one that was clicked at.
    for (std::ptrdiff_t offset = -1; offset <= 1; ++offset) {
        if (const auto value = valueAt(held + offset)) {
            consider(*value);
        }
    }
    return closest;
}

void CanvasWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event)
    QElapsedTimer paintTimer;
    paintTimer.start();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    const QPalette palette = this->palette();
    painter.fillRect(rect(), palette.color(QPalette::Base));
    const QRect area = plotRect();
    if (area.width() <= 1 || area.height() <= 1) {
        return;
    }

    const bool darkMode    = palette.color(QPalette::Base).lightness() < 128;
    const QColor textColor = palette.color(QPalette::Text);
    painter.setFont(font());
    drawGrid(painter, area, textColor);

    painter.setPen(QPen(textColor, 1.0));
    painter.drawRect(area);
    painter.drawText(
        QRectF(area.left(), 0.0, area.width(), TopMargin), Qt::AlignCenter, tr("Time / Value")
    );

    // clip narrows the drawing to a stretch of the plot, which is how a single
    // segment of a curve is drawn differently from the rest of it; an empty one
    // means the whole plot.
    auto drawSamples = [&](const std::vector<double>& samples,
                           const QColor& color,
                           double width,
                           const QRectF& clip = {}) {
        if (samples.empty() || displayRate_ <= 0.0) {
            return;
        }
        painter.save();
        painter.setClipRect(clip.isEmpty() ? QRectF(area) : clip);
        painter.setPen(QPen(color, width));
        painter.setRenderHint(QPainter::Antialiasing, true);

        // Samples cover exactly the visible window, so every one is drawn.
        if (displayedAsSteps(area)) {
            // Each value is what the output holds from its sample time until the
            // next one's, where it jumps straight to the next value: a staircase
            // of horizontal and vertical lines, never a slope between two
            // values. The last one is held for its full sample period, which is
            // where the exported signal ends. One polyline with a corner only
            // where the value changes, so a translucent curve is not blended
            // twice where two lines would meet, and a long constant stretch
            // costs a single line. Its joins are all right angles, which are
            // cheap to stroke, unlike the steep zigzags of the dense case.
            QPen pen = painter.pen();
            pen.setJoinStyle(Qt::MiterJoin);
            painter.setPen(pen);
            // Zoomed in far, the first and the last value can reach thousands of
            // pixels beyond the plot; only the stretch that can be seen is kept.
            const double leftLimit  = area.left() - 4.0;
            const double rightLimit = area.right() + 4.0;
            const auto timePixel    = [&](size_t index) {
                return std::clamp(
                    xToPixel(displayStart_ + static_cast<double>(index) / displayRate_, area),
                    leftLimit,
                    rightLimit
                );
            };
            QPolygonF steps;
            steps.reserve(static_cast<qsizetype>(2 * samples.size() + 1));
            double previousY = 0.0;
            for (size_t index = 0; index < samples.size(); ++index) {
                const double y = yToPixel(samples[index], area);
                if (index == 0) {
                    steps << QPointF(timePixel(index), y);
                } else if (y != previousY) {
                    const double x = timePixel(index);
                    steps << QPointF(x, previousY) << QPointF(x, y);
                }
                previousY = y;
            }
            steps << QPointF(timePixel(samples.size()), previousY);
            painter.drawPolyline(steps);
        } else {
            // Dense data (more than one value per pixel column) becomes one
            // vertical min/max line per column. That is indistinguishable inside
            // a 1 px column and avoids QPainter's wide-pen stroking of thousands
            // of steep joins, which is very slow for signals that alias at
            // display resolution.
            std::vector<QLineF> lines;
            lines.reserve(static_cast<size_t>(area.width()) + 1);
            bool haveColumn = false;
            int column      = 0;
            double minY = 0.0, maxY = 0.0, previousY = 0.0;
            const auto flushColumn = [&] {
                if (haveColumn) {
                    lines.emplace_back(column + 0.5, minY, column + 0.5, maxY);
                }
            };
            for (size_t index = 0; index < samples.size(); ++index) {
                const double x = xToPixel(displayStart_ + index / displayRate_, area);
                const double y = yToPixel(samples[index], area);
                const int c    = static_cast<int>(std::floor(x));
                if (! haveColumn || c != column) {
                    flushColumn();
                    // Include the previous column's last value so adjacent
                    // columns stay visually connected across jumps.
                    minY       = haveColumn ? std::min(y, previousY) : y;
                    maxY       = haveColumn ? std::max(y, previousY) : y;
                    column     = c;
                    haveColumn = true;
                } else {
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);
                }
                previousY = y;
            }
            flushColumn();
            painter.drawLines(lines.data(), static_cast<int>(lines.size()));
        }
        painter.restore();
    };

    // Every curve at the same weight first. Nothing is dimmed unless one of them
    // is actually singled out: with no selection, or with a group selected, they
    // all stay as they were.
    const bool singledOut = anyLeafEmphasized();
    const int baseAlpha   = singledOut ? (darkMode ? 130 : 70) : (darkMode ? 205 : 115);
    for (size_t index = 0; index < leafSamples_.size(); ++index) {
        drawSamples(
            leafSamples_[index], translucent(theme::curveColor(index, palette), baseAlpha), 1.0
        );
    }

    // Then what was clicked, a little wider and opaque, on top - so no other
    // curve can cover it. With a segment selected that is only the segment's own
    // stretch of the curve: the rest of its layer keeps the weight of everything
    // else, otherwise clicking one segment would mark its neighbors just as much.
    for (size_t index = 0; index < leafSamples_.size(); ++index) {
        if (! leafEmphasized(index)) {
            continue;
        }
        QRectF clip;
        if (const auto location = selectedSegmentLocation()) {
            const double left = xToPixel(location->start, area);
            const double right =
                xToPixel(location->start + segmentTotalDuration(*segmentAt(*location)), area);
            clip = QRectF(QPointF(left, area.top()), QPointF(right, area.bottom()))
                       .normalized()
                       .intersected(area);
            if (clip.isEmpty()) {
                continue;  // scrolled out of the view
            }
        }
        drawSamples(
            leafSamples_[index], theme::curveColor(index, palette), EmphasizedCurveWidth, clip
        );
    }

    drawSelectedSegment(painter, area);

    if (const WaveLayer* layer = selectedLayer(); layer && ! isGroup(*layer)) {
        painter.save();
        painter.setClipRect(area);
        QColor guideColor = palette.color(QPalette::Highlight);
        guideColor.setAlpha(145);
        painter.setPen(QPen(guideColor, 1.0, Qt::DashLine));
        double boundary = 0.0;
        for (const Segment& segment : layer->segments) {
            boundary += segmentTotalDuration(segment);
            if (boundary > view_.xMin && boundary < view_.xMax) {
                const double pixel = xToPixel(boundary, area);
                painter.drawLine(QPointF(pixel, area.top()), QPointF(pixel, area.bottom()));
            }
        }
        painter.restore();
    }

    QColor totalColor = palette.color(QPalette::Highlight);
    if (darkMode && totalColor.lightness() < 155) {
        totalColor = totalColor.lighter(175);
    }
    drawSamples(totalSamples_, totalColor, darkMode ? 3.0 : 2.0);
    drawPointHandles(painter, area);
    drawLegend(painter, area, totalColor);
    qCDebug(zwePerf) << "paintEvent:" << paintTimer.elapsed() << "ms";
}

void CanvasWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    rebuildDisplaySamples();
    // A wider plot fits more grid lines, which makes the automatic snap step
    // finer even though the view range did not change.
    notifySnapStep();
}

void CanvasWidget::wheelEvent(QWheelEvent* event) {
    const bool overPlot   = plotRect().contains(event->position().toPoint());
    const AxisRegion axis = axisAtPosition(event->position());
    // A wheel tilted sideways carries no vertical rotation; scaling on that would
    // make an axis jump while one only scrolls past it.
    const int rotation = event->angleDelta().y();
    if ((! overPlot && axis == AxisRegion::None) || rotation == 0) {
        event->ignore();
        return;
    }

    // On an axis strip only that axis is scaled. Over the plot itself both keep
    // what they always did: the wheel scales time, Ctrl and the wheel the value
    // axis.
    const bool value =
        axis == AxisRegion::Value || (overPlot && event->modifiers().testFlag(Qt::ControlModifier));
    const bool time = axis == AxisRegion::Time || (overPlot && ! value);
    zoomView(time, value, rotation > 0 ? 0.8 : 1.25, event->position());
    event->accept();
}

void CanvasWidget::zoomView(bool time, bool value, double factor, const QPointF& position) {
    const QRect area = plotRect();
    if (time) {
        const double anchor = pixelToX(position.x(), area);
        view_.xMin          = anchor + (view_.xMin - anchor) * factor;
        view_.xMax          = anchor + (view_.xMax - anchor) * factor;
        // Display samples only cover the visible window, so a changed time range
        // has to resample; the rebuild is bounded by the plot's pixel width.
        rebuildDisplaySamples();
    }
    if (value) {
        const double anchor = pixelToY(position.y(), area);
        view_.yMin          = anchor + (view_.yMin - anchor) * factor;
        view_.yMax          = anchor + (view_.yMax - anchor) * factor;
    }
    updateCursorReadout(position);
    notifySnapStep();
    update();
}

void CanvasWidget::mousePressEvent(QMouseEvent* event) {
    if (const AxisRegion axis = axisAtPosition(event->position());
        event->button() == Qt::LeftButton && axis != AxisRegion::None) {
        // Dragging a scale shifts its own axis only, which is the way to follow a
        // signal in time without losing the amplitude one has zoomed in on.
        setFocus(Qt::MouseFocusReason);
        dragMode_     = DragMode::Pan;
        panAxis_      = axis;
        panStart_     = event->pos();
        panStartView_ = view_;
        panMoved_     = false;
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && plotRect().contains(event->position().toPoint())) {
        setFocus(Qt::MouseFocusReason);
        panAxis_ = AxisRegion::None;
        if (const auto pointIndex = pointAtPosition(event->position())) {
            setPointSelection(pointIndex);
            dragSegment_ = selectedSegmentLocation();
            dragMode_    = DragMode::Point;
            setCursor(Qt::SizeAllCursor);
        } else if (const auto boundary = boundaryAtPosition(event->position())) {
            dragSegment_ = boundary;
            dragMode_    = DragMode::Boundary;
            setCursor(Qt::SizeHorCursor);
        } else if (const auto segment = segmentAtPosition(event->position())) {
            selectedLayerPath_    = segment->layerPath;
            selectedSegmentIndex_ = segment->segmentIndex;
            setPointSelection(std::nullopt);
            emit segmentSelected(segment->layerPath, segment->segmentIndex);
            update();
        } else {
            dragMode_     = DragMode::Pan;
            panStart_     = event->pos();
            panStartView_ = view_;
            panMoved_     = false;
            setCursor(Qt::ClosedHandCursor);
        }
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void CanvasWidget::mouseMoveEvent(QMouseEvent* event) {
    const QRect area = plotRect();
    if (dragMode_ == DragMode::Pan) {
        const QPoint delta = event->pos() - panStart_;
        // A press that ends up panning is a drag, not a click on the background:
        // the release must not throw the selection away. A few pixels of slack
        // keep an unsteady hand from turning a click into a pan.
        if (delta.manhattanLength() > 3) {
            panMoved_ = true;
        }
        // A pan started on a scale is locked to that axis: the movement across it
        // is what one aims at, the movement along it is not meant to do anything.
        const int alongTime  = panAxis_ == AxisRegion::Value ? 0 : delta.x();
        const int alongValue = panAxis_ == AxisRegion::Time ? 0 : delta.y();
        const double xScale  = (panStartView_.xMax - panStartView_.xMin) / area.width();
        const double yScale  = (panStartView_.yMax - panStartView_.yMin) / area.height();
        view_.xMin           = panStartView_.xMin - alongTime * xScale;
        view_.xMax           = panStartView_.xMax - alongTime * xScale;
        view_.yMin           = panStartView_.yMin + alongValue * yScale;
        view_.yMax           = panStartView_.yMax + alongValue * yScale;
        // Display samples only cover the visible window, so panning must
        // resample; the rebuild is bounded by the plot's pixel width.
        if (panAxis_ != AxisRegion::Value) {
            rebuildDisplaySamples();
        }
        update();
    } else if (dragMode_ == DragMode::Point) {
        dragPoint(event->position(), snappingActive(event->modifiers()));
    } else if (dragMode_ == DragMode::Boundary) {
        dragBoundary(event->position(), snappingActive(event->modifiers()));
    } else {
        updateHoverCursor(event->position());
    }
    updateCursorReadout(event->position());
    QWidget::mouseMoveEvent(event);
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && dragMode_ != DragMode::None) {
        // Clicking the background - no curve, no handle, no boundary, and not a
        // pan - is how one gets back to "nothing selected", which is the state
        // that draws every curve at the same weight again. A scale is not the
        // background: a click that misses its drag must not throw anything away.
        const bool clearsSelection =
            dragMode_ == DragMode::Pan && panAxis_ == AxisRegion::None && ! panMoved_;
        dragMode_ = DragMode::None;
        panAxis_  = AxisRegion::None;
        dragSegment_.reset();
        if (clearsSelection && ! selectedLayerPath_.empty()) {
            selectedLayerPath_.clear();
            selectedSegmentIndex_.reset();
            setPointSelection(std::nullopt);
            emit selectionCleared();
            update();
        }
        updateHoverCursor(event->position());
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void CanvasWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (const AxisRegion axis = axisAtPosition(event->position());
        event->button() == Qt::LeftButton && axis != AxisRegion::None) {
        // Double-clicking a scale fits that axis and leaves the other one alone -
        // the counterpart to Fit Waveform, which fits both.
        if (axis == AxisRegion::Time) {
            fitTimeAxis();
        } else {
            fitValueAxis();
        }
        notifySnapStep();
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && plotRect().contains(event->position().toPoint())) {
        const auto location = selectedSegmentLocation();
        const Segment* selected = location ? segmentAt(*location) : nullptr;
        if (selected && std::holds_alternative<PointsParams>(selected->params)) {
            const QRect area        = plotRect();
            const double clicked    = pixelToX(event->position().x(), area);
            const Segment& original = *selected;
            const double total = segmentTotalDuration(original);
            // The hit test uses the unsnapped time, so a click near the end of a
            // segment cannot be snapped past its boundary and fall through to
            // "fit the waveform".
            if (clicked >= location->start && clicked <= location->start + total) {
                const double time = snappingActive(event->modifiers())
                                        ? std::clamp(
                                              snapToStep(clicked, snapStep(area).x),
                                              location->start,
                                              location->start + total
                                          )
                                        : clicked;
                Segment edited    = original;
                auto& points      = std::get<PointsParams>(edited.params).points;
                double localTime  = std::fmod(time - location->start, edited.duration);
                if (localTime < 0.0) {
                    localTime += edited.duration;
                }
                if (std::abs(time - (location->start + total)) < MinimumDuration) {
                    localTime = edited.duration;
                }
                // Only the time is snapped: the value comes from the curve, so
                // inserting a point never changes the shape of the segment.
                const double value   = sampling::segmentValueAt(edited, localTime, time);
                const auto insertion = std::lower_bound(
                    points.begin(),
                    points.end(),
                    localTime,
                    [](const auto& point, double t) { return point.first < t; }
                );
                const double spacing = std::max(MinimumDuration, edited.duration * 1e-9);
                if ((insertion == points.end() || insertion->first - localTime > spacing) &&
                    (insertion == points.begin() ||
                     localTime - std::prev(insertion)->first > spacing)) {
                    setPointSelection(
                        static_cast<size_t>(std::distance(points.begin(), insertion))
                    );
                    points.insert(insertion, {localTime, value});
                    emitEditedSegment(*location, std::move(edited));
                }
                event->accept();
                return;
            }
        }
        fitToDocument();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void CanvasWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete && selectedPointIndex_) {
        const auto location = selectedSegmentLocation();
        if (const Segment* selected = location ? segmentAt(*location) : nullptr) {
            Segment edited = *selected;
            if (auto* params = std::get_if<PointsParams>(&edited.params);
                params && params->points.size() > 2 &&
                *selectedPointIndex_ < params->points.size()) {
                params->points.erase(
                    params->points.begin() + static_cast<std::ptrdiff_t>(*selectedPointIndex_)
                );
                setPointSelection(std::nullopt);
                emitEditedSegment(*location, std::move(edited));
                event->accept();
                return;
            }
        }
    }
    // Placing a point exactly is what the mouse is bad at: it can only ever hit
    // the value one pixel maps to. The arrow keys walk the selected point along
    // the snap grid instead, Shift along whole grid cells.
    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right ||
        event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
        if (nudgeSelectedPoint(event->key(), event->modifiers().testFlag(Qt::ShiftModifier))) {
            event->accept();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

bool CanvasWidget::nudgeSelectedPoint(int key, bool coarse) {
    const auto location = selectedSegmentLocation();
    if (! location || ! selectedPointIndex_) {
        return false;
    }
    Segment edited = *segmentAt(*location);
    auto* params   = std::get_if<PointsParams>(&edited.params);
    if (! params || *selectedPointIndex_ >= params->points.size()) {
        return false;
    }

    const QRect area    = plotRect();
    const GridStep fine = snapStep(area);
    // A coarse nudge jumps a whole grid cell - ten steps when the step is fixed
    // rather than derived from the grid - so it still lands where a fine nudge
    // could have taken the point.
    const double factor = ! coarse          ? 1.0
                          : snap_.fixedStep ? 10.0
                                            : static_cast<double>(std::max(1, snap_.divisions));
    const GridStep step{fine.x * factor, fine.y * factor};
    auto& points = params->points;
    auto& point  = points[*selectedPointIndex_];
    if (key == Qt::Key_Left || key == Qt::Key_Right) {
        const double spacing = std::max(MinimumDuration, edited.duration * 1e-9);
        const double lower =
            *selectedPointIndex_ == 0 ? 0.0 : points[*selectedPointIndex_ - 1].first + spacing;
        const double upper = *selectedPointIndex_ + 1 == points.size()
                                 ? edited.duration
                                 : points[*selectedPointIndex_ + 1].first - spacing;
        // Nudging works on the position of the first repetition, the one the
        // point's own time refers to.
        const double nudged =
            nudgeToStep(location->start + point.first, step.x, key == Qt::Key_Right ? 1 : -1) -
            location->start;
        const double clamped = std::clamp(nudged, lower, std::max(lower, upper));
        if (clamped == point.first) {
            return false;
        }
        point.first = clamped;
    } else {
        const double nudged = nudgeToStep(point.second, step.y, key == Qt::Key_Up ? 1 : -1);
        if (nudged == point.second) {
            return false;
        }
        point.second = nudged;
    }
    // Not a continuous edit: consecutive nudges of one point already merge into a
    // single undo step through the segment's merge key.
    emitEditedSegment(*location, std::move(edited));
    return true;
}

std::optional<CanvasWidget::SegmentLocation> CanvasWidget::selectedSegmentLocation() const {
    const WaveLayer* layer = selectedLayer();
    if (! selectedSegment()) {
        return std::nullopt;
    }

    double start = 0.0;
    for (size_t index = 0; index < *selectedSegmentIndex_; ++index) {
        start += segmentTotalDuration(layer->segments[index]);
    }
    return SegmentLocation{selectedLayerPath_, *selectedSegmentIndex_, start};
}

std::optional<CanvasWidget::SegmentLocation> CanvasWidget::segmentAtTime(
    const LayerPath& layerPath, double time
) const {
    const WaveLayer* layer = layerAtPath(document_.layers, layerPath);
    if (! layer || isGroup(*layer) || time < 0.0) {
        return std::nullopt;
    }
    double start         = 0.0;
    const auto& segments = layer->segments;
    for (size_t index = 0; index < segments.size(); ++index) {
        const double end = start + segmentTotalDuration(segments[index]);
        if (time >= start && time <= end && (time < end || index + 1 == segments.size())) {
            return SegmentLocation{layerPath, index, start};
        }
        start = end;
    }
    return std::nullopt;
}

std::optional<CanvasWidget::SegmentLocation> CanvasWidget::segmentAtPosition(
    const QPointF& position
) const {
    const QRect area   = plotRect();
    const double time  = pixelToX(position.x(), area);
    const double value = pixelToY(position.y(), area);
    std::optional<SegmentLocation> closest;
    double closestDistance = CurveHitRadius;
    // Hit tested against the samples that were drawn rather than against a
    // fresh evaluation: inside a looping or holding group a leaf contributes a
    // different curve than it would on its own, and the one on screen is the
    // one being aimed at.
    for (size_t index = 0; index < leafPaths_.size(); ++index) {
        if (! leafContributes(leafPaths_[index])) {
            continue;
        }
        const auto location = segmentAtTime(leafPaths_[index], time);
        if (! location) {
            continue;
        }
        const std::optional<double> displayed = displayedLeafValue(index, time, value);
        if (! displayed) {
            continue;
        }
        const double distance = std::abs(yToPixel(*displayed, area) - position.y());
        if (distance < closestDistance) {
            closestDistance = distance;
            closest         = location;
        }
    }
    return closest;
}

std::optional<size_t> CanvasWidget::pointAtPosition(const QPointF& position) const {
    const auto location = selectedSegmentLocation();
    if (! location) {
        return std::nullopt;
    }
    const Segment& segment = *segmentAt(*location);
    const auto* params     = std::get_if<PointsParams>(&segment.params);
    if (! params || params->points.empty() || ! (segment.duration > 0.0)) {
        return std::nullopt;
    }

    // Only points within the hit radius in x can be hits, so visit just the
    // repetitions overlapping that time span and, within each, the point range
    // found by binary search. Scanning all repeat * points combinations would
    // cost seconds per mouse event on a large imported segment.
    const QRect area          = plotRect();
    const double tMin         = pixelToX(position.x() - PointHitRadius, area);
    const double tMax         = pixelToX(position.x() + PointHitRadius, area);
    const int firstRepetition = static_cast<int>(std::clamp(
        std::floor((tMin - location->start) / segment.duration),
        0.0,
        static_cast<double>(segment.repeat - 1)
    ));
    const int lastRepetition  = static_cast<int>(std::clamp(
        std::floor((tMax - location->start) / segment.duration),
        0.0,
        static_cast<double>(segment.repeat - 1)
    ));

    std::optional<size_t> closest;
    double closestDistance = PointHitRadius;
    for (int repetition = firstRepetition; repetition <= lastRepetition; ++repetition) {
        const double repetitionStart = location->start + repetition * segment.duration;
        const double tauLo           = tMin - repetitionStart;
        const double tauHi           = tMax - repetitionStart;
        auto it                      = std::lower_bound(
            params->points.begin(),
            params->points.end(),
            tauLo,
            [](const auto& point, double tau) { return point.first < tau; }
        );
        for (; it != params->points.end() && it->first <= tauHi; ++it) {
            const QPointF handle(
                xToPixel(repetitionStart + it->first, area), yToPixel(it->second, area)
            );
            const double distance = QLineF(handle, position).length();
            if (distance < closestDistance) {
                closestDistance = distance;
                closest         = static_cast<size_t>(std::distance(params->points.begin(), it));
            }
        }
    }
    return closest;
}

std::optional<CanvasWidget::SegmentLocation> CanvasWidget::boundaryAtPosition(
    const QPointF& position
) const {
    const WaveLayer* layer = selectedLayer();
    if (! layer || isGroup(*layer)) {
        return std::nullopt;
    }
    const QRect area     = plotRect();
    const double time    = pixelToX(position.x(), area);
    double start         = 0.0;
    const auto& segments = layer->segments;
    for (size_t index = 0; index < segments.size(); ++index) {
        const double boundary = start + segmentTotalDuration(segments[index]);
        if (std::abs(xToPixel(boundary, area) - position.x()) <= BoundaryHitRadius &&
            time >= view_.xMin && time <= view_.xMax) {
            return SegmentLocation{selectedLayerPath_, index, start};
        }
        start = boundary;
    }
    return std::nullopt;
}

void CanvasWidget::emitEditedSegment(
    const SegmentLocation& location, Segment segment, bool continuous
) {
    if (! segmentAt(location)) {
        return;
    }
    emit segmentChanged(location.layerPath, location.segmentIndex, segment, continuous);
}

void CanvasWidget::dragPoint(const QPointF& position, bool snap) {
    if (! dragSegment_ || ! selectedPointIndex_) {
        return;
    }
    const Segment* dragged = segmentAt(*dragSegment_);
    if (! dragged) {
        return;
    }
    const Segment& original    = *dragged;
    const auto* originalPoints = std::get_if<PointsParams>(&original.params);
    if (! originalPoints || *selectedPointIndex_ >= originalPoints->points.size()) {
        return;
    }

    Segment edited       = original;
    auto& points         = std::get<PointsParams>(edited.params).points;
    const double spacing = std::max(MinimumDuration, edited.duration * 1e-9);
    const double lower =
        *selectedPointIndex_ == 0 ? 0.0 : points[*selectedPointIndex_ - 1].first + spacing;
    const double upper  = *selectedPointIndex_ + 1 == points.size()
                              ? edited.duration
                              : points[*selectedPointIndex_ + 1].first - spacing;

    const QRect area    = plotRect();
    const GridStep step = snapStep(area);
    // The grid is in document time, so the time is snapped before it is folded
    // into the segment: the handle then sits on a line of the repetition being
    // dragged, which is the one under the cursor.
    const double time =
        snap ? snapToStep(pixelToX(position.x(), area), step.x) : pixelToX(position.x(), area);
    const double value =
        snap ? snapToStep(pixelToY(position.y(), area), step.y) : pixelToY(position.y(), area);
    double localTime = std::fmod(time - dragSegment_->start, edited.duration);
    if (localTime < 0.0) {
        localTime += edited.duration;
    }
    localTime                    = std::clamp(localTime, lower, std::max(lower, upper));
    points[*selectedPointIndex_] = {localTime, value};
    emitEditedSegment(*dragSegment_, std::move(edited), true);
}

void CanvasWidget::dragBoundary(const QPointF& position, bool snap) {
    if (! dragSegment_ || ! segmentAt(*dragSegment_)) {
        return;
    }
    Segment edited    = *segmentAt(*dragSegment_);
    const QRect area  = plotRect();
    const double time = snap ? snapToStep(pixelToX(position.x(), area), snapStep(area).x)
                             : pixelToX(position.x(), area);
    const double totalDuration =
        std::max(MinimumDuration * edited.repeat, time - dragSegment_->start);
    edited.duration = totalDuration / edited.repeat;
    if (auto* params = std::get_if<PointsParams>(&edited.params)) {
        const double spacing = std::max(MinimumDuration, edited.duration * 1e-9);
        for (size_t index = 0; index < params->points.size(); ++index) {
            const double lower = index == 0 ? 0.0 : params->points[index - 1].first + spacing;
            const double upper = index + 1 == params->points.size()
                                     ? edited.duration
                                     : params->points[index + 1].first - spacing;
            params->points[index].first =
                std::clamp(params->points[index].first, lower, std::max(lower, upper));
        }
    }
    emitEditedSegment(*dragSegment_, std::move(edited), true);
}

void CanvasWidget::updateHoverCursor(const QPointF& position) {
    const AxisRegion axis = axisAtPosition(position);
    if (axis != AxisRegion::None) {
        // The double arrow says which direction the wheel stretches here;
        // otherwise the axes would look as dead as the rest of the margin.
        setCursor(axis == AxisRegion::Time ? Qt::SizeHorCursor : Qt::SizeVerCursor);
    } else if (! plotRect().contains(position.toPoint())) {
        unsetCursor();
    } else if (pointAtPosition(position)) {
        setCursor(Qt::SizeAllCursor);
    } else if (boundaryAtPosition(position)) {
        setCursor(Qt::SizeHorCursor);
    } else {
        unsetCursor();
    }
}

void CanvasWidget::drawGrid(QPainter& painter, const QRect& area, const QColor& textColor) const {
    const bool darkMode = palette().color(QPalette::Base).lightness() < 128;
    QColor gridColor    = palette().color(QPalette::Mid);
    gridColor.setAlpha(darkMode ? 145 : 90);
    const GridStep grid = gridStep(area);

    // The snap subdivision goes down first and much fainter than the labeled
    // grid: it is a target for the mouse, not a reading aid. Showing it only
    // while snapping is on keeps the plot as clean as before for everyone who
    // switches it off, and makes it obvious what the handles will land on.
    if (snap_.enabled) {
        const GridStep fine = snapStep(area);
        painter.save();
        painter.setClipRect(area);
        painter.setPen(QPen(translucent(gridColor, darkMode ? 60 : 40), 1.0));
        // A fixed step can also be coarser than the labeled grid; what matters is
        // that every line a handle can land on is visible somewhere.
        const double xSpacing = fine.x / (view_.xMax - view_.xMin) * area.width();
        if (fine.x > 0.0 && fine.x != grid.x && xSpacing >= MinimumFineGridSpacing) {
            for (double x = std::ceil(view_.xMin / fine.x) * fine.x; x <= view_.xMax; x += fine.x) {
                const double pixel = xToPixel(x, area);
                painter.drawLine(QPointF(pixel, area.top()), QPointF(pixel, area.bottom()));
            }
        }
        const double ySpacing = fine.y / (view_.yMax - view_.yMin) * area.height();
        if (fine.y > 0.0 && fine.y != grid.y && ySpacing >= MinimumFineGridSpacing) {
            for (double y = std::ceil(view_.yMin / fine.y) * fine.y; y <= view_.yMax; y += fine.y) {
                const double pixel = yToPixel(y, area);
                painter.drawLine(QPointF(area.left(), pixel), QPointF(area.right(), pixel));
            }
        }
        painter.restore();
    }

    painter.setPen(QPen(gridColor, 1.0));
    const double firstX = std::ceil(view_.xMin / grid.x) * grid.x;
    for (double x = firstX; x <= view_.xMax + grid.x * 0.5; x += grid.x) {
        const double pixel = xToPixel(x, area);
        painter.drawLine(QPointF(pixel, area.top()), QPointF(pixel, area.bottom()));
        painter.setPen(textColor);
        const QRectF labelRect(pixel - 38.0, area.bottom() + 5.0, 76.0, 22.0);
        painter.drawText(
            labelRect, Qt::AlignHCenter | Qt::AlignTop, siLabel(x, QStringLiteral("s"))
        );
        painter.setPen(QPen(gridColor, 1.0));
    }

    const double firstY = std::ceil(view_.yMin / grid.y) * grid.y;
    for (double y = firstY; y <= view_.yMax + grid.y * 0.5; y += grid.y) {
        const double pixel = yToPixel(y, area);
        painter.drawLine(QPointF(area.left(), pixel), QPointF(area.right(), pixel));
        painter.setPen(textColor);
        const QRectF labelRect(1.0, pixel - 10.0, LeftMargin - 8.0, 20.0);
        painter.drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter, siLabel(y, {}));
        painter.setPen(QPen(gridColor, 1.0));
    }
}

void CanvasWidget::drawSelectedSegment(QPainter& painter, const QRect& area) const {
    const auto location = selectedSegmentLocation();
    if (! location) {
        return;
    }
    const Segment& segment = *segmentAt(*location);
    const double left      = xToPixel(location->start, area);
    const double right = xToPixel(location->start + segmentTotalDuration(segment), area);
    painter.save();
    painter.setClipRect(area);
    QColor highlight    = palette().color(QPalette::Highlight);
    const bool darkMode = palette().color(QPalette::Base).lightness() < 128;
    if (darkMode && highlight.lightness() < 155) {
        highlight = highlight.lighter(175);
    }
    // Just enough tint to read as "this segment": the dashed boundaries below
    // carry the information, so a stronger fill would only compete with the
    // curves inside it.
    highlight.setAlpha(darkMode ? 38 : 20);
    painter.fillRect(
        QRectF(QPointF(left, area.top()), QPointF(right, area.bottom())).normalized(), highlight
    );
    highlight.setAlpha(darkMode ? 235 : 180);
    painter.setPen(QPen(highlight, 1.5, Qt::DashLine));
    painter.drawLine(QPointF(left, area.top()), QPointF(left, area.bottom()));
    painter.drawLine(QPointF(right, area.top()), QPointF(right, area.bottom()));
    painter.restore();
}

void CanvasWidget::drawPointHandles(QPainter& painter, const QRect& area) const {
    const auto location = selectedSegmentLocation();
    if (! location) {
        return;
    }
    const Segment& segment = *segmentAt(*location);
    const auto* params     = std::get_if<PointsParams>(&segment.params);
    if (! params) {
        return;
    }

    if (params->points.empty() || ! (segment.duration > 0.0)) {
        return;
    }

    // Draw only the handles inside the visible time range, found by binary
    // search per visible repetition; drawing every repeat * points handle
    // costs seconds per paint on large imported segments. When the visible
    // handles are denser than one per ~4 px they are neither distinguishable
    // nor individually clickable, so skip them entirely.
    const double duration     = segment.duration;
    const int firstRepetition = static_cast<int>(std::clamp(
        std::floor((view_.xMin - location->start) / duration),
        0.0,
        static_cast<double>(segment.repeat - 1)
    ));
    const int lastRepetition  = static_cast<int>(std::clamp(
        std::floor((view_.xMax - location->start) / duration),
        0.0,
        static_cast<double>(segment.repeat - 1)
    ));

    const auto visibleRange   = [&](int repetition) {
        const double repetitionStart = location->start + repetition * duration;
        const auto begin             = std::lower_bound(
            params->points.begin(),
            params->points.end(),
            view_.xMin - repetitionStart,
            [](const auto& point, double tau) { return point.first < tau; }
        );
        const auto end = std::upper_bound(
            begin,
            params->points.end(),
            view_.xMax - repetitionStart,
            [](double tau, const auto& point) { return tau < point.first; }
        );
        return std::pair{begin, end};
    };

    size_t visibleCount = 0;
    for (int repetition = firstRepetition; repetition <= lastRepetition; ++repetition) {
        const auto [begin, end] = visibleRange(repetition);
        visibleCount += static_cast<size_t>(std::distance(begin, end));
    }
    if (visibleCount > static_cast<size_t>(std::max(area.width() / 4, 64))) {
        return;
    }

    painter.save();
    painter.setClipRect(area);
    for (int repetition = firstRepetition; repetition <= lastRepetition; ++repetition) {
        const double repetitionStart = location->start + repetition * duration;
        const auto [begin, end]      = visibleRange(repetition);
        for (auto it = begin; it != end; ++it) {
            const size_t index = static_cast<size_t>(std::distance(params->points.begin(), it));
            const QPointF handle(
                xToPixel(repetitionStart + it->first, area), yToPixel(it->second, area)
            );
            const bool selected = selectedPointIndex_ && *selectedPointIndex_ == index;
            QColor fill         = palette().color(selected ? QPalette::Highlight : QPalette::Base);
            painter.setBrush(fill);
            painter.setPen(QPen(palette().color(QPalette::Highlight), selected ? 2.0 : 1.0));
            painter.drawEllipse(handle, selected ? 5.0 : 4.0, selected ? 5.0 : 4.0);
        }
    }
    painter.restore();
}

QString CanvasWidget::leafLabel(const LayerPath& layerPath) const {
    // Names of every node on the way down, so a layer that only says "Sine"
    // still says which group it sits in. The fallback for an unnamed node
    // repeats what the structure tree shows, so both name the same thing.
    QStringList parts;
    const std::vector<WaveLayer>* level = &document_.layers;
    for (const size_t index : layerPath) {
        if (! level || index >= level->size()) {
            break;
        }
        const WaveLayer& node = (*level)[index];
        parts += node.name.isEmpty()
                     ? (isGroup(node) ? tr("Group %1") : tr("Layer %1")).arg(index + 1)
                     : node.name;
        level = isGroup(node) ? &node.children : nullptr;
    }
    return parts.join(QStringLiteral(" / "));
}

void CanvasWidget::drawLegend(
    QPainter& painter, const QRect& area, const QColor& totalColor
) const {
    if (! legendVisible_ || leafPaths_.empty()) {
        return;
    }

    constexpr int Inset        = 8;
    constexpr int Padding      = 7;
    constexpr int SwatchWidth  = 18;
    constexpr int Gap          = 7;

    const QPalette palette     = this->palette();
    const bool darkMode        = palette.color(QPalette::Base).lightness() < 128;
    const QFontMetrics metrics = painter.fontMetrics();
    const int rowHeight        = metrics.height() + 2;
    const int textLimit        = std::max(60, area.width() * 2 / 5);

    struct Row {
        QString text;
        QColor color;
        double width;
        bool emphasized;
        bool muted;  // does not reach the result: this leaf or one of its groups is off
    };
    std::vector<Row> rows;
    rows.reserve(leafPaths_.size() + 1);
    // The result first: it is the curve the document is about, and the one drawn
    // on top of everything else.
    rows.push_back({tr("Result"), totalColor, darkMode ? 3.0 : 2.0, true, false});
    for (size_t index = 0; index < leafPaths_.size(); ++index) {
        const bool emphasized = leafEmphasized(index);
        rows.push_back(
            {metrics.elidedText(leafLabel(leafPaths_[index]), Qt::ElideMiddle, textLimit),
             theme::curveColor(index, palette),
             emphasized ? EmphasizedCurveWidth : 1.0,
             emphasized,
             ! leafContributes(leafPaths_[index])}
        );
    }

    // A document with more layers than the plot is tall loses the tail rather
    // than the plot: the count says how many are missing instead of pretending
    // the list is complete.
    const size_t fittingRows = static_cast<size_t>(
        std::max(1, (area.height() - 2 * Inset - 2 * Padding) / std::max(rowHeight, 1))
    );
    if (rows.size() > fittingRows) {
        const size_t hidden = rows.size() - fittingRows + 1;
        rows.resize(fittingRows > 0 ? fittingRows - 1 : 0);
        rows.push_back({tr("+ %1 more").arg(hidden), QColor(), 0.0, false, true});
    }

    QFont boldFont = painter.font();
    boldFont.setBold(true);
    const QFontMetrics boldMetrics(boldFont);
    int textWidth = 0;
    for (const Row& row : rows) {
        textWidth = std::max(
            textWidth, (row.emphasized ? boldMetrics : metrics).horizontalAdvance(row.text)
        );
    }

    const int boxWidth  = 2 * Padding + SwatchWidth + Gap + textWidth;
    const int boxHeight = 2 * Padding + static_cast<int>(rows.size()) * rowHeight;
    const QRect box(area.right() - Inset - boxWidth, area.top() + Inset, boxWidth, boxHeight);

    painter.save();
    painter.setClipRect(area);
    painter.setRenderHint(QPainter::Antialiasing, false);
    // Opaque enough to read a name over a dense curve, translucent enough to
    // show that the plot continues underneath.
    painter.setBrush(translucent(palette.color(QPalette::Base), darkMode ? 225 : 215));
    painter.setPen(QPen(translucent(palette.color(QPalette::Mid), 150), 1.0));
    painter.drawRect(box);

    int y = box.top() + Padding;
    for (const Row& row : rows) {
        const QRect rowRect(box.left() + Padding, y, box.width() - 2 * Padding, rowHeight);
        if (row.color.isValid()) {
            QColor swatch = row.color;
            swatch.setAlpha(row.muted ? 90 : 255);
            painter.setPen(QPen(swatch, row.width, row.muted ? Qt::DashLine : Qt::SolidLine));
            const double middle = rowRect.center().y() + 0.5;
            painter.drawLine(
                QPointF(rowRect.left(), middle), QPointF(rowRect.left() + SwatchWidth, middle)
            );
        }
        painter.setFont(row.emphasized ? boldFont : font());
        painter.setPen(
            palette.color(row.muted ? QPalette::Disabled : QPalette::Active, QPalette::Text)
        );
        painter.drawText(
            rowRect.adjusted(SwatchWidth + Gap, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, row.text
        );
        y += rowHeight;
    }
    painter.restore();
}

void CanvasWidget::leaveEvent(QEvent* event) {
    if (dragMode_ == DragMode::None) {
        unsetCursor();
    }
    emit cursorReadout(std::numeric_limits<double>::quiet_NaN(), 0.0);
    QWidget::leaveEvent(event);
}

void CanvasWidget::updateCursorReadout(const QPointF& position) {
    const QRect area = plotRect();
    if (! area.contains(position.toPoint())) {
        emit cursorReadout(std::numeric_limits<double>::quiet_NaN(), 0.0);
        return;
    }
    const double time = pixelToX(position.x(), area);
    emit cursorReadout(time, valueAtTime(time));
}

}  // namespace zwe
