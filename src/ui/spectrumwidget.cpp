// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "spectrumwidget.h"

#include <QEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>

#include "plotscale.h"

namespace zwe {

namespace {

// The margins of the waveform canvas, so the two plots line up one above the
// other and their scales can be read against each other.
constexpr int LeftMargin        = 78;
constexpr int RightMargin       = 14;
constexpr int TopMargin         = 16;
constexpr int BottomMargin      = 38;
constexpr double MarginFraction = 0.06;

// What a logarithmic amplitude axis clamps to: 1e-18, the pseudo zero of the
// instruments. A bin that is exactly zero has no logarithm and sits there instead.
constexpr double AmplitudeFloor = 1e-18;

// The largest decimal exponent a logarithmic view may reach, with room to spare
// before 10^x overflows or underflows a double.
constexpr double LogarithmLimit = 300.0;

// Grid lines closer together than this read as a solid fill, the same limit the
// canvas uses for its snap grid. It decides whether the decades that carry no
// label still get a line of their own.
constexpr double MinimumFineGridSpacing = 4.0;
// The 2..9 lines of a decade crowd towards its top end: 9 to 10 is less than a
// twentieth of the decade. From this many pixels per decade on, that gap is two
// pixels or more, which is where the lines start to say something.
constexpr double MinimumSubDecadeSpacing = 40.0;
// The least distance between the centers of two labels, horizontally on the
// frequency axis and vertically on the amplitude axis.
constexpr double FrequencyLabelSpacing = 70.0;
constexpr double AmplitudeLabelSpacing = 24.0;

// The part of the line from a to b (a left of b) that lies between left and
// right; a null line when none does. The bins just outside the view can be
// thousands of pixels away when zoomed in far, and the painter is given only the
// stretch that can be seen.
QLineF clipHorizontally(QPointF a, QPointF b, double left, double right) {
    if (b.x() < left || a.x() > right) {
        return {};
    }
    const auto at = [&](double x) {
        const double t = (x - a.x()) / (b.x() - a.x());
        return QPointF(x, a.y() + t * (b.y() - a.y()));
    };
    if (a.x() < left) {
        a = at(left);
    }
    if (b.x() > right) {
        b = at(right);
    }
    return {a, b};
}

// Floor modulo, so that decade 10^-3 falls into the same label stride as 10^3.
int floorModulo(int value, int divisor) {
    const int remainder = value % divisor;
    return remainder < 0 ? remainder + divisor : remainder;
}

}  // namespace

SpectrumWidget::SpectrumWidget(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    // No keyboard handling of its own, but a click moves the focus here, away
    // from whatever editor in the sidebar had it.
    setFocusPolicy(Qt::ClickFocus);
    // Smaller than the canvas: it shares a splitter with it and is the lesser of
    // the two.
    setMinimumSize(360, 140);
}

void SpectrumWidget::setSpectrum(double binWidth, std::vector<double> amplitude, bool resetView) {
    const bool wasEmpty = amplitude_.empty();
    // A spectrum without a usable bin width has no frequency axis to be drawn on.
    if (! (binWidth > 0.0) || ! std::isfinite(binWidth)) {
        amplitude.clear();
    }
    binWidth_  = amplitude.empty() ? 0.0 : binWidth;
    amplitude_ = std::move(amplitude);
    // The scales of a spectrum that is gone would only label an empty plot with
    // frequencies that mean nothing any more.
    if (amplitude_.empty()) {
        view_ = {};
    }
    // An empty plot has no zoom worth keeping, so the first spectrum after one
    // is always shown in full.
    if (! amplitude_.empty() && (resetView || wasEmpty)) {
        fitFrequencyAxis();
        fitAmplitudeAxis();
    }
    update();
}

void SpectrumWidget::setMessage(const QString& message) {
    if (message_ == message) {
        return;
    }
    message_ = message;
    update();
}

void SpectrumWidget::setLogFrequency(bool logarithmic) {
    if (logFrequency_ == logarithmic) {
        return;
    }
    logFrequency_ = logarithmic;
    fitFrequencyAxis();
    update();
}

void SpectrumWidget::setLogAmplitude(bool logarithmic) {
    if (logAmplitude_ == logarithmic) {
        return;
    }
    logAmplitude_ = logarithmic;
    fitAmplitudeAxis();
    update();
}

void SpectrumWidget::fitToSpectrum() {
    fitFrequencyAxis();
    fitAmplitudeAxis();
    update();
}

void SpectrumWidget::fitFrequencyAxis() {
    view_.xMin         = ViewRange{}.xMin;
    view_.xMax         = ViewRange{}.xMax;
    const size_t count = amplitude_.size();
    if (count == 0 || ! (binWidth_ > 0.0)) {
        return;
    }
    const double last = static_cast<double>(count - 1) * binWidth_;
    if (! logFrequency_) {
        // A spectrum starts at DC, and so does its axis: a margin left of 0 Hz
        // would only show frequencies that do not exist. Neither end gets a
        // margin - the bins on the frame lines are drawn all the same.
        view_.xMax = count > 1 ? last : binWidth_;
        return;
    }
    // From the first bin after DC to the last one. Up to two bins that is a
    // single frequency at most, which gets a decade around it.
    if (count <= 2) {
        const double center = std::log10(binWidth_);
        view_.xMin          = center - 0.5;
        view_.xMax          = center + 0.5;
    } else {
        view_.xMin = std::log10(binWidth_);
        view_.xMax = std::log10(last);
    }
    limitView();
}

void SpectrumWidget::fitAmplitudeAxis() {
    view_.yMin = ViewRange{}.yMin;
    view_.yMax = ViewRange{}.yMax;
    if (amplitude_.size() <= firstBin() || ! (binWidth_ > 0.0)) {
        return;
    }

    // Like the canvas' value axis, this fits what is on screen: the bins inside
    // the visible frequency range. The tolerance keeps the bins on the frame
    // lines, which the fitted frequency axis puts there.
    constexpr double Tolerance = 1e-9;
    const double lastBin       = static_cast<double>(amplitude_.size() - 1);
    const double lower         = std::max(
        std::ceil(axisToFrequency(view_.xMin) / binWidth_ - Tolerance),
        static_cast<double>(firstBin())
    );
    const double upper =
        std::min(std::floor(axisToFrequency(view_.xMax) / binWidth_ + Tolerance), lastBin);
    if (! (lower <= upper)) {
        return;
    }
    const auto from               = amplitude_.begin() + static_cast<std::ptrdiff_t>(lower);
    const auto to                 = amplitude_.begin() + static_cast<std::ptrdiff_t>(upper) + 1;
    const auto [minimum, maximum] = std::minmax_element(from, to);

    if (! logAmplitude_) {
        // Amplitudes are magnitudes, so zero is where the axis starts: a line
        // is then read against nothing rather than against the smallest bin.
        view_.yMin = 0.0;
        view_.yMax = *maximum > 0.0 ? *maximum * (1.0 + MarginFraction) : 1.0;
        return;
    }
    double low  = amplitudeToAxis(*minimum);
    double high = amplitudeToAxis(*maximum);
    if (high - low < 1e-6) {
        low -= 0.5;
        high += 0.5;
    }
    const double margin = (high - low) * MarginFraction;
    view_.yMin          = low - margin;
    view_.yMax          = high + margin;
    limitView();
}

void SpectrumWidget::limitView() {
    // Shifted rather than clamped, so that a pan against the limit stops there
    // instead of squeezing the view.
    const auto limit = [](double& lower, double& upper) {
        if (upper - lower > 2.0 * LogarithmLimit) {
            lower = -LogarithmLimit;
            upper = LogarithmLimit;
            return;
        }
        if (lower < -LogarithmLimit) {
            upper += -LogarithmLimit - lower;
            lower = -LogarithmLimit;
        }
        if (upper > LogarithmLimit) {
            lower -= upper - LogarithmLimit;
            upper = LogarithmLimit;
        }
    };
    if (logFrequency_) {
        limit(view_.xMin, view_.xMax);
    }
    if (logAmplitude_) {
        limit(view_.yMin, view_.yMax);
    }
}

QRect SpectrumWidget::plotRect() const {
    return rect().adjusted(LeftMargin, TopMargin, -RightMargin, -BottomMargin);
}

SpectrumWidget::AxisRegion SpectrumWidget::axisAtPosition(const QPointF& position) const {
    const QRect area   = plotRect();
    const QPoint point = position.toPoint();
    if (area.contains(point)) {
        return AxisRegion::None;
    }
    // As on the canvas, only the strip alongside the plot counts; the corner
    // where the two margins meet carries no scale.
    if (point.x() < area.left() && point.y() >= area.top() && point.y() <= area.bottom()) {
        return AxisRegion::Amplitude;
    }
    if (point.y() > area.bottom() && point.x() >= area.left() && point.x() <= area.right()) {
        return AxisRegion::Frequency;
    }
    return AxisRegion::None;
}

double SpectrumWidget::frequencyToAxis(double frequency) const {
    return logFrequency_ ? std::log10(frequency) : frequency;
}

double SpectrumWidget::axisToFrequency(double position) const {
    return logFrequency_ ? std::pow(10.0, position) : position;
}

double SpectrumWidget::amplitudeToAxis(double amplitude) const {
    return logAmplitude_ ? std::log10(std::max(amplitude, AmplitudeFloor)) : amplitude;
}

double SpectrumWidget::xToPixel(double position, const QRect& area) const {
    return area.left() + (position - view_.xMin) / (view_.xMax - view_.xMin) * area.width();
}

double SpectrumWidget::yToPixel(double position, const QRect& area) const {
    return area.bottom() - (position - view_.yMin) / (view_.yMax - view_.yMin) * area.height();
}

double SpectrumWidget::pixelToX(double pixel, const QRect& area) const {
    return view_.xMin + (pixel - area.left()) / area.width() * (view_.xMax - view_.xMin);
}

double SpectrumWidget::pixelToY(double pixel, const QRect& area) const {
    return view_.yMin + (area.bottom() - pixel) / area.height() * (view_.yMax - view_.yMin);
}

double SpectrumWidget::binPixel(size_t bin, const QRect& area) const {
    return xToPixel(frequencyToAxis(static_cast<double>(bin) * binWidth_), area);
}

double SpectrumWidget::amplitudePixel(double amplitude, const QRect& area) const {
    return yToPixel(amplitudeToAxis(amplitude), area);
}

size_t SpectrumWidget::columnBoundary(double pixel, const QRect& area) const {
    const double index = std::ceil(axisToFrequency(pixelToX(pixel, area)) / binWidth_);
    // Written so that a NaN lands on the first bin instead of in a cast.
    if (! (index > static_cast<double>(firstBin()))) {
        return firstBin();
    }
    return index >= static_cast<double>(amplitude_.size()) ? amplitude_.size()
                                                           : static_cast<size_t>(index);
}

std::optional<size_t> SpectrumWidget::binAtPixel(double x, const QRect& area) const {
    if (amplitude_.size() <= firstBin() || ! (binWidth_ > 0.0)) {
        return std::nullopt;
    }
    const double column = std::floor(x);
    const size_t begin  = columnBoundary(column, area);
    const size_t end    = columnBoundary(column + 1.0, area);
    if (end > begin) {
        // The bin whose line reaches highest in the column is the one the eye
        // sees there when zoomed out.
        const auto from = amplitude_.begin() + static_cast<std::ptrdiff_t>(begin);
        const auto to   = amplitude_.begin() + static_cast<std::ptrdiff_t>(end);
        return static_cast<size_t>(std::distance(amplitude_.begin(), std::max_element(from, to)));
    }
    // Zoomed in, most columns fall between two bins; the nearer one is read.
    std::optional<size_t> nearest;
    if (begin > firstBin()) {
        nearest = begin - 1;
    }
    if (begin < amplitude_.size() && (! nearest || std::abs(binPixel(begin, area) - x) <
                                                       std::abs(binPixel(*nearest, area) - x))) {
        nearest = begin;
    }
    return nearest;
}

std::vector<SpectrumWidget::Tick> SpectrumWidget::axisTicks(
    double minimum,
    double maximum,
    bool logarithmic,
    double pixels,
    int linearTicks,
    double labelSpacing
) const {
    std::vector<Tick> ticks;
    const double span = maximum - minimum;
    if (! (span > 0.0) || ! (pixels > 0.0)) {
        return ticks;
    }

    // Round values at a round step, every one labeled - the canvas' grid. The
    // tolerance keeps a tick on the frame line that a fitted view puts there.
    const auto addLinear = [&](double lower, double upper, auto toPosition) {
        constexpr double Tolerance = 1e-9;
        const double step          = plotscale::niceStep(upper - lower, linearTicks);
        const double first         = std::ceil(lower / step - Tolerance);
        const double last          = std::floor(upper / step + Tolerance);
        for (double index = first; index <= last; ++index) {
            const double value = index * step;
            if (logarithmic && ! (value > 0.0)) {
                continue;
            }
            ticks.push_back({toPosition(value), value, true, true});
        }
    };
    if (! logarithmic) {
        addLinear(minimum, maximum, [](double value) { return value; });
        return ticks;
    }

    // Decades first, labeled at a stride that keeps their labels apart - every
    // decade on a tall plot, every other one or fewer on a squat one - and
    // drawn as full grid lines as long as that does not fill the plot.
    const double pixelsPerDecade = pixels / span;
    const auto candidates        = plotscale::logTicks(
        std::pow(10.0, minimum), std::pow(10.0, maximum), pixelsPerDecade >= MinimumSubDecadeSpacing
    );
    const int stride = std::max(1, static_cast<int>(std::ceil(labelSpacing / pixelsPerDecade)));
    std::vector<double> labelPixels;
    const auto pixelOf = [&](double position) { return (position - minimum) / span * pixels; };
    for (const plotscale::LogTick& candidate : candidates) {
        const double position = std::log10(candidate.value);
        if (candidate.multiple == 1) {
            const int exponent = static_cast<int>(std::lround(position));
            const bool labeled = floorModulo(exponent, stride) == 0;
            if (labeled || pixelsPerDecade >= MinimumFineGridSpacing) {
                ticks.push_back({position, candidate.value, true, labeled});
            }
            if (labeled) {
                labelPixels.push_back(pixelOf(position));
            }
        } else {
            ticks.push_back({position, candidate.value, false, false});
        }
    }

    // Then the sub-decade lines where their labels fit between the ones already
    // placed: 2 and 5 whenever there is room, the others only when the view is
    // too narrow to show two labels otherwise - a scale with a single label
    // cannot be read.
    const auto labelWhereItFits = [&](auto accepts) {
        for (Tick& tick : ticks) {
            if (tick.labeled || ! accepts(tick)) {
                continue;
            }
            const double pixel = pixelOf(tick.position);
            if (std::all_of(labelPixels.begin(), labelPixels.end(), [&](double placed) {
                    return std::abs(placed - pixel) >= labelSpacing;
                })) {
                tick.labeled = true;
                labelPixels.push_back(pixel);
            }
        }
    };
    const auto multipleOf = [](const Tick& tick) {
        return static_cast<int>(
            std::lround(tick.value / std::pow(10.0, std::floor(tick.position)))
        );
    };
    labelWhereItFits([&](const Tick& tick) {
        const int multiple = multipleOf(tick);
        return multiple == 2 || multiple == 5;
    });
    if (labelPixels.size() < 2) {
        labelWhereItFits([](const Tick&) { return true; });
    }
    // Zoomed into a stretch without two labeled decade lines - 110 to 190 Hz,
    // say - the scale is linear enough to label like a linear one.
    if (labelPixels.size() < 2) {
        ticks.clear();
        addLinear(std::pow(10.0, minimum), std::pow(10.0, maximum), [](double value) {
            return std::log10(value);
        });
    }
    return ticks;
}

void SpectrumWidget::drawGrid(QPainter& painter, const QRect& area, const QColor& textColor) const {
    const bool darkMode = palette().color(QPalette::Base).lightness() < 128;
    QColor gridColor    = palette().color(QPalette::Mid);
    gridColor.setAlpha(darkMode ? 145 : 90);
    // As faint as the canvas' snap grid: the sub-decade lines are what makes a
    // logarithmic scale readable between its labels, not a grid of their own.
    const QColor minorColor        = plotscale::translucent(gridColor, darkMode ? 60 : 40);

    const std::vector<Tick> xTicks = axisTicks(
        view_.xMin,
        view_.xMax,
        logFrequency_,
        area.width(),
        std::max(2, area.width() / 90),
        FrequencyLabelSpacing
    );
    const std::vector<Tick> yTicks = axisTicks(
        view_.yMin,
        view_.yMax,
        logAmplitude_,
        area.height(),
        std::max(2, area.height() / 58),
        AmplitudeLabelSpacing
    );

    // The faint lines first, so a full one is never drawn over by them.
    for (const bool major : {false, true}) {
        painter.setPen(QPen(major ? gridColor : minorColor, 1.0));
        for (const Tick& tick : xTicks) {
            if (tick.major == major) {
                const double pixel = xToPixel(tick.position, area);
                painter.drawLine(QPointF(pixel, area.top()), QPointF(pixel, area.bottom()));
            }
        }
        for (const Tick& tick : yTicks) {
            if (tick.major == major) {
                const double pixel = yToPixel(tick.position, area);
                painter.drawLine(QPointF(area.left(), pixel), QPointF(area.right(), pixel));
            }
        }
    }

    painter.setPen(textColor);
    for (const Tick& tick : xTicks) {
        if (tick.labeled) {
            const double pixel = xToPixel(tick.position, area);
            const QRectF labelRect(pixel - 38.0, area.bottom() + 5.0, 76.0, 22.0);
            painter.drawText(
                labelRect,
                Qt::AlignHCenter | Qt::AlignTop,
                plotscale::siLabel(tick.value, QStringLiteral("Hz"))
            );
        }
    }
    for (const Tick& tick : yTicks) {
        if (tick.labeled) {
            const double pixel = yToPixel(tick.position, area);
            const QRectF labelRect(1.0, pixel - 10.0, LeftMargin - 8.0, 20.0);
            painter.drawText(
                labelRect,
                Qt::AlignRight | Qt::AlignVCenter,
                plotscale::exactSiLabel(tick.value, {})
            );
        }
    }
}

void SpectrumWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event)
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
        QRectF(area.left(), 0.0, area.width(), TopMargin),
        Qt::AlignCenter,
        tr("Frequency / Amplitude")
    );

    if (amplitude_.empty()) {
        if (! message_.isEmpty()) {
            // Muted like a legend entry that does not contribute: it explains
            // the empty plot, it is not a value.
            painter.setPen(palette.color(QPalette::Disabled, QPalette::Text));
            painter.drawText(
                area.adjusted(12, 8, -12, -8), Qt::AlignCenter | Qt::TextWordWrap, message_
            );
        }
        return;
    }

    // The color of the waveform's result curve: this is the spectrum of that
    // very curve.
    QColor curveColor = palette.color(QPalette::Highlight);
    if (darkMode && curveColor.lightness() < 155) {
        curveColor = curveColor.lighter(175);
    }
    drawSpectrum(painter, area, curveColor);
}

