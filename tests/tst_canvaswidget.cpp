// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include <QtTest>

#include "ui/canvaswidget.h"
#include "ui/theme.h"

using namespace zwe;

namespace {

WaveDocument twoSegmentDocument() {
    WaveDocument document;
    document.sampleRate = 100.0;

    Segment first;
    first.duration = 1.0;
    first.params   = DcParams{.value = 2.0};

    Segment second;
    second.duration = 1.0;
    second.params   = RampParams{.startValue = -1.0, .endValue = 3.0};

    document.layers.push_back(
        WaveLayer{.name = QStringLiteral("Layer"), .segments = {first, second}}
    );
    return document;
}

// A one second segment whose three points span the full value range, so fitting
// it puts the view at t = -0.06 .. 1.06 and value = -0.06 .. 1.06.
WaveDocument pointsDocument() {
    WaveDocument document;
    document.sampleRate = 1000.0;

    Segment segment;
    segment.duration = 1.0;
    segment.params   = PointsParams{
        .interp = PointsParams::Interp::Linear, .points = {{0.0, 0.0}, {0.5, 1.0}, {1.0, 0.0}}
    };

    document.layers.push_back(WaveLayer{.name = QStringLiteral("Layer"), .segments = {segment}});
    return document;
}

// One layer at a value rate of 10 1/s that is 0 for half a second and 1 for
// the other half, or ramps from 0 to 1 over the whole second: few enough values
// that each one is drawn several pixels wide. Both fit to the same view as
// pointsDocument(), so plotPosition() below applies.
WaveDocument lowRateDocument(bool ramp) {
    WaveDocument document;
    document.sampleRate = 10.0;

    std::vector<Segment> segments;
    if (ramp) {
        Segment segment;
        segment.duration = 1.0;
        segment.params   = RampParams{.startValue = 0.0, .endValue = 1.0};
        segments.push_back(segment);
    } else {
        Segment low;
        low.duration = 0.5;
        low.params   = DcParams{.value = 0.0};
        Segment high;
        high.duration = 0.5;
        high.params   = DcParams{.value = 1.0};
        segments      = {low, high};
    }
    document.layers.push_back(WaveLayer{.name = QStringLiteral("Layer"), .segments = segments});
    return document;
}

// Whether a curve is drawn at or right next to the given pixel. Curves are
// saturated colors, the grid and the frame are not.
bool curveNear(const QImage& image, const QPoint& position) {
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const QPoint pixel = position + QPoint(dx, dy);
            if (image.rect().contains(pixel) && image.pixelColor(pixel).hsvSaturation() > 60) {
                return true;
            }
        }
    }
    return false;
}

// Where a point of pointsDocument() lands on screen. Margins are treated as
// black-box padding, as in boundaryPosition() below.
QPoint plotPosition(const CanvasWidget& canvas, double time, double value) {
    constexpr double leftMargin   = 78.0;
    constexpr double rightMargin  = 14.0;
    constexpr double topMargin    = 16.0;
    constexpr double bottomMargin = 38.0;
    constexpr double viewMinimum  = -0.06;
    constexpr double viewSpan     = 1.12;
    const double plotWidth        = canvas.width() - leftMargin - rightMargin;
    const double plotHeight       = canvas.height() - topMargin - bottomMargin;
    return {
        qRound(leftMargin + (time - viewMinimum) / viewSpan * plotWidth),
        qRound(canvas.height() - 1.0 - bottomMargin - (value - viewMinimum) / viewSpan * plotHeight)
    };
}

