// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QPoint>
#include <QString>
#include <QWidget>
#include <cstddef>
#include <optional>
#include <vector>

class QEvent;
class QMouseEvent;
class QPaintEvent;
class QPainter;
class QWheelEvent;

namespace zwe {

// The plot below the waveform: the amplitude spectrum of the document's output.
// It only draws what it is given; computing it, and the settings that decide how,
// belong to MainWindow and the sidebar.
class SpectrumWidget final : public QWidget {
    Q_OBJECT

public:
    explicit SpectrumWidget(QWidget* parent = nullptr);

    // Bin k is at k * binWidth. The amplitude has no unit of its own: the output
    // is a voltage or a current, whichever the user runs it as. An empty amplitude clears the
    // plot. The view is fitted to the new spectrum when resetView is set or when
    // the plot was empty before; otherwise a live edit keeps the zoom.
    void setSpectrum(double binWidth, std::vector<double> amplitude, bool resetView = false);
    // Shown in the middle of the plot while there is no spectrum - why none could
    // be computed, or that one is being computed. Empty for none.
    void setMessage(const QString& message);

    // Logarithmic axes. Switching refits the affected axis. On a logarithmic
    // frequency axis the DC bin has no place and is left out; on a logarithmic
    // amplitude axis amplitudes below 1e-18, the pseudo zero, are clamped to it.
    void setLogFrequency(bool logarithmic);
    void setLogAmplitude(bool logarithmic);
    bool logFrequency() const { return logFrequency_; }
    bool logAmplitude() const { return logAmplitude_; }

    void fitToSpectrum();

signals:
    // A press anywhere on the widget. MainWindow shows the spectrum's settings in
    // the sidebar in response, the way a click on a curve shows its segment.
    void activated();
    // Frequency and amplitude under the pointer; a NaN frequency once the pointer
    // leaves the plot.
    void cursorReadout(double frequency, double amplitude);

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    // In axis coordinates: frequency and amplitude as they are on a linear axis,
    // their decimal logarithm on a logarithmic one. Zooming and panning are then
    // the same straight-line arithmetic on either kind of axis, and a logarithmic
    // bound can never reach zero or below, however far one pans.
    struct ViewRange {
        double xMin = 0.0;
        double xMax = 1.0;
        double yMin = 0.0;
        double yMax = 1.0;
    };

    // The labeled strips left of and below the plot, handles for their axis in
    // the same way as on the waveform canvas.
    enum class AxisRegion { None, Frequency, Amplitude };

    // One grid line of an axis, at position in axis coordinates. value is what
    // its label says; major lines are drawn at full grid strength, the others
    // fainter.
    struct Tick {
        double position;
        double value;
        bool major;
        bool labeled;
    };

    QRect plotRect() const;
    AxisRegion axisAtPosition(const QPointF& position) const;
    // The halves of fitToSpectrum(). The amplitude axis is fitted to the bins in
    // the visible frequency range, so the order of the two matters.
    void fitFrequencyAxis();
    void fitAmplitudeAxis();
    void zoomView(bool frequency, bool amplitude, double factor, const QPointF& position);
    // Keeps a logarithmic view inside what 10^x can represent.
    void limitView();

    // The first bin that has a place on the frequency axis: the DC bin does not
    // on a logarithmic one.
    size_t firstBin() const { return logFrequency_ ? 1 : 0; }
    double frequencyToAxis(double frequency) const;
    double axisToFrequency(double position) const;
    double amplitudeToAxis(double amplitude) const;
    double xToPixel(double position, const QRect& area) const;
    double yToPixel(double position, const QRect& area) const;
    double pixelToX(double pixel, const QRect& area) const;
    double pixelToY(double pixel, const QRect& area) const;
    double binPixel(size_t bin, const QRect& area) const;
    double amplitudePixel(double amplitude, const QRect& area) const;
    // The first bin at or right of the given pixel edge, clamped to the bins the
    // axis shows. Pixel column c holds the bins [boundary(c), boundary(c + 1)),
    // so consecutive columns partition the spectrum without a gap or an overlap.
    size_t columnBoundary(double pixel, const QRect& area) const;
    // The bin the pointer at pixel x reads: the largest one in that pixel column,
    // or the nearest one when the column holds none.
    std::optional<size_t> binAtPixel(double x, const QRect& area) const;

    std::vector<Tick> axisTicks(
        double minimum,
        double maximum,
        bool logarithmic,
        double pixels,
        int linearTicks,
        double labelSpacing
    ) const;
    void drawGrid(QPainter& painter, const QRect& area, const QColor& textColor) const;
    void drawSpectrum(QPainter& painter, const QRect& area, const QColor& color) const;
    void updateHoverCursor(const QPointF& position);
    void updateCursorReadout(const QPointF& position);

    bool logFrequency_ = false;
    bool logAmplitude_ = false;
    double binWidth_   = 0.0;
    std::vector<double> amplitude_;
    QString message_;
    ViewRange view_;
    QPoint panStart_;
    ViewRange panStartView_;
    // The axis the current pan is locked to; None pans both, which is what a drag
    // over the plot itself does.
    AxisRegion panAxis_ = AxisRegion::None;
    bool panning_       = false;
};

}  // namespace zwe
