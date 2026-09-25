// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "spectrumpanel.h"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>
#include <cmath>

#include "engineeringedit.h"
#include "engineeringnotation.h"

namespace zwe {

namespace {

// Above this share of the AC power close to Nyquist the spectrum most likely
// does not end there: whatever lies beyond has been folded back onto it.
constexpr double AliasingWarningFraction = 0.01;

// What a figure the signal does not define reads as - an en dash rather than
// "nan", which says "no value" without looking like a malfunction.
QString undefinedFigure() {
    return QStringLiteral("–");
}

QLabel* headingLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    QFont font  = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

// A value with its unit in engineering notation, "1.5 ms". format() puts the SI
// prefix right after the number ("1.5m"); the space goes in front of the prefix
// so that prefix and unit read as one, the way they are written by hand.
QString quantity(double value, const QString& unit) {
    if (! std::isfinite(value)) {
        return undefinedFigure();
    }
    QString text = engineering::format(value);
    if (unit.isEmpty()) {
        return text;
    }
    const qsizetype prefixAt =
        ! text.isEmpty() && text.back().isLetter() ? text.size() - 1 : text.size();
    text.insert(prefixAt, QLatin1Char(' '));
    return text + unit;
}

// A rate in the notation of the rate field and the value rate box, "2k 1/s":
// the prefix stays with the number, since "k1/s" would read as nothing at all.
QString rate(double value) {
    if (! std::isfinite(value)) {
        return undefinedFigure();
    }
    return engineering::format(value) + QStringLiteral(" 1/s");
}

QString ratio(double value) {
    if (! std::isfinite(value)) {
        return undefinedFigure();
    }
    return QString::number(value, 'g', 4);
}

QString percent(double fraction) {
    if (! std::isfinite(fraction)) {
        return undefinedFigure();
    }
    return QString::number(fraction * 100.0, 'g', 3) + QStringLiteral(" %");
}

}  // namespace

SpectrumPanel::SpectrumPanel(QWidget* parent) : QWidget(parent) {
    // The same margins as the property panel, so swapping the two in the dock
    // does not make the content jump.
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);

    layout->addWidget(headingLabel(tr("Spectrum (FFT)"), this));

    auto* form = new QFormLayout;
    layout->addLayout(form);

    sampleRate_ = new EngineeringEdit(this);
    sampleRate_->setObjectName(QStringLiteral("spectrumSampleRateEdit"));
    sampleRate_->setValue(settings_.sampleRate);
    form->addRow(tr("Sample rate (1/s)"), sampleRate_);
    const QString rateTooltip =
        tr("The rate at which the output is sampled for the FFT. The output holds every value "
           "until the next one, so it is a staircase; at the value rate the spectrum shows the "
           "values themselves, while a multiple of the value rate also reveals the images of "
           "the held output around multiples of the value rate. Follows the value rate until "
           "you enter a rate of your own, and again once you enter the value rate itself.");
    sampleRate_->setToolTip(rateTooltip);
    if (QWidget* label = form->labelForField(sampleRate_)) {
        label->setToolTip(rateTooltip);
    }

    // The ratio is what the rate means for the spectrum - whether it shows the
    // images of the hold, or folds part of the signal - and that is easier to
    // read off "2 x value rate" than off two numbers in different places.
    rateHint_ = new QLabel(this);
    rateHint_->setObjectName(QStringLiteral("spectrumRateHintLabel"));
    form->addRow(QString(), rateHint_);

    window_ = new QComboBox(this);
    window_->setObjectName(QStringLiteral("spectrumWindowCombo"));
    // In the order of spectrum::Window, so a combo box index is the enumerator.
    const struct {
        QString name;
        QString tooltip;
    } windows[] = {
        {tr("Rectangular"),
         tr("No window. Exact for a signal that is periodic in the analyzed stretch; anything "
            "else leaks into wide skirts around every peak.")},
        {tr("Hann"), tr("A good general-purpose compromise between peak width and leakage.")},
        {tr("Flat top"), tr("The most accurate amplitudes, at the cost of the widest peaks.")},
        {tr("Blackman-Harris"),
         tr("The lowest leakage, for finding small components next to large ones.")}
    };
    for (const auto& entry : windows) {
        window_->addItem(entry.name);
        window_->setItemData(window_->count() - 1, entry.tooltip, Qt::ToolTipRole);
    }
    window_->setCurrentIndex(static_cast<int>(settings_.window));
    form->addRow(tr("Window"), window_);
    const QString windowTooltip =
        tr("The weighting applied to the analyzed stretch before the FFT. It trades the width "
           "of a peak against how much of it leaks into its neighbors.");
    window_->setToolTip(windowTooltip);
    if (QWidget* label = form->labelForField(window_)) {
        label->setToolTip(windowTooltip);
    }

