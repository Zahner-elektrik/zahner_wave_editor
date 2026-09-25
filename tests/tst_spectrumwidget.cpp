// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include <QtTest>
#include <cmath>
#include <limits>
#include <vector>

#include "ui/plotscale.h"
#include "ui/spectrumwidget.h"

using namespace zwe;

namespace {

// The widget's margins, treated as black-box padding the way tst_canvaswidget
// treats the canvas' - the two plots share them.
constexpr int LeftMargin   = 78;
constexpr int RightMargin  = 14;
constexpr int TopMargin    = 16;
constexpr int BottomMargin = 38;

QRect plotArea(const QWidget& widget) {
    return widget.rect().adjusted(LeftMargin, TopMargin, -RightMargin, -BottomMargin);
}

// A few bins, far enough apart to be drawn several pixels from each other: 101
// bins of 10 Hz, a 0.5 V line at 370 Hz over a floor of 10 mV.
constexpr double SparseBinWidth = 10.0;
constexpr size_t SparseBins     = 101;
constexpr size_t SparsePeak     = 37;

std::vector<double> sparseSpectrum(double floor = 0.01) {
    std::vector<double> amplitude(SparseBins, floor);
    amplitude[SparsePeak] = 0.5;
    return amplitude;
}

// Two million 1 Hz bins with a single 1 V line: on a plot a few hundred pixels
// wide, thousands of bins share every pixel column, and all but one of them are
// down at the 1 mV floor.
constexpr size_t DenseBins = 2'000'001;
constexpr size_t DensePeak = 1'234'567;

std::vector<double> denseSpectrum() {
    std::vector<double> amplitude(DenseBins, 1e-3);
    amplitude[DensePeak] = 1.0;
    return amplitude;
}

// Where frequency lands on the fitted frequency axis of a spectrum whose last
// bin is at last: from 0 Hz on a linear axis, from the first bin after DC on a
// logarithmic one.
double fittedPixel(const SpectrumWidget& widget, double frequency, double binWidth, double last) {
    const QRect area = plotArea(widget);
    const double t   = widget.logFrequency() ? (std::log10(frequency) - std::log10(binWidth)) /
                                                   (std::log10(last) - std::log10(binWidth))
                                             : frequency / last;
    return area.left() + t * area.width();
}

// Whether any pixel in the columns from x - 2 to x + 2, in the upper half of the
// plot, is drawn in the curve's color. The curve is a saturated color, the grid,
// the frame and the labels are not. The upper half keeps the floor out: it is a
// thousand times smaller than the line in every spectrum used here, which puts it
// at the bottom of the plot on either amplitude axis.
bool curveInUpperHalf(const QImage& image, const QRect& area, int x) {
    for (int column = x - 2; column <= x + 2; ++column) {
        for (int y = area.top() + 2; y < area.center().y(); ++y) {
            if (image.pixelColor(column, y).hsvSaturation() > 60) {
                return true;
            }
        }
    }
    return false;
}

// Moves the pointer with no button held, as a synthetic event: QTest::mouseMove
// warps the physical cursor then, which only reaches an uncovered widget.
void hover(QWidget& widget, const QPoint& position) {
    QMouseEvent move(
        QEvent::MouseMove,
        QPointF(position),
        widget.mapToGlobal(QPointF(position)),
        Qt::NoButton,
        Qt::NoButton,
        Qt::NoModifier
    );
    QCoreApplication::sendEvent(&widget, &move);
}

// Turns the wheel over the given position, one notch of 120 units at a time; a
// positive count zooms in.
void turnWheel(QWidget& widget, const QPoint& position, int notches) {
    for (int step = 0; step < qAbs(notches); ++step) {
        QWheelEvent event(
            QPointF(position),
            widget.mapToGlobal(QPointF(position)),
            QPoint(),
            QPoint(0, notches > 0 ? 120 : -120),
            Qt::NoButton,
            Qt::NoModifier,
            Qt::NoScrollPhase,
            false
        );
        QCoreApplication::sendEvent(&widget, &event);
    }
}

// A point on the frequency scale below the plot, and one on the amplitude scale
// left of it.
QPoint frequencyScalePosition(const QWidget& widget) {
    return {widget.width() / 2, widget.height() - 10};
}

QPoint amplitudeScalePosition(const QWidget& widget) {
    return {10, widget.height() / 2};
}

// The parts of the margins that only one axis draws into: the amplitude labels
// left of the plot, above the row the frequency labels start in, and the
// frequency labels below the plot, right of where the amplitude labels end.
QImage amplitudeScale(const QImage& image, const QRect& area) {
    return image.copy(0, 0, LeftMargin - 1, area.bottom() + 4);
}

QImage frequencyScale(const QImage& image, const QRect& area) {
    return image.copy(
        LeftMargin,
        area.bottom() + 3,
        image.width() - LeftMargin,
        image.height() - area.bottom() - 3
    );
}

// Records the last cursor readout.
struct Readout {
    double frequency = 0.0;
    double amplitude = 0.0;