// Two constant layers inside a group, plus a ramp next to it: three curves whose
// colors and emphasis can be told apart in a grabbed image. The ramp makes the
// total sweep 0 .. 6, so fitting the view to it keeps the two constant curves
// in sight.
WaveDocument groupedDocument() {
    WaveDocument document;
    document.sampleRate      = 100.0;

    const auto constantLayer = [](const QString& name, double value) {
        Segment segment;
        segment.duration = 1.0;
        segment.params   = DcParams{.value = value};
        return WaveLayer{.name = name, .segments = {segment}};
    };

    Segment ramp;
    ramp.duration = 1.0;
    ramp.params   = RampParams{.startValue = -5.0, .endValue = 1.0};
    document.layers.push_back(WaveLayer{.name = QStringLiteral("Alone"), .segments = {ramp}});
    WaveLayer group{.name = QStringLiteral("Group"), .kind = LayerKind::Group};
    group.children.push_back(constantLayer(QStringLiteral("Inner A"), 2.0));
    group.children.push_back(constantLayer(QStringLiteral("Inner B"), 3.0));
    document.layers.push_back(std::move(group));
    return document;
}

// A layer of two constant segments next to a flat one. Every curve sits at its
// own value, and none of them coincides with the total, so each one can be
// counted in a grabbed image on its own.
WaveDocument segmentedDocument() {
    WaveDocument document;
    document.sampleRate = 100.0;

    Segment flat;
    flat.duration = 2.0;
    flat.params   = DcParams{.value = 1.0};

    Segment low;
    low.duration = 1.0;
    low.params   = DcParams{.value = 2.0};

    Segment high;
    high.duration = 1.0;
    high.params   = DcParams{.value = 3.0};

    document.layers.push_back(WaveLayer{.name = QStringLiteral("Flat"), .segments = {flat}});
    document.layers.push_back(WaveLayer{.name = QStringLiteral("Steps"), .segments = {low, high}});
    return document;
}

// How much of the image is drawn in a curve's color: a curve that is emphasized
// is both wider and fully opaque, so it covers strictly more pixels of its own
// hue than the same curve dimmed. Blending against the background keeps the hue
// and only takes saturation away, which is why the hue is what is counted.
int coloredPixels(const QImage& image, const QColor& color) {
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.hsvSaturation() > 60 && std::abs(pixel.hsvHue() - color.hsvHue()) < 8) {
                ++count;
            }
        }
    }
    return count;
}

// A point on the time scale below the plot, and one on the value scale left of
// it. Margins are treated as black-box padding, as in boundaryPosition() below.
QPoint timeScalePosition(const CanvasWidget& canvas) {
    return {canvas.width() / 2, canvas.height() - 10};
}

QPoint valueScalePosition(const CanvasWidget& canvas) {
    return {10, canvas.height() / 2};
}

// Turns the wheel over the given position, one notch of 120 units at a time;
// a positive count zooms in.
void turnWheel(CanvasWidget& canvas, const QPoint& position, int notches) {
    for (int step = 0; step < qAbs(notches); ++step) {
        QWheelEvent event(
            QPointF(position),
            canvas.mapToGlobal(QPointF(position)),
            QPoint(),
            QPoint(0, notches > 0 ? 120 : -120),
            Qt::NoButton,
            Qt::NoModifier,
            Qt::NoScrollPhase,
            false
        );
        QCoreApplication::sendEvent(&canvas, &event);
    }
}

// Moves the pointer with no button held. QTest::mouseMove warps the physical
// cursor in that case, which only reaches the widget while nothing covers it on
// screen; a synthetic event is what the widget sees either way and does not
// depend on the desktop the test happens to run on.
void hover(CanvasWidget& canvas, const QPoint& position) {
    QMouseEvent move(
        QEvent::MouseMove,
        QPointF(position),
        canvas.mapToGlobal(QPointF(position)),
        Qt::NoButton,
        Qt::NoButton,
        Qt::NoModifier
    );
    QCoreApplication::sendEvent(&canvas, &move);
}

QPoint boundaryPosition(const CanvasWidget& canvas, double normalizedDocumentTime) {
    // Canvas margins are deliberately treated as black-box padding here. The
    // fitted document has a 6% margin at both ends.
    constexpr double leftMargin        = 78.0;
    constexpr double rightMargin       = 14.0;
    const double plotWidth             = canvas.width() - leftMargin - rightMargin;
    const double normalizedWithMargins = (normalizedDocumentTime + 0.06) / 1.12;
    return {qRound(leftMargin + normalizedWithMargins * plotWidth), canvas.height() / 2};
}

}  // namespace