    logFrequency_ = new QCheckBox(tr("Logarithmic frequency axis"), this);
    logFrequency_->setObjectName(QStringLiteral("spectrumLogFrequencyCheck"));
    logFrequency_->setChecked(settings_.logFrequency);
    logFrequency_->setToolTip(tr("Give every decade of frequency the same width"));
    form->addRow(logFrequency_);

    logAmplitude_ = new QCheckBox(tr("Logarithmic amplitude axis"), this);
    logAmplitude_->setObjectName(QStringLiteral("spectrumLogAmplitudeCheck"));
    logAmplitude_->setChecked(settings_.logAmplitude);
    logAmplitude_->setToolTip(
        tr("Show small components next to large ones - harmonics, images and noise are "
           "usually decades below the fundamental")
    );
    form->addRow(logAmplitude_);

    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    layout->addWidget(separator);

    auto* statisticsHeading = new QHBoxLayout;
    statisticsHeading->addWidget(headingLabel(tr("Statistics"), this));
    statisticsHeading->addStretch(1);
    copy_ = new QPushButton(tr("Copy"), this);
    copy_->setObjectName(QStringLiteral("spectrumCopyButton"));
    copy_->setToolTip(
        tr("Copy all figures to the clipboard, one name and value per line separated by a "
           "tab, ready to paste into a spreadsheet")
    );
    statisticsHeading->addWidget(copy_);
    layout->addLayout(statisticsHeading);

    // The editor does not know how the waveform is run, so the levels are plain
    // numbers; this says what they turn into.
    auto* unitNote = new QLabel(
        tr("Levels carry no unit: they are volts or amperes, depending on whether the waveform "
           "is output as a voltage or as a current. The slew rate is then in V/s or A/s."),
        this
    );
    unitNote->setObjectName(QStringLiteral("spectrumUnitNote"));
    unitNote->setWordWrap(true);
    unitNote->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    unitNote->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(unitNote);

    messageLabel_ = new QLabel(this);
    messageLabel_->setObjectName(QStringLiteral("spectrumMessageLabel"));
    messageLabel_->setWordWrap(true);
    messageLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    messageLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(messageLabel_);

    // The figures are a form of plain labels rather than one block of rich text:
    // names and values line up exactly like the fields of the property panel,
    // and a single value can still be selected and copied on its own.
    figures_ = new QWidget(this);
    figures_->setObjectName(QStringLiteral("spectrumFiguresWidget"));
    auto* figuresForm = new QFormLayout(figures_);
    figuresForm->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(figures_);

    using Format        = std::function<QString(const spectrum::Statistics&)>;
    const auto addGroup = [this, figuresForm](const QString& title) {
        figuresForm->addRow(headingLabel(title, figures_));
    };
    const auto addFigure = [this, figuresForm](
                               const char* objectName,
                               const QString& name,
                               Format format,
                               const QString& tooltip = QString()
                           ) {
        auto* nameLabel  = new QLabel(name, figures_);
        auto* valueLabel = new QLabel(figures_);
        valueLabel->setObjectName(QLatin1String(objectName));
        valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        if (! tooltip.isEmpty()) {
            nameLabel->setToolTip(tooltip);
            valueLabel->setToolTip(tooltip);
        }
        figuresForm->addRow(nameLabel, valueLabel);
        figureRows_.push_back({nameLabel, valueLabel, std::move(format)});
    };
    const QString hertz  = QStringLiteral("Hz");
    const QString second = QStringLiteral("s");
    using Statistics     = spectrum::Statistics;
    // A level of the output, without a unit: volts or amperes, see the note.
    const auto levels = [](double Statistics::* member) {
        return [member](const Statistics& s) { return quantity(s.*member, QString()); };
    };

    addGroup(tr("Signal"));
    addFigure(
        "spectrumCountValue",
        tr("Samples"),
        [](const Statistics& s) { return QString::number(static_cast<qulonglong>(s.count)); },
        tr("How many samples of the output the FFT was computed from")
    );
    addFigure("spectrumRateValue", tr("Analysis rate"), [](const Statistics& s) {
        return rate(s.rate);
    });
    addFigure(
        "spectrumDurationValue",
        tr("Duration"),
        [second](const Statistics& s) { return quantity(s.duration, second); },
        tr("The analyzed stretch of the output: the samples divided by the analysis rate")
    );