    void connectTo(SpectrumWidget& widget) {
        QObject::connect(
            &widget, &SpectrumWidget::cursorReadout, &widget, [this](double f, double a) {
                frequency = f;
                amplitude = a;
            }
        );
    }
};

}  // namespace

class TestSpectrumWidget : public QObject {
    Q_OBJECT

private slots:
    void aPressActivatesTheSpectrum();
    void aSinglePeakIsDrawnOnEveryAxis();
    void aPeakSurvivesThousandsOfBinsPerColumn();
    void theCursorReadsTheBinUnderThePointer();
    void theCursorReadsTheLargestBinOfAColumn();
    void theFrequencyScaleZoomsOnlyFrequency();
    void doubleClickingTheFrequencyScaleFitsIt();
    void hoveringAnAxisShowsTheScalingCursor();
    void aLogarithmicFrequencyAxisLeavesOutDc();
    void panningALogarithmicAxisStaysPositive();
    void anEmptySpectrumShowsTheMessage();
    void aLiveUpdateKeepsTheZoom();
    void logTicksCoverDecadesAndTheirMultiples();
    void zzSnapshots();
};

void TestSpectrumWidget::aPressActivatesTheSpectrum() {
    SpectrumWidget widget;
    widget.resize(640, 240);
    widget.setSpectrum(SparseBinWidth, sparseSpectrum());
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    QSignalSpy activated(&widget, &SpectrumWidget::activated);
    // Anywhere, with either button: over the plot, on a scale, and in the corner
    // that carries none.
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, plotArea(widget).center());
    QCOMPARE(activated.count(), 1);
    QTest::mouseClick(&widget, Qt::RightButton, Qt::NoModifier, plotArea(widget).center());
    QCOMPARE(activated.count(), 2);
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, frequencyScalePosition(widget));
    QCOMPARE(activated.count(), 3);
    QTest::mouseClick(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(5, widget.height() - 5));
    QCOMPARE(activated.count(), 4);
}

void TestSpectrumWidget::aSinglePeakIsDrawnOnEveryAxis() {
    SpectrumWidget widget;
    widget.resize(640, 240);
    widget.setSpectrum(SparseBinWidth, sparseSpectrum());
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    const QRect area  = plotArea(widget);
    const double last = (SparseBins - 1) * SparseBinWidth;
    const double peak = SparsePeak * SparseBinWidth;
    for (const bool logFrequency : {false, true}) {
        for (const bool logAmplitude : {false, true}) {
            widget.setLogFrequency(logFrequency);
            widget.setLogAmplitude(logAmplitude);
            QCoreApplication::processEvents();
            const QImage image = widget.grab().toImage();
            const int x        = qRound(fittedPixel(widget, peak, SparseBinWidth, last));
            QVERIFY2(
                curveInUpperHalf(image, area, x),
                qPrintable(QStringLiteral("log frequency %1, log amplitude %2")
                               .arg(logFrequency)
                               .arg(logAmplitude))
            );
            // Away from the line only the floor is drawn, far below.
            const int away = qRound(fittedPixel(widget, 80 * SparseBinWidth, SparseBinWidth, last));
            QVERIFY(! curveInUpperHalf(image, area, away));
        }
    }
}

