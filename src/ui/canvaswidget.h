// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QColor>
#include <QPoint>
#include <QString>
#include <QWidget>
#include <optional>
#include <vector>

#include "core/wavedocument.h"

class QMouseEvent;
class QPaintEvent;
class QPainter;
class QKeyEvent;
class QWheelEvent;

namespace zwe {

// How interactive canvas edits are quantized. Only edits made with the mouse or
// the arrow keys are affected: a value read from a file or typed into the
// property panel is never changed.
struct SnapSettings {
    bool enabled = false;
    // false: the step is one cell of the drawn grid divided by divisions, so it
    // follows the zoom level and stays a round number.
    // true: timeStep and valueStep are used as they are, independent of the
    // zoom. A step of zero leaves that axis unsnapped.
    bool fixedStep                                                   = false;
    int divisions                                                    = 10;
    double timeStep                                                  = 0.0;
    double valueStep                                                 = 0.0;

    friend bool operator==(const SnapSettings&, const SnapSettings&) = default;
};

class CanvasWidget final : public QWidget {
    Q_OBJECT

public:
    explicit CanvasWidget(QWidget* parent = nullptr);

    // resetView is true for a newly opened document; live edits preserve the
    // current pan/zoom so direct manipulation remains stable while dragging.
    void setDocument(const WaveDocument& document, bool resetView = true);
    void fitToDocument();
    // Fits only when the document extends beyond the current horizontal view.
    // Intended for insert operations that should preserve a user's zoom while
    // the newly inserted segment remains visible.
    void ensureDocumentVisible();
    // An empty path selects no layer; only a leaf layer can hold the segment
    // that direct manipulation acts on.
    void setSelectedLayerPath(const LayerPath& layerPath);
    void setSelectedSegmentIndex(std::optional<size_t> segmentIndex);

    // Snapping of point drags, boundary drags, point insertion and the arrow-key
    // nudge. Disabled until set, so the widget on its own behaves as it always
    // did; the GridBar owns the stored preference. Ctrl inverts the mode for a
    // single edit.
    void setSnapSettings(const SnapSettings& settings);
    const SnapSettings& snapSettings() const { return snap_; }

    // The legend that maps each layer's color and name to its curve. Shown until
    // set otherwise; MainWindow owns the stored preference.
    void setLegendVisible(bool visible);
    bool legendVisible() const { return legendVisible_; }

    // The point handle the next arrow-key nudge or Delete acts on. Set by
    // clicking a handle, and by the points table of the property panel.
    void setSelectedPointIndex(std::optional<size_t> pointIndex);
    std::optional<size_t> selectedPointIndex() const { return selectedPointIndex_; }

signals:
    // Emitted while display samples are being recalculated, but only once a
    // rebuild has taken noticeably long; always ends with percent == 100.
    void recalculationProgress(int percent);
    void cursorReadout(double time, double value);
    void segmentSelected(const LayerPath& layerPath, size_t segmentIndex);
    // A click on the plot background, which selects nothing. Panning does not
    // count: that is a drag, and it keeps the selection.
    void selectionCleared();
    // The point handle that edits act on, -1 for none. An int rather than an
    // optional so the signal stays usable from QSignalSpy and from a queued
    // connection.
    void selectedPointChanged(int pointIndex);
    // The snap step now in effect, so the GridBar can show what the automatic
    // mode currently rounds to. Only emitted when it actually changes.
    void snapStepChanged(double timeStep, double valueStep);
    // continuous is true for a drag stream, so history can merge the stream
    // into one undo operation even when a boundary edit changes several fields.
    void segmentChanged(
        const LayerPath& layerPath, size_t segmentIndex, const Segment& segment, bool continuous
    );

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    struct ViewRange {
        double xMin = 0.0;
        double xMax = 1.0;
        double yMin = -1.0;
        double yMax = 1.0;
    };

    // start is the segment's begin in document time, accumulated from the
    // layer's first segment. That is where the layer's content sits as long as
    // no ancestor loops or holds its timeline; direct manipulation of a segment
    // inside such a group would address the wrong repetition, which is why the
    // plot places its handles from the layer's own timeline only.
    struct SegmentLocation {
        LayerPath layerPath;
        size_t segmentIndex;
        double start;
    };

    // The distance between two grid lines, in document time and in values.
    struct GridStep {
        double x;
        double y;
    };

    enum class DragMode { None, Pan, Point, Boundary };

    // The labeled strips left of and below the plot. Both are handles for the
    // axis they belong to: the wheel scales it, a drag shifts it, a double-click
    // fits it - which is how one stretches or follows time without touching the
    // amplitude and the other way round.
    enum class AxisRegion { None, Time, Value };