    addGroup(tr("Time domain"));
    addFigure("spectrumMeanValue", tr("DC (mean)"), levels(&Statistics::mean));
    addFigure("spectrumRmsValue", tr("RMS"), levels(&Statistics::rms));
    addFigure(
        "spectrumAcRmsValue",
        tr("AC RMS"),
        levels(&Statistics::acRms),
        tr("The RMS without the DC part: what an AC coupled meter would read")
    );
    addFigure("spectrumMinimumValue", tr("Minimum"), levels(&Statistics::minimum));
    addFigure("spectrumMaximumValue", tr("Maximum"), levels(&Statistics::maximum));
    addFigure(
        "spectrumPeakValue",
        tr("Peak"),
        levels(&Statistics::peak),
        tr("The largest magnitude of the output, of either sign")
    );
    addFigure("spectrumPeakToPeakValue", tr("Peak-to-peak"), levels(&Statistics::peakToPeak));
    addFigure(
        "spectrumCrestFactorValue",
        tr("Crest factor"),
        [](const Statistics& s) { return ratio(s.crestFactor); },
        tr("Peak divided by RMS - 1.414 for a sine, 1 for a square wave")
    );
    addFigure(
        "spectrumFormFactorValue",
        tr("Form factor"),
        [](const Statistics& s) { return ratio(s.formFactor); },
        tr("RMS divided by the mean magnitude - 1.111 for a sine, 1 for a square wave")
    );
    addFigure(
        "spectrumMaxStepValue",
        tr("Max. step"),
        levels(&Statistics::maxStep),
        tr("The largest jump between two consecutive output values")
    );
    addFigure(
        "spectrumMaxSlewRateValue",
        tr("Max. slew rate"),
        [](const Statistics& s) { return rate(s.maxSlewRate); },
        tr("The largest jump per value period - the slew rate the output stage has to manage")
    );

    addGroup(tr("Spectrum"));
    addFigure(
        "spectrumBinWidthValue",
        tr("Resolution Δf"),
        [hertz](const Statistics& s) { return quantity(s.binWidth, hertz); },
        tr("The spacing of the spectral lines: the analysis rate divided by the samples, or "
           "one over the duration")
    );
    addFigure(
        "spectrumNyquistValue",
        tr("Nyquist frequency"),
        [hertz](const Statistics& s) { return quantity(s.nyquist, hertz); },
        tr("Half the analysis rate, the highest frequency the spectrum can show")
    );
    addFigure(
        "spectrumFundamentalFrequencyValue",
        tr("Fundamental"),
        [hertz](const Statistics& s) { return quantity(s.fundamentalFrequency, hertz); },
        tr("The frequency of the strongest spectral line above DC")
    );
    addFigure(
        "spectrumFundamentalAmplitudeValue",
        tr("Fundamental amplitude"),
        levels(&Statistics::fundamentalAmplitude),
        tr("The peak amplitude of the fundamental")
    );
    addFigure(
        "spectrumThdValue",
        tr("THD"),
        [](const Statistics& s) { return percent(s.thd); },
        tr("Total harmonic distortion: the RMS of the harmonics of the fundamental relative "
           "to the fundamental itself")
    );
    addFigure(
        "spectrumNyquistPowerValue",
        tr("Power near Nyquist"),
        [](const Statistics& s) { return percent(s.nyquistPowerFraction); },
        tr("The share of the AC power in the top tenth of the spectrum below the Nyquist "
           "frequency. A signal sampled fast enough has next to nothing there.")
    );

    aliasingWarning_ = new QLabel(this);
    aliasingWarning_->setObjectName(QStringLiteral("spectrumAliasingWarning"));
    aliasingWarning_->setWordWrap(true);
    aliasingWarning_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(aliasingWarning_);

    // Keeps the content at the top of a dock that is taller than the page.
    layout->addStretch(1);

    connect(sampleRate_, &EngineeringEdit::valueCommitted, this, &SpectrumPanel::commitSampleRate);
    connect(window_, &QComboBox::currentIndexChanged, this, [this](int index) {
        const auto window = static_cast<spectrum::Window>(index);
        if (index < 0 || window == settings_.window) {
            return;
        }
        settings_.window = window;
        emit settingsChanged();
    });
    connect(logFrequency_, &QCheckBox::toggled, this, [this](bool checked) {
        settings_.logFrequency = checked;
        emit settingsChanged();
    });
    connect(logAmplitude_, &QCheckBox::toggled, this, [this](bool checked) {
        settings_.logAmplitude = checked;
        emit settingsChanged();
    });
    connect(copy_, &QPushButton::clicked, this, &SpectrumPanel::copyFigures);