void TestSpectrumWidget::aPeakSurvivesThousandsOfBinsPerColumn() {
    SpectrumWidget widget;
    widget.resize(640, 240);
    widget.setSpectrum(1.0, denseSpectrum());
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    const QRect area  = plotArea(widget);
    const double last = static_cast<double>(DenseBins - 1);
    for (const bool logFrequency : {false, true}) {
        for (const bool logAmplitude : {false, true}) {
            widget.setLogFrequency(logFrequency);
            widget.setLogAmplitude(logAmplitude);
            QCoreApplication::processEvents();
            const QImage image = widget.grab().toImage();
            // Thousands of bins per column in either case: on the logarithmic
            // axis the line sits near the top end, where a pixel spans some
            // 30 kHz.
            const int x = qRound(fittedPixel(widget, static_cast<double>(DensePeak), 1.0, last));
            QVERIFY2(
                curveInUpperHalf(image, area, x),
                qPrintable(QStringLiteral("log frequency %1, log amplitude %2")
                               .arg(logFrequency)
                               .arg(logAmplitude))
            );
            QVERIFY(! curveInUpperHalf(image, area, area.left() + 150));
        }
    }
}

void TestSpectrumWidget::theCursorReadsTheBinUnderThePointer() {
    SpectrumWidget widget;
    widget.resize(640, 240);
    widget.setSpectrum(SparseBinWidth, sparseSpectrum());
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    Readout readout;
    readout.connectTo(widget);
    const double last = (SparseBins - 1) * SparseBinWidth;
    const int x = qRound(fittedPixel(widget, SparsePeak * SparseBinWidth, SparseBinWidth, last));
    // The bins are more than five pixels apart, so a pointer a pixel off still
    // reads the line: the nearest bin, at its own frequency.
    for (const int offset : {-1, 0, 1}) {
        hover(widget, QPoint(x + offset, plotArea(widget).center().y()));
        QCOMPARE(readout.frequency, SparsePeak * SparseBinWidth);
        QCOMPARE(readout.amplitude, 0.5);
    }

    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(&widget, &leave);
    QVERIFY(std::isnan(readout.frequency));

    // The margins are outside the plot as well.
    hover(widget, QPoint(x, 5));
    QVERIFY(std::isnan(readout.frequency));
}

void TestSpectrumWidget::theCursorReadsTheLargestBinOfAColumn() {
    SpectrumWidget widget;
    widget.resize(640, 240);
    widget.setSpectrum(1.0, denseSpectrum());
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    Readout readout;
    readout.connectTo(widget);
    const double last = static_cast<double>(DenseBins - 1);
    for (const bool logFrequency : {false, true}) {
        widget.setLogFrequency(logFrequency);
        const int x = qRound(fittedPixel(widget, static_cast<double>(DensePeak), 1.0, last));
        // Rounding may put the line into the column next to the computed one;
        // one of the three must read it.
        bool found = false;
        for (const int offset : {-1, 0, 1}) {
            hover(widget, QPoint(x + offset, plotArea(widget).center().y()));
            found = found || (readout.frequency == static_cast<double>(DensePeak) &&
                              readout.amplitude == 1.0);
        }
        QVERIFY(found);
        // Anywhere else a column holds nothing but the floor.
        hover(widget, QPoint(plotArea(widget).left() + 150, plotArea(widget).center().y()));
        QCOMPARE(readout.amplitude, 1e-3);
    }
}