class TestCanvasWidget : public QObject {
    Q_OBJECT

private slots:
    void boundaryHoverShowsResizeCursor();
    void boundaryDragEditsCapturedSegment();
    void curveSelectionUsesWideInvisibleCorridor();
    void fitAndEnsureVisibleAreImmediatelyUsable();
    void pointDragSnapsToTheGrid();
    void controlSuspendsSnappingForOneDrag();
    void arrowKeysWalkTheSelectedPointAlongTheGrid();
    void automaticSnapStepFollowsTheFineness();
    void theSnapGridIsOnlyPaintedWhileSnappingIsOn();
    void onlyTheSelectedLayersCurveIsEmphasized();
    void aSelectedSegmentEmphasizesOnlyItsOwnStretch();
    void clickingThePlotBackgroundClearsTheSelection();
    void panningKeepsTheSelection();
    void theAxisUnderThePointerScalesOnItsOwn();
    void draggingAScaleShiftsOnlyItsAxis();
    void doubleClickingAScaleFitsOnlyItsAxis();
    void hoveringAnAxisShowsTheScalingCursor();
    void theLegendCanBeSwitchedOff();
    void eachValueIsHeldUntilTheNextOne();
    void theCursorReadsTheHeldValue();
};

void TestCanvasWidget::curveSelectionUsesWideInvisibleCorridor() {
    CanvasWidget canvas;
    canvas.resize(640, 360);
    canvas.setDocument(twoSegmentDocument());
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));

    QSignalSpy selected(&canvas, &CanvasWidget::segmentSelected);
    // The first segment is the horizontal y=2 line. Click 12 pixels away:
    // outside a normal painted stroke, but inside the selection corridor.
    constexpr double leftMargin   = 78.0;
    constexpr double rightMargin  = 14.0;
    constexpr double topMargin    = 16.0;
    constexpr double bottomMargin = 38.0;
    const double plotWidth        = canvas.width() - leftMargin - rightMargin;
    const double plotHeight       = canvas.height() - topMargin - bottomMargin;
    const int x                   = qRound(leftMargin + ((0.5 + 0.12) / 2.24) * plotWidth);
    const int lineY               = qRound(topMargin + ((3.24 - 2.0) / 4.48) * plotHeight);
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, QPoint(x, lineY + 12));

    QCOMPARE(selected.count(), 1);
    QCOMPARE(qvariant_cast<LayerPath>(selected.first().at(0)), LayerPath{0});
    QCOMPARE(selected.first().at(1).toULongLong(), qulonglong{0});
}

void TestCanvasWidget::boundaryHoverShowsResizeCursor() {
    CanvasWidget canvas;
    canvas.resize(640, 360);
    canvas.setDocument(twoSegmentDocument());
    canvas.setSelectedLayerPath({0});
    canvas.setSelectedSegmentIndex(0);
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));

    hover(canvas, boundaryPosition(canvas, 0.5));
    QCOMPARE(canvas.cursor().shape(), Qt::SizeHorCursor);

    hover(canvas, QPoint(90, 30));
    QCOMPARE(canvas.cursor().shape(), Qt::ArrowCursor);
}

void TestCanvasWidget::boundaryDragEditsCapturedSegment() {
    CanvasWidget canvas;
    canvas.resize(640, 360);
    canvas.setDocument(twoSegmentDocument());
    canvas.setSelectedLayerPath({0});
    canvas.setSelectedSegmentIndex(0);
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));

    LayerPath changedLayer;
    size_t changedSegment = std::numeric_limits<size_t>::max();
    Segment changed;
    connect(
        &canvas,
        &CanvasWidget::segmentChanged,
        this,
        [&](const LayerPath& layer, size_t segment, const Segment& value, bool) {
            changedLayer   = layer;
            changedSegment = segment;
            changed        = value;
        }
    );

    const QPoint secondBoundary = boundaryPosition(canvas, 1.0);
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, secondBoundary);
    QTest::mouseMove(&canvas, secondBoundary - QPoint(25, 0), 20);
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, secondBoundary - QPoint(25, 0));

    QCOMPARE(changedLayer, LayerPath{0});
    QCOMPARE(changedSegment, size_t{1});
    QVERIFY(std::holds_alternative<RampParams>(changed.params));
    QCOMPARE(std::get<RampParams>(changed.params).startValue, -1.0);
    QCOMPARE(std::get<RampParams>(changed.params).endValue, 3.0);
}