    QRect plotRect() const;
    // The two halves of fitToDocument(), for fitting one axis on its own.
    // fitTimeAxis() covers the whole document; fitValueAxis() covers the values
    // inside the visible time window, so the order of the two matters.
    void fitTimeAxis();
    void fitValueAxis();
    // Which axis strip the position falls into; None inside the plot, in the
    // corner between the two strips, and anywhere else outside.
    AxisRegion axisAtPosition(const QPointF& position) const;
    // Scales the named axes by factor - below one to zoom in - around position,
    // which keeps the time and value it points at while the rest spreads out.
    void zoomView(bool time, bool value, double factor, const QPointF& position);
    // The spacing of the labeled grid, and of the finer snap grid derived from
    // it. Both painting and snapping go through these, so a handle always lands
    // on a line that is actually drawn.
    GridStep gridStep(const QRect& area) const;
    GridStep snapStep(const QRect& area) const;
    bool snappingActive(Qt::KeyboardModifiers modifiers) const;
    void notifySnapStep();
    // Point selection changes come from a click, from the points table and from a
    // document that no longer holds the point. They all report through here, so
    // the table can follow the canvas and vice versa.
    void setPointSelection(std::optional<size_t> pointIndex);
    // Moves the selected point to the next grid position; coarse walks whole
    // grid cells instead of snap steps. False when nothing moved.
    bool nudgeSelectedPoint(int key, bool coarse);
    void rebuildDisplaySamples();
    void updateCursorReadout(const QPointF& position);
    double xToPixel(double time, const QRect& rect) const;
    double yToPixel(double value, const QRect& rect) const;
    double pixelToX(double pixel, const QRect& rect) const;
    double pixelToY(double pixel, const QRect& rect) const;
    double valueAtTime(double time) const;
    // The selected node, the selected leaf's segment, and the segment a
    // location points at; nullptr whenever the selection does not resolve.
    const WaveLayer* selectedLayer() const;
    const Segment* selectedSegment() const;
    const Segment* segmentAt(const SegmentLocation& location) const;
    std::optional<SegmentLocation> selectedSegmentLocation() const;
    std::optional<SegmentLocation> segmentAtTime(const LayerPath& layerPath, double time) const;
    // Whether leaf number index is part of the current selection - the selected
    // node itself, or any leaf below a selected group. Those curves are drawn
    // wider and opaque while the others step back.
    bool leafEmphasized(size_t index) const;
    // Whether any curve is singled out at all. Nothing is dimmed while none is:
    // a selected group has no curve of its own to point at.
    bool anyLeafEmphasized() const;
    // Whether a leaf contributes to the total: it and all its groups are enabled.
    bool leafContributes(const LayerPath& layerPath) const;
    // The legend entry for a leaf: the names of its groups and its own, joined.
    QString leafLabel(const LayerPath& layerPath) const;
    // The displayed value of leaf number index at the given time, taken from the
    // samples the plot has drawn - of the candidates in that pixel column the
    // one closest to targetValue. nullopt outside the sampled window.
    std::optional<double> displayedLeafValue(size_t index, double time, double targetValue) const;
    std::optional<SegmentLocation> segmentAtPosition(const QPointF& position) const;
    std::optional<size_t> pointAtPosition(const QPointF& position) const;
    std::optional<SegmentLocation> boundaryAtPosition(const QPointF& position) const;
    void emitEditedSegment(
        const SegmentLocation& location, Segment segment, bool continuous = false
    );
    void updateHoverCursor(const QPointF& position);
    void dragPoint(const QPointF& position, bool snap);
    void dragBoundary(const QPointF& position, bool snap);
    void drawGrid(QPainter& painter, const QRect& area, const QColor& textColor) const;
    void drawSelectedSegment(QPainter& painter, const QRect& area) const;
    void drawPointHandles(QPainter& painter, const QRect& area) const;
    void drawLegend(QPainter& painter, const QRect& area, const QColor& totalColor) const;

    WaveDocument document_;
    // Display samples cover only the visible time window (clamped to the
    // document), at most ~2 samples per pixel: sample k of a layer is at
    // displayStart_ + k / displayRate_.
    double displayStart_ = 0.0;
    double displayRate_  = 0.0;
    std::vector<double> totalSamples_;
    // One curve per leaf layer, in the order of leafPaths(document_.layers);
    // leafPaths_ is that order, kept so a hit test can name what it found.
    std::vector<std::vector<double>> leafSamples_;
    std::vector<LayerPath> leafPaths_;
    LayerPath selectedLayerPath_;
    std::optional<size_t> selectedSegmentIndex_;
    std::optional<size_t> selectedPointIndex_;
    ViewRange view_;
    QPoint panStart_;
    ViewRange panStartView_;
    // The axis the current pan is locked to; None pans both, which is what a drag
    // over the plot itself does.
    AxisRegion panAxis_ = AxisRegion::None;
    // Whether the current pan actually moved the view, which tells a drag from a
    // click on the background.
    bool panMoved_      = false;
    bool legendVisible_ = true;
    DragMode dragMode_  = DragMode::None;
    std::optional<SegmentLocation> dragSegment_;
    SnapSettings snap_;
    // The step snapStepChanged() last reported, so panning and repainting do not
    // re-announce an unchanged step.
    GridStep notifiedSnapStep_{0.0, 0.0};
};

}  // namespace zwe