    updateRateHint();
    updateFigures();
}

SpectrumPanel::Settings SpectrumPanel::settings() const {
    return settings_;
}

void SpectrumPanel::setValueRate(double valueRate) {
    // A document always carries a positive rate; anything else would turn the
    // analysis rate into something no FFT can be computed at.
    if (! std::isfinite(valueRate) || valueRate <= 0.0) {
        return;
    }
    valueRate_ = valueRate;
    if (followValueRate_) {
        applySampleRate(valueRate);
    } else {
        updateRateHint();
    }
}

void SpectrumPanel::setStatistics(const spectrum::Statistics& statistics) {
    statistics_ = statistics;
    message_.clear();
    updateFigures();
}

void SpectrumPanel::setMessage(const QString& message) {
    // The figures are dropped rather than just hidden: a message means the last
    // analysis no longer stands - a new one is running, or none can be made - so
    // an empty message later must not bring back figures of a signal that may
    // not exist any more.
    message_ = message;
    statistics_.reset();
    updateFigures();
}

void SpectrumPanel::commitSampleRate(double value) {
    // A rate of zero or less has no spectrum; restoring the field says so.
    if (! std::isfinite(value) || value <= 0.0) {
        sampleRate_->setValue(settings_.sampleRate);
        return;
    }
    // The field shows four significant digits, and leaving it commits what it
    // shows. A value rate of 1234567 1/s reads "1.235M" there, so merely tabbing
    // through the field would change the rate and stop it from following. What
    // looks the same as before therefore changes nothing, and the field gets the
    // exact rate back.
    if (engineering::format(value) == engineering::format(settings_.sampleRate)) {
        sampleRate_->setValue(settings_.sampleRate);
        return;
    }
    // The same goes for typing the value rate as shown in the toolbar: that is
    // asking to follow it, not for a rate that is almost but not quite it.
    if (valueRate_ > 0.0 && engineering::format(value) == engineering::format(valueRate_)) {
        followValueRate();
        return;
    }
    followValueRate_ = false;
    applySampleRate(value);
}

void SpectrumPanel::followValueRate() {
    followValueRate_ = true;
    if (valueRate_ > 0.0) {
        applySampleRate(valueRate_);
    } else {
        updateRateHint();
    }
}

void SpectrumPanel::applySampleRate(double rate) {
    const bool changed   = rate != settings_.sampleRate;
    settings_.sampleRate = rate;
    sampleRate_->setValue(rate);
    updateRateHint();
    if (changed) {
        emit settingsChanged();
    }
}

void SpectrumPanel::updateRateHint() {
    if (valueRate_ <= 0.0) {
        rateHint_->hide();
        return;
    }
    const double multiple = settings_.sampleRate / valueRate_;
    // Four digits, like every other number of the panel; that also hides the
    // last-digit noise a typed rate brings into the division.
    const QString multipleText = QString::number(multiple, 'g', 4);
    rateHint_->setText(
        multipleText == QStringLiteral("1") ? tr("value rate")
                                            : tr("%1 × value rate").arg(multipleText)
    );
    rateHint_->show();
}

void SpectrumPanel::updateFigures() {
    const bool hasFigures = statistics_.has_value();
    messageLabel_->setText(message_);
    messageLabel_->setVisible(! message_.isEmpty());
    figures_->setVisible(hasFigures);
    copy_->setEnabled(hasFigures);
    if (! hasFigures) {
        aliasingWarning_->hide();
        return;
    }
    for (const FigureRow& row : figureRows_) {
        row.value->setText(row.format(*statistics_));
    }
    const double nearNyquist = statistics_->nyquistPowerFraction;
    if (std::isfinite(nearNyquist) && nearNyquist > AliasingWarningFraction) {
        aliasingWarning_->setText(
            tr("%1 of the AC power lies close to the Nyquist frequency. The sample rate may be "
               "too low: parts of the signal above it are folded back into the spectrum "
               "(aliasing).")
                .arg(percent(nearNyquist))
        );
        aliasingWarning_->show();
    } else {
        aliasingWarning_->hide();
    }
}

void SpectrumPanel::copyFigures() const {
    if (! statistics_) {
        return;
    }
    // Tab separated, so pasting into a spreadsheet puts names and values into two
    // columns; the values keep their units, the way they are shown.
    QStringList lines;
    for (const FigureRow& row : figureRows_) {
        lines.append(row.name->text() + QLatin1Char('\t') + row.value->text());
    }
    QGuiApplication::clipboard()->setText(lines.join(QLatin1Char('\n')) + QLatin1Char('\n'));
}

}  // namespace zwe