void TestCanvasWidget::fitAndEnsureVisibleAreImmediatelyUsable() {
    CanvasWidget canvas;
    canvas.resize(640, 360);
    canvas.setDocument(twoSegmentDocument());
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));

    canvas.fitToDocument();
    canvas.ensureDocumentVisible();
    QCoreApplication::processEvents();
    QVERIFY(! canvas.grab().isNull());
}

void TestCanvasWidget::pointDragSnapsToTheGrid() {
    CanvasWidget canvas;
    canvas.resize(640, 360);
    canvas.setDocument(pointsDocument());
    canvas.setSelectedLayerPath({0});
    canvas.setSelectedSegmentIndex(0);
    // A fixed step keeps the expected result independent of the zoom level.
    canvas.setSnapSettings(
        SnapSettings{.enabled = true, .fixedStep = true, .timeStep = 0.1, .valueStep = 0.25}
    );
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));

    Segment changed;
    connect(
        &canvas,
        &CanvasWidget::segmentChanged,
        this,
        [&](const LayerPath&, size_t, const Segment& value, bool) { changed = value; }
    );

    // Grab the middle handle and drop it between grid positions.
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, plotPosition(canvas, 0.5, 1.0));
    const QPoint target = plotPosition(canvas, 0.63, 0.42);
    QTest::mouseMove(&canvas, target, 20);
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, target);

    const auto& points = std::get<PointsParams>(changed.params).points;
    QCOMPARE(points.size(), size_t{3});
    QCOMPARE(points[1].first, 0.6);
    QCOMPARE(points[1].second, 0.5);
    // The neighbours are left alone.
    QCOMPARE(points[0].first, 0.0);
    QCOMPARE(points[2].first, 1.0);
}

void TestCanvasWidget::controlSuspendsSnappingForOneDrag() {
    CanvasWidget canvas;
    canvas.resize(640, 360);
    canvas.setDocument(pointsDocument());
    canvas.setSelectedLayerPath({0});
    canvas.setSelectedSegmentIndex(0);
    canvas.setSnapSettings(
        SnapSettings{.enabled = true, .fixedStep = true, .timeStep = 0.1, .valueStep = 0.25}
    );
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));

    Segment changed;
    connect(
        &canvas,
        &CanvasWidget::segmentChanged,
        this,
        [&](const LayerPath&, size_t, const Segment& value, bool) { changed = value; }
    );

    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, plotPosition(canvas, 0.5, 1.0));
    const QPoint target = plotPosition(canvas, 0.63, 0.42);
    QTest::mouseMove(&canvas, target, 20);
    // QTest::mouseMove does not carry modifiers, so the drag event is sent
    // directly - which is what the widget sees when Ctrl is held down.
    QMouseEvent move(
        QEvent::MouseMove,
        QPointF(target),
        canvas.mapToGlobal(QPointF(target)),
        Qt::NoButton,
        Qt::LeftButton,
        Qt::ControlModifier
    );
    QCoreApplication::sendEvent(&canvas, &move);
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::ControlModifier, target);

    const auto& points = std::get<PointsParams>(changed.params).points;
    QCOMPARE(points.size(), size_t{3});
    QVERIFY(qAbs(points[1].first - 0.63) < 0.01);
    QVERIFY(qAbs(points[1].second - 0.42) < 0.01);
}