void TestSpectrumWidget::theFrequencyScaleZoomsOnlyFrequency() {
    SpectrumWidget widget;
    widget.resize(640, 240);
    widget.setSpectrum(SparseBinWidth, sparseSpectrum());
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    Readout readout;
    readout.connectTo(widget);
    const QRect area   = plotArea(widget);
    const QPoint probe = QPoint(area.left() + 60, area.center().y());
    hover(widget, probe);
    const double fitted = readout.frequency;

    const QImage before = widget.grab().toImage();
    turnWheel(widget, frequencyScalePosition(widget), 6);
    const QImage zoomed = widget.grab().toImage();
    QVERIFY(frequencyScale(zoomed, area) != frequencyScale(before, area));
    QCOMPARE(amplitudeScale(zoomed, area), amplitudeScale(before, area));
    hover(widget, probe);
    QVERIFY(readout.frequency != fitted);
    const double zoomedFrequency = readout.frequency;

    // The amplitude scale the other way round.
    turnWheel(widget, amplitudeScalePosition(widget), 6);
    const QImage both = widget.grab().toImage();
    QVERIFY(amplitudeScale(both, area) != amplitudeScale(zoomed, area));
    QCOMPARE(frequencyScale(both, area), frequencyScale(zoomed, area));
    hover(widget, probe);
    QCOMPARE(readout.frequency, zoomedFrequency);

    // Over the plot the wheel scales frequency, not amplitude.
    turnWheel(widget, area.center(), 3);
    const QImage again = widget.grab().toImage();
    QVERIFY(frequencyScale(again, area) != frequencyScale(both, area));
    QCOMPARE(amplitudeScale(again, area), amplitudeScale(both, area));

    // The corner where the two margins meet carries no scale and scales nothing.
    turnWheel(widget, QPoint(10, widget.height() - 10), 6);
    QCOMPARE(widget.grab().toImage(), again);
}

void TestSpectrumWidget::doubleClickingTheFrequencyScaleFitsIt() {
    SpectrumWidget widget;
    widget.resize(640, 240);
    widget.setSpectrum(SparseBinWidth, sparseSpectrum());
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    const QRect area    = plotArea(widget);
    const QImage fitted = widget.grab().toImage();
    turnWheel(widget, frequencyScalePosition(widget), 6);
    turnWheel(widget, amplitudeScalePosition(widget), 6);
    const QImage zoomed = widget.grab().toImage();

    QTest::mouseDClick(&widget, Qt::LeftButton, Qt::NoModifier, frequencyScalePosition(widget));
    const QImage frequencyFitted = widget.grab().toImage();
    QCOMPARE(frequencyScale(frequencyFitted, area), frequencyScale(fitted, area));
    QCOMPARE(amplitudeScale(frequencyFitted, area), amplitudeScale(zoomed, area));

    // Over the plot both come back.
    turnWheel(widget, frequencyScalePosition(widget), 6);
    QTest::mouseDClick(&widget, Qt::LeftButton, Qt::NoModifier, area.center());
    QCOMPARE(widget.grab().toImage(), fitted);
}

void TestSpectrumWidget::hoveringAnAxisShowsTheScalingCursor() {
    SpectrumWidget widget;
    widget.resize(640, 240);
    widget.setSpectrum(SparseBinWidth, sparseSpectrum());
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    hover(widget, frequencyScalePosition(widget));
    QCOMPARE(widget.cursor().shape(), Qt::SizeHorCursor);
    hover(widget, amplitudeScalePosition(widget));
    QCOMPARE(widget.cursor().shape(), Qt::SizeVerCursor);
    hover(widget, plotArea(widget).center());
    QCOMPARE(widget.cursor().shape(), Qt::ArrowCursor);
}

void TestSpectrumWidget::aLogarithmicFrequencyAxisLeavesOutDc() {
    SpectrumWidget widget;
    widget.resize(640, 240);
    // A DC bin ten times the line: on a linear axis it is the largest bin.
    std::vector<double> amplitude = sparseSpectrum();
    amplitude[0]                  = 5.0;
    widget.setSpectrum(SparseBinWidth, amplitude);
    widget.setLogFrequency(true);
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));
    QVERIFY(! widget.grab().isNull());

    Readout readout;
    readout.connectTo(widget);
    const QRect area = plotArea(widget);
    for (int x = area.left(); x <= area.right(); x += 3) {
        hover(widget, QPoint(x, area.center().y()));
        QVERIFY(readout.frequency >= SparseBinWidth);
    }
    hover(widget, QPoint(area.left(), area.center().y()));
    QCOMPARE(readout.frequency, SparseBinWidth);

    // Nothing but DC, and DC plus a single bin: nothing or a single frequency to
    // show, which must neither crash nor read a bin that is not there.
    widget.setSpectrum(SparseBinWidth, {1.0}, true);
    QVERIFY(! widget.grab().isNull());
    hover(widget, area.center());
    QVERIFY(std::isnan(readout.frequency));
    widget.setSpectrum(SparseBinWidth, {1.0, 0.5}, true);
    QVERIFY(! widget.grab().isNull());
    hover(widget, area.center());
    QCOMPARE(readout.frequency, SparseBinWidth);
    widget.setLogAmplitude(true);
    QVERIFY(! widget.grab().isNull());

    // A spectrum of zeros on logarithmic axes sits on its floor.
    widget.setSpectrum(SparseBinWidth, std::vector<double>(SparseBins, 0.0), true);
    QVERIFY(! widget.grab().isNull());
}