void SpectrumWidget::drawSpectrum(QPainter& painter, const QRect& area, const QColor& color) const {
    const size_t count = amplitude_.size();
    if (count <= firstBin() || ! (binWidth_ > 0.0)) {
        return;
    }

    // The spectrum is reduced to at most one group per pixel column, which bounds
    // what is drawn by the plot's width however many bins there are. A column
    // that holds several bins becomes one vertical line from the smallest to the
    // largest of them - the largest is the line the spectrum is about, so a peak
    // survives any zoom level, and the smallest keeps the noise floor visible
    // below it. A column with a single bin keeps that bin's exact position.
    // Consecutive groups are joined by a straight line, the trace a spectrum
    // analyzer shows: stems from the bottom would be the honest picture of a
    // discrete spectrum, but on a logarithmic amplitude axis the bottom is just
    // wherever the view ends, and dense stems fill the plot below the noise
    // floor with color.
    struct Group {
        double x;
        double first;   // the leftmost bin's pixel row, where the line from the left arrives
        double last;    // the rightmost bin's, where the line to the right leaves
        double top;     // the largest amplitude's
        double bottom;  // the smallest amplitude's
        bool single;
    };
    std::vector<Group> groups;
    groups.reserve(static_cast<size_t>(area.width()) + 4);
    const auto singleBin = [&](size_t bin) {
        const double y = amplitudePixel(amplitude_[bin], area);
        return Group{binPixel(bin, area), y, y, y, y, true};
    };

    // The bin on either side of the view is included so the trace runs out to
    // the frame instead of stopping at the last bin inside it. The column right
    // of the plot is the frame line itself, where the fitted view puts the last
    // bin.
    const int lastColumn = area.right() + 1;
    size_t begin         = columnBoundary(area.left(), area);
    if (begin > firstBin()) {
        groups.push_back(singleBin(begin - 1));
    }
    for (int column = area.left(); column <= lastColumn; ++column) {
        const size_t end = std::max(begin, columnBoundary(column + 1.0, area));
        if (end == begin + 1) {
            groups.push_back(singleBin(begin));
        } else if (end > begin) {
            const auto from               = amplitude_.begin() + static_cast<std::ptrdiff_t>(begin);
            const auto to                 = amplitude_.begin() + static_cast<std::ptrdiff_t>(end);
            const auto [minimum, maximum] = std::minmax_element(from, to);
            // The amplitude-to-pixel mapping is monotonic, so only the four
            // values that matter are mapped - not a logarithm per bin, which
            // would be the bulk of the paint time for millions of bins.
            groups.push_back(
                {column + 0.5,
                 amplitudePixel(*from, area),
                 amplitudePixel(*(to - 1), area),
                 amplitudePixel(*maximum, area),
                 amplitudePixel(*minimum, area),
                 false}
            );
        }
        begin = end;
    }
    if (begin < count) {
        groups.push_back(singleBin(begin));
    }

    std::vector<QLineF> lines;
    lines.reserve(2 * groups.size());
    const double leftLimit  = area.left() - 4.0;
    const double rightLimit = area.right() + 5.0;
    for (size_t index = 0; index < groups.size(); ++index) {
        Group group = groups[index];
        if (index > 0) {
            const Group& previous = groups[index - 1];
            if (! group.single && group.x - previous.x <= 1.5) {
                // Adjacent dense columns are joined the way the canvas joins
                // them: the vertical line reaches to where the previous column
                // ended, which keeps every line vertical and cheap to stroke.
                group.top    = std::min(group.top, previous.last);
                group.bottom = std::max(group.bottom, previous.last);
            } else if (
                const QLineF joint = clipHorizontally(
                    {previous.x, previous.last}, {group.x, group.first}, leftLimit, rightLimit
                );
                ! joint.isNull()
            ) {
                lines.push_back(joint);
            }
        }
        if (! group.single) {
            lines.emplace_back(group.x, group.top, group.x, group.bottom);
        }
    }

    painter.save();
    // The frame lines count as plot here: the fitted view puts the first and the
    // last bin on them.
    painter.setClipRect(area.adjusted(0, 0, 1, 1));
    painter.setRenderHint(QPainter::Antialiasing, true);
    const bool darkMode = palette().color(QPalette::Base).lightness() < 128;
    // Round caps give a zero-length line - a column whose bins are all equal -
    // a visible dot, so a flat noise floor still reads as a line.
    painter.setPen(QPen(color, darkMode ? 2.0 : 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLines(lines.data(), static_cast<int>(lines.size()));
    painter.restore();
}

void SpectrumWidget::wheelEvent(QWheelEvent* event) {
    const bool overPlot   = plotRect().contains(event->position().toPoint());
    const AxisRegion axis = axisAtPosition(event->position());
    // A wheel tilted sideways carries no vertical rotation; scaling on that would
    // make an axis jump while one only scrolls past it.
    const int rotation = event->angleDelta().y();
    if ((! overPlot && axis == AxisRegion::None) || rotation == 0) {
        event->ignore();
        return;
    }

    // The canvas' convention: on an axis strip only that axis is scaled, over
    // the plot the wheel scales frequency, Ctrl and the wheel the amplitude.
    const bool amplitude = axis == AxisRegion::Amplitude ||
                           (overPlot && event->modifiers().testFlag(Qt::ControlModifier));
    const bool frequency = axis == AxisRegion::Frequency || (overPlot && ! amplitude);
    zoomView(frequency, amplitude, rotation > 0 ? 0.8 : 1.25, event->position());
    event->accept();
}

void SpectrumWidget::zoomView(
    bool frequency, bool amplitude, double factor, const QPointF& position
) {
    const QRect area = plotRect();
    // Zooming in stops where the span would drown in the rounding of its bounds
    // and the mapping to pixels would divide by zero.
    const auto zoomable = [factor](double lower, double upper, double anchor) {
        return factor >= 1.0 || (upper - lower) * factor > std::max(std::abs(anchor), 1.0) * 1e-12;
    };
    if (frequency) {
        const double anchor = pixelToX(position.x(), area);
        if (zoomable(view_.xMin, view_.xMax, anchor)) {
            view_.xMin = anchor + (view_.xMin - anchor) * factor;
            view_.xMax = anchor + (view_.xMax - anchor) * factor;
        }
    }
    if (amplitude) {
        const double anchor = pixelToY(position.y(), area);
        if (zoomable(view_.yMin, view_.yMax, anchor)) {
            view_.yMin = anchor + (view_.yMin - anchor) * factor;
            view_.yMax = anchor + (view_.yMax - anchor) * factor;
        }
    }
    limitView();
    updateCursorReadout(position);
    update();
}

void SpectrumWidget::mousePressEvent(QMouseEvent* event) {
    // Any press, before anything else: clicking the spectrum is how its settings
    // come up in the sidebar, whatever else the click goes on to do.
    emit activated();
    setFocus(Qt::MouseFocusReason);

    const AxisRegion axis = axisAtPosition(event->position());
    const bool overPlot   = plotRect().contains(event->position().toPoint());
    if (event->button() == Qt::LeftButton && (overPlot || axis != AxisRegion::None)) {
        // Over the plot a drag pans both axes; on a scale only that one, as on
        // the canvas. There is nothing to select here, so every drag is a pan.
        panning_      = true;
        panAxis_      = axis;
        panStart_     = event->position().toPoint();
        panStartView_ = view_;
        if (overPlot) {
            setCursor(Qt::ClosedHandCursor);
        }
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void SpectrumWidget::mouseMoveEvent(QMouseEvent* event) {
    if (panning_) {
        const QRect area   = plotRect();
        const QPoint delta = event->position().toPoint() - panStart_;
        // A pan started on a scale is locked to that axis: the movement across it
        // is what one aims at, the movement along it is not meant to do anything.
        const int alongFrequency = panAxis_ == AxisRegion::Amplitude ? 0 : delta.x();
        const int alongAmplitude = panAxis_ == AxisRegion::Frequency ? 0 : delta.y();
        const double xScale      = (panStartView_.xMax - panStartView_.xMin) / area.width();
        const double yScale      = (panStartView_.yMax - panStartView_.yMin) / area.height();
        view_.xMin               = panStartView_.xMin - alongFrequency * xScale;
        view_.xMax               = panStartView_.xMax - alongFrequency * xScale;
        view_.yMin               = panStartView_.yMin + alongAmplitude * yScale;
        view_.yMax               = panStartView_.yMax + alongAmplitude * yScale;
        limitView();
        update();
    } else {
        updateHoverCursor(event->position());
    }
    updateCursorReadout(event->position());
    QWidget::mouseMoveEvent(event);
}

void SpectrumWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && panning_) {
        panning_ = false;
        panAxis_ = AxisRegion::None;
        updateHoverCursor(event->position());
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void SpectrumWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }
    // A scale fits its own axis and leaves the other alone; the plot fits both.
    const AxisRegion axis = axisAtPosition(event->position());
    if (axis == AxisRegion::Frequency) {
        fitFrequencyAxis();
    } else if (axis == AxisRegion::Amplitude) {
        fitAmplitudeAxis();
    } else if (plotRect().contains(event->position().toPoint())) {
        fitFrequencyAxis();
        fitAmplitudeAxis();
    } else {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }
    updateCursorReadout(event->position());
    update();
    event->accept();
}

void SpectrumWidget::leaveEvent(QEvent* event) {
    if (! panning_) {
        unsetCursor();
    }
    emit cursorReadout(std::numeric_limits<double>::quiet_NaN(), 0.0);
    QWidget::leaveEvent(event);
}

void SpectrumWidget::updateHoverCursor(const QPointF& position) {
    // The double arrow says which direction the wheel stretches here; otherwise
    // the axes would look as dead as the rest of the margin.
    switch (axisAtPosition(position)) {
        case AxisRegion::Frequency:
            setCursor(Qt::SizeHorCursor);
            break;
        case AxisRegion::Amplitude:
            setCursor(Qt::SizeVerCursor);
            break;
        case AxisRegion::None:
            unsetCursor();
            break;
    }
}

void SpectrumWidget::updateCursorReadout(const QPointF& position) {
    const QRect area = plotRect();
    const auto bin =
        area.contains(position.toPoint()) ? binAtPixel(position.x(), area) : std::nullopt;
    if (! bin) {
        emit cursorReadout(std::numeric_limits<double>::quiet_NaN(), 0.0);
        return;
    }
    // The bin's own frequency rather than the pointer's: the pair then names a
    // line of the spectrum exactly, which is what one hovers it for.
    emit cursorReadout(static_cast<double>(*bin) * binWidth_, amplitude_[*bin]);
}

}  // namespace zwe