void TestCanvasWidget::arrowKeysWalkTheSelectedPointAlongTheGrid() {
    CanvasWidget canvas;
    WaveDocument document = pointsDocument();
    canvas.resize(640, 360);
    canvas.setDocument(document);
    canvas.setSelectedLayerPath({0});
    canvas.setSelectedSegmentIndex(0);
    canvas.setSelectedPointIndex(1);
    canvas.setSnapSettings(
        SnapSettings{.enabled = true, .fixedStep = true, .timeStep = 0.1, .valueStep = 0.25}
    );
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));
    canvas.setFocus();

    // Feeding the edit back is what MainWindow does, and what the next key press
    // has to start from.
    connect(
        &canvas,
        &CanvasWidget::segmentChanged,
        this,
        [&](const LayerPath& layer, size_t segment, const Segment& value, bool) {
            layerAtPath(document.layers, layer)->segments[segment] = value;
            canvas.setDocument(document, false);
        }
    );
    const auto point = [&document] {
        return std::get<PointsParams>(document.layers[0].segments[0].params).points[1];
    };

    QTest::keyClick(&canvas, Qt::Key_Right);
    QCOMPARE(point().first, 0.6);
    QTest::keyClick(&canvas, Qt::Key_Right);
    QCOMPARE(point().first, 0.7);
    QTest::keyClick(&canvas, Qt::Key_Up);
    QCOMPARE(point().second, 1.25);
    // Shift walks ten fixed steps at once, so it stays on the fine grid.
    QTest::keyClick(&canvas, Qt::Key_Up, Qt::ShiftModifier);
    QCOMPARE(point().second, 2.5);
}

void TestCanvasWidget::automaticSnapStepFollowsTheFineness() {
    CanvasWidget canvas;
    canvas.resize(640, 360);
    canvas.setDocument(pointsDocument());
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));

    QSignalSpy steps(&canvas, &CanvasWidget::snapStepChanged);
    canvas.setSnapSettings(SnapSettings{.enabled = true, .divisions = 1});
    QCOMPARE(steps.count(), 1);
    const double gridTime = steps.takeFirst().at(0).toDouble();
    QVERIFY(gridTime > 0.0);

    canvas.setSnapSettings(SnapSettings{.enabled = true, .divisions = 10});
    QCOMPARE(steps.count(), 1);
    // One tenth of a grid cell, which is what the grid bar shows as the step in
    // effect.
    QVERIFY(qFuzzyCompare(steps.takeFirst().at(0).toDouble() * 10.0, gridTime));
}

void TestCanvasWidget::theSnapGridIsOnlyPaintedWhileSnappingIsOn() {
    CanvasWidget canvas;
    canvas.resize(640, 360);
    canvas.setDocument(pointsDocument());
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));

    const QImage plain = canvas.grab().toImage();
    canvas.setSnapSettings(SnapSettings{.enabled = true, .divisions = 10});
    QCoreApplication::processEvents();
    const QImage snapping = canvas.grab().toImage();
    // The subdivision the handles land on has to be visible, otherwise the plot
    // does not say where a point will go.
    QVERIFY(plain != snapping);

    // A subdivision denser than a few pixels would be a solid fill and hide the
    // curve, so it is snapped to but not drawn.
    canvas.setSnapSettings(
        SnapSettings{.enabled = true, .fixedStep = true, .timeStep = 1e-6, .valueStep = 1e-6}
    );
    QCoreApplication::processEvents();
    QCOMPARE(canvas.grab().toImage(), plain);
}

void TestCanvasWidget::onlyTheSelectedLayersCurveIsEmphasized() {
    CanvasWidget canvas;
    canvas.resize(640, 360);
    canvas.setDocument(groupedDocument());
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));

    // Leaves are numbered depth first: Alone, Inner A, Inner B.
    const QColor innerA = theme::curveColor(1, canvas.palette());

    canvas.setSelectedLayerPath(LayerPath{1, 0});
    QCoreApplication::processEvents();
    const int selected = coloredPixels(canvas.grab().toImage(), innerA);

    // Its neighbor selected: Inner A is now one of the curves that step back.
    canvas.setSelectedLayerPath(LayerPath{0});
    QCoreApplication::processEvents();
    const int dimmed = coloredPixels(canvas.grab().toImage(), innerA);

    // The group around it selected. A group has no curve of its own, so it
    // singles out none of its children - every curve keeps its plain weight
    // instead of the whole subtree being picked out.
    canvas.setSelectedLayerPath(LayerPath{1});
    QCoreApplication::processEvents();
    const int wholeGroup = coloredPixels(canvas.grab().toImage(), innerA);

    QVERIFY(selected > wholeGroup);
    QVERIFY(wholeGroup > dimmed);
}