void TestSpectrumWidget::panningALogarithmicAxisStaysPositive() {
    SpectrumWidget widget;
    widget.resize(640, 240);
    widget.setSpectrum(SparseBinWidth, sparseSpectrum());
    widget.setLogFrequency(true);
    widget.setLogAmplitude(true);
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    // Dragging the plot to the right again and again moves the view towards ever
    // lower frequencies, which on a logarithmic axis approach zero without ever
    // getting there.
    const QPoint start = plotArea(widget).center();
    for (int drag = 0; drag < 20; ++drag) {
        QTest::mousePress(&widget, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(&widget, start + QPoint(250, 80), 5);
        QTest::mouseRelease(&widget, Qt::LeftButton, Qt::NoModifier, start + QPoint(250, 80));
    }
    QVERIFY(! widget.grab().isNull());
    // Zooming out as far as the wheel goes is just as harmless.
    turnWheel(widget, start, -400);
    QVERIFY(! widget.grab().isNull());

    Readout readout;
    readout.connectTo(widget);
    hover(widget, start);
    QVERIFY(readout.frequency >= SparseBinWidth);

    widget.fitToSpectrum();
    hover(widget, QPoint(plotArea(widget).left(), start.y()));
    QCOMPARE(readout.frequency, SparseBinWidth);
}

void TestSpectrumWidget::anEmptySpectrumShowsTheMessage() {
    SpectrumWidget widget;
    widget.resize(640, 240);
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    const QImage plain = widget.grab().toImage();
    widget.setMessage(QStringLiteral("Computing the spectrum..."));
    QCoreApplication::processEvents();
    const QImage withMessage = widget.grab().toImage();
    QVERIFY(withMessage != plain);

    Readout readout;
    readout.connectTo(widget);
    hover(widget, plotArea(widget).center());
    QVERIFY(std::isnan(readout.frequency));

    // A spectrum replaces the message, and clearing it brings the message back.
    widget.setSpectrum(SparseBinWidth, sparseSpectrum());
    QVERIFY(widget.grab().toImage() != withMessage);
    widget.setSpectrum(SparseBinWidth, {});
    QCOMPARE(widget.grab().toImage(), withMessage);
    // So does a spectrum that has no frequency axis.
    widget.setSpectrum(0.0, sparseSpectrum());
    QCOMPARE(widget.grab().toImage(), withMessage);
}

void TestSpectrumWidget::aLiveUpdateKeepsTheZoom() {
    SpectrumWidget widget;
    widget.resize(640, 240);
    widget.setSpectrum(SparseBinWidth, sparseSpectrum());
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    Readout readout;
    readout.connectTo(widget);
    const QRect area   = plotArea(widget);
    const QPoint probe = QPoint(area.left() + 60, area.center().y());
    hover(widget, probe);
    const double fitted = readout.frequency;

    turnWheel(widget, area.center(), 6);
    hover(widget, probe);
    const double zoomed = readout.frequency;
    QVERIFY(zoomed != fitted);

    // A live edit: same bins, new amplitudes. The zoom stays.
    widget.setSpectrum(SparseBinWidth, sparseSpectrum(0.02));
    hover(widget, probe);
    QCOMPARE(readout.frequency, zoomed);
    QCOMPARE(readout.amplitude, 0.02);

    // A new document resets it.
    widget.setSpectrum(SparseBinWidth, sparseSpectrum(), true);
    hover(widget, probe);
    QCOMPARE(readout.frequency, fitted);

    // And so does a spectrum after an empty plot, where there is no zoom to keep.
    turnWheel(widget, area.center(), 6);
    widget.setSpectrum(SparseBinWidth, {});
    widget.setSpectrum(SparseBinWidth, sparseSpectrum());
    hover(widget, probe);
    QCOMPARE(readout.frequency, fitted);
}

void TestSpectrumWidget::logTicksCoverDecadesAndTheirMultiples() {
    // The audio range: 20 Hz to 20 kHz, both ends ticks themselves.
    const std::vector<plotscale::LogTick> audio = plotscale::logTicks(20.0, 20000.0);
    QCOMPARE(audio.size(), size_t{28});
    QCOMPARE(audio.front().value, 20.0);
    QCOMPARE(audio.front().multiple, 2);
    QVERIFY(qFuzzyCompare(audio.back().value, 20000.0));
    QCOMPARE(audio.back().multiple, 2);
    const auto decades = std::count_if(audio.begin(), audio.end(), [](const auto& tick) {
        return tick.multiple == 1;
    });
    QCOMPARE(decades, std::ptrdiff_t{3});
    QVERIFY(std::is_sorted(audio.begin(), audio.end(), [](const auto& a, const auto& b) {
        return a.value < b.value;
    }));

    // Decades only, and bounds that came out of 10^x with a rounding error.
    const std::vector<plotscale::LogTick> coarse =
        plotscale::logTicks(std::pow(10.0, 1e-12), std::pow(10.0, 3.0 - 1e-12), false);
    QCOMPARE(coarse.size(), size_t{4});
    QCOMPARE(coarse.front().value, 1.0);
    QCOMPARE(coarse.back().value, 1000.0);

    QVERIFY(plotscale::logTicks(0.0, 10.0).empty());
    QVERIFY(plotscale::logTicks(10.0, 1.0).empty());
    QVERIFY(plotscale::logTicks(1.0, std::numeric_limits<double>::infinity()).empty());
}


#include "ui/theme.h"
void TestSpectrumWidget::zzSnapshots() {
    const QString dir = QStringLiteral("C:/Users/max/AppData/Local/Temp/claude/c--Programmieren-zahner-wave-editor/5540144c-d0a4-43ef-842c-4ac24f36aaf5/scratchpad/");
    // square wave 100 Hz fundamental, bin 1 Hz, 500k bins, odd harmonics 1/k, small floor with jitter
    std::vector<double> a(500001);
    for (size_t k = 0; k < a.size(); ++k) a[k] = 1e-9 * (1.0 + 0.5 * std::sin(k * 0.37));
    for (size_t h = 1; h * 100 < a.size(); h += 2) a[h * 100] = 4.0 / (3.14159 * h);
    a[0] = 0.3;
    for (const bool dark : {false, true}) {
        SpectrumWidget w;
        w.setPalette(dark ? theme::darkPalette() : theme::lightPalette());
        w.resize(900, 260);
        w.setSpectrum(1.0, a);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        for (int mode = 0; mode < 4; ++mode) {
            w.setLogFrequency(mode & 1);
            w.setLogAmplitude(mode & 2);
            QCoreApplication::processEvents();
            w.grab().save(dir + QStringLiteral("snap_%1_%2.png").arg(dark).arg(mode));
        }
        // zoomed in linear around 500 Hz
        w.setLogFrequency(false); w.setLogAmplitude(true);
        w.fitToSpectrum();
        const QRect area = plotArea(w);
        turnWheel(w, QPoint(area.left() + 1, area.center().y()), 40);
        w.grab().save(dir + QStringLiteral("snap_%1_zoom.png").arg(dark));
        turnWheel(w, QPoint(area.left() + 1, area.center().y()), 10);
        w.grab().save(dir + QStringLiteral("snap_%1_zoom2.png").arg(dark));
    }
    SpectrumWidget e; e.resize(600, 180); e.setMessage(QStringLiteral("Computing the spectrum...")); e.show();
    QVERIFY(QTest::qWaitForWindowExposed(&e));
    e.grab().save(dir + QStringLiteral("snap_empty.png"));
}

QTEST_MAIN(TestSpectrumWidget)
#include "tst_spectrumwidget.moc"