void TestCanvasWidget::aSelectedSegmentEmphasizesOnlyItsOwnStretch() {
    CanvasWidget canvas;
    canvas.resize(640, 360);
    canvas.setDocument(segmentedDocument());
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));

    const QColor steps = theme::curveColor(1, canvas.palette());
    canvas.setSelectedLayerPath(LayerPath{1});
    canvas.setSelectedSegmentIndex(std::nullopt);
    QCoreApplication::processEvents();
    const int wholeLayer = coloredPixels(canvas.grab().toImage(), steps);

    // One of its two segments: the other half of the curve goes back to the
    // weight of every other curve, so less of the color is on screen.
    canvas.setSelectedSegmentIndex(0);
    QCoreApplication::processEvents();
    const int oneSegment = coloredPixels(canvas.grab().toImage(), steps);
    QVERIFY(oneSegment < wholeLayer);

    // But still more than with nothing of that layer picked out at all.
    canvas.setSelectedLayerPath(LayerPath{0});
    QCoreApplication::processEvents();
    QVERIFY(coloredPixels(canvas.grab().toImage(), steps) < oneSegment);
}

void TestCanvasWidget::clickingThePlotBackgroundClearsTheSelection() {
    CanvasWidget canvas;
    canvas.resize(640, 360);
    canvas.setDocument(pointsDocument());
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));
    canvas.setSelectedLayerPath(LayerPath{0});

    QSignalSpy cleared(&canvas, &CanvasWidget::selectionCleared);
    // High above the curve, and far enough from t = 0 to miss the segment
    // boundary: nothing there to select.
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, plotPosition(canvas, 0.05, 1.0));
    QCOMPARE(cleared.count(), 1);

    // Nothing is selected now, so clicking the background again changes nothing.
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, plotPosition(canvas, 0.05, 1.0));
    QCOMPARE(cleared.count(), 1);
}

void TestCanvasWidget::panningKeepsTheSelection() {
    CanvasWidget canvas;
    canvas.resize(640, 360);
    canvas.setDocument(pointsDocument());
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));
    canvas.setSelectedLayerPath(LayerPath{0});

    QSignalSpy cleared(&canvas, &CanvasWidget::selectionCleared);
    const QPoint start = plotPosition(canvas, 0.05, 1.0);
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(&canvas, start + QPoint(40, 15));
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, start + QPoint(40, 15));
    // A pan is a drag over the background, not a click on it.
    QCOMPARE(cleared.count(), 0);
}

void TestCanvasWidget::theAxisUnderThePointerScalesOnItsOwn() {
    CanvasWidget canvas;
    canvas.resize(640, 360);
    canvas.setDocument(pointsDocument());
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));

    // The canvas keeps its view range to itself. The automatic snap step follows
    // the drawn grid, so what it reports is a measure of each axis' zoom.
    QSignalSpy steps(&canvas, &CanvasWidget::snapStepChanged);
    canvas.setSnapSettings(SnapSettings{.enabled = true, .divisions = 1});

    double timeStep     = 0.0;
    double valueStep    = 0.0;
    const auto reported = [&] {
        while (! steps.isEmpty()) {
            const QList<QVariant> arguments = steps.takeFirst();
            timeStep                        = arguments.at(0).toDouble();
            valueStep                       = arguments.at(1).toDouble();
        }
    };
    reported();
    QVERIFY(timeStep > 0.0);
    QVERIFY(valueStep > 0.0);
    const double fittedTime  = timeStep;
    const double fittedValue = valueStep;

    // Six notches over the time scale below the plot shrink the visible span to a
    // quarter, so its grid has to get finer - and the value axis has to stay.
    turnWheel(canvas, timeScalePosition(canvas), 6);
    reported();
    QVERIFY(timeStep < fittedTime);
    QCOMPARE(valueStep, fittedValue);

    const double zoomedTime = timeStep;
    // The same over the value scale left of the plot, the other way round.
    turnWheel(canvas, valueScalePosition(canvas), 6);
    reported();
    QCOMPARE(timeStep, zoomedTime);
    QVERIFY(valueStep < fittedValue);

    // The corner where the two margins meet carries no scale and scales nothing.
    turnWheel(canvas, QPoint(10, canvas.height() - 10), 6);
    QVERIFY(steps.isEmpty());
}

void TestCanvasWidget::draggingAScaleShiftsOnlyItsAxis() {
    CanvasWidget canvas;
    canvas.resize(640, 360);
    canvas.setDocument(pointsDocument());
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));

    // The time under a fixed pixel is where a shifted time axis shows up.
    double readout = 0.0;
    connect(&canvas, &CanvasWidget::cursorReadout, this, [&readout](double time, double) {
        readout = time;
    });
    const QPoint probe     = plotPosition(canvas, 0.5, 0.5);
    const auto timeAtProbe = [&] {
        hover(canvas, probe);
        return readout;
    };
    const double fitted = timeAtProbe();
    QVERIFY(qAbs(fitted - 0.5) < 0.01);

    const QPoint timeScale = timeScalePosition(canvas);
    const auto drag        = [&canvas](const QPoint& from, const QPoint& delta) {
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(&canvas, from + delta, 20);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, from + delta);
    };

    // Dragging the time scale to the right takes the curve along, so the pixel
    // that showed t = 0.5 shows an earlier time afterwards.
    drag(timeScale, QPoint(60, 0));
    const double shifted  = timeAtProbe();
    const double expected = fitted - 60.0 * 1.12 / (canvas.width() - 78.0 - 14.0);
    QVERIFY(qAbs(shifted - expected) < 0.01);

    // Along the scale instead of across it nothing happens at all - not the time,
    // and nothing that is drawn either.
    const QImage shiftedImage = canvas.grab().toImage();
    drag(timeScale, QPoint(0, -40));
    QCOMPARE(timeAtProbe(), shifted);
    QCOMPARE(canvas.grab().toImage(), shiftedImage);

    // The value scale the other way round: it moves the curve vertically and
    // leaves the time axis exactly where the drag before it put it.
    drag(valueScalePosition(canvas), QPoint(0, 40));
    QVERIFY(canvas.grab().toImage() != shiftedImage);
    QCOMPARE(timeAtProbe(), shifted);
}

void TestCanvasWidget::doubleClickingAScaleFitsOnlyItsAxis() {
    CanvasWidget canvas;
    canvas.resize(640, 360);
    canvas.setDocument(pointsDocument());
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));

    QSignalSpy steps(&canvas, &CanvasWidget::snapStepChanged);
    canvas.setSnapSettings(SnapSettings{.enabled = true, .divisions = 1});

    double timeStep     = 0.0;
    double valueStep    = 0.0;
    const auto reported = [&] {
        while (! steps.isEmpty()) {
            const QList<QVariant> arguments = steps.takeFirst();
            timeStep                        = arguments.at(0).toDouble();
            valueStep                       = arguments.at(1).toDouble();
        }
    };
    reported();
    const double fittedTime  = timeStep;
    const double fittedValue = valueStep;

    const QPoint timeScale   = timeScalePosition(canvas);
    const QPoint valueScale  = valueScalePosition(canvas);
    turnWheel(canvas, timeScale, 6);
    turnWheel(canvas, valueScale, 6);
    reported();
    QVERIFY(timeStep < fittedTime);
    QVERIFY(valueStep < fittedValue);
    const double zoomedValue = valueStep;

    // The time axis is back on the whole document; the value axis keeps the zoom
    // it was left at.
    QTest::mouseDClick(&canvas, Qt::LeftButton, Qt::NoModifier, timeScale);
    reported();
    QCOMPARE(timeStep, fittedTime);
    QCOMPARE(valueStep, zoomedValue);

    // The value scale fits what is visible in time, which is the whole document
    // again - so the step the test started from comes back.
    QTest::mouseDClick(&canvas, Qt::LeftButton, Qt::NoModifier, valueScale);
    reported();
    QCOMPARE(valueStep, fittedValue);
    QCOMPARE(timeStep, fittedTime);
}

void TestCanvasWidget::hoveringAnAxisShowsTheScalingCursor() {
    CanvasWidget canvas;
    canvas.resize(640, 360);
    canvas.setDocument(pointsDocument());
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));

    hover(canvas, timeScalePosition(canvas));
    QCOMPARE(canvas.cursor().shape(), Qt::SizeHorCursor);

    hover(canvas, valueScalePosition(canvas));
    QCOMPARE(canvas.cursor().shape(), Qt::SizeVerCursor);

    hover(canvas, QPoint(10, canvas.height() - 10));
    QCOMPARE(canvas.cursor().shape(), Qt::ArrowCursor);
}

void TestCanvasWidget::theLegendCanBeSwitchedOff() {
    CanvasWidget canvas;
    canvas.resize(640, 360);
    canvas.setDocument(groupedDocument());
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));

    QVERIFY(canvas.legendVisible());
    const QImage withLegend = canvas.grab().toImage();
    canvas.setLegendVisible(false);
    QCoreApplication::processEvents();
    const QImage withoutLegend = canvas.grab().toImage();
    QVERIFY(withLegend != withoutLegend);

    canvas.setLegendVisible(true);
    QCoreApplication::processEvents();
    QCOMPARE(canvas.grab().toImage(), withLegend);
}

void TestCanvasWidget::eachValueIsHeldUntilTheNextOne() {
    CanvasWidget canvas;
    canvas.resize(640, 360);
    canvas.setLegendVisible(false);
    canvas.setDocument(lowRateDocument(false));
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));
    const QImage image = canvas.grab().toImage();

    // Value 4 (t = 0.4) is the last 0, value 5 (t = 0.5) the first 1. The output
    // holds the 0 until 0.5 and then jumps: nothing in between is ever output,
    // so no line may slope across it.
    QVERIFY(curveNear(image, plotPosition(canvas, 0.45, 0.0)));
    QVERIFY(! curveNear(image, plotPosition(canvas, 0.45, 0.5)));
    QVERIFY(curveNear(image, plotPosition(canvas, 0.5, 0.5)));
    QVERIFY(curveNear(image, plotPosition(canvas, 0.55, 1.0)));

    // The last value (t = 0.9) is held for its full period, up to the end of
    // the document, and the curve does not fall off to whatever the model says
    // beyond it.
    QVERIFY(curveNear(image, plotPosition(canvas, 0.97, 1.0)));
    QVERIFY(! curveNear(image, plotPosition(canvas, 0.97, 0.5)));
}

void TestCanvasWidget::theCursorReadsTheHeldValue() {
    CanvasWidget canvas;
    canvas.resize(640, 360);
    canvas.setDocument(lowRateDocument(true));
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));

    double time  = 0.0;
    double value = 0.0;
    connect(&canvas, &CanvasWidget::cursorReadout, this, [&](double t, double v) {
        time  = t;
        value = v;
    });

    // Between two sample times the output still holds the earlier value: 0.4
    // of the ramp, not the 0.45 the model would give there.
    hover(canvas, plotPosition(canvas, 0.45, 0.5));
    QVERIFY(time > 0.42 && time < 0.48);
    QVERIFY(qAbs(value - 0.4) < 1e-9);
}

QTEST_MAIN(TestCanvasWidget)
#include "tst_canvaswidget.moc"
