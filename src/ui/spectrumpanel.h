// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QWidget>
#include <functional>
#include <optional>
#include <vector>

#include "core/spectrum.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

namespace zwe {

class EngineeringEdit;

// The sidebar page of the spectrum below the waveform, shown in place of the
// property panel once the spectrum is clicked: how the spectrum is computed and
// displayed, and the statistics of the analyzed signal as text. Nothing is
// stored; the settings last as long as the window.
class SpectrumPanel final : public QWidget {
    Q_OBJECT

public:
    struct Settings {
        double sampleRate       = 1000.0;  // analysis rate, 1/s
        spectrum::Window window = spectrum::Window::Rectangular;
        bool logFrequency       = true;
        bool logAmplitude       = false;

        bool operator==(const Settings&) const = default;
    };

    explicit SpectrumPanel(QWidget* parent = nullptr);

    Settings settings() const;

    // The document's value rate. The analysis rate follows it - stays equal to
    // it - until a rate of its own is entered, and follows again once the rate is
    // set back to the value rate. Emits settingsChanged() when that changes the
    // analysis rate.
    void setValueRate(double valueRate);

    // The figures of the last analysis, replacing any message.
    void setStatistics(const spectrum::Statistics& statistics);
    // Shown instead of the figures - that an analysis is running, or why none
    // could be made. An empty message with no statistics shows nothing.
    void setMessage(const QString& message);

signals:
    // Emitted after any setting changed; read settings() to apply it.
    void settingsChanged();

private:
    // One line of the statistics: the value label is rewritten from the
    // statistics on every update, the name stays as built.
    struct FigureRow {
        QLabel* name  = nullptr;
        QLabel* value = nullptr;
        std::function<QString(const spectrum::Statistics&)> format;
    };

    void commitSampleRate(double value);
    void followValueRate();
    // Sets the analysis rate, and emits settingsChanged() if that changed it.
    void applySampleRate(double rate);
    void updateRateHint();
    void updateFigures();
    void copyFigures() const;

    Settings settings_;
    // Zero until the first valid setValueRate(): without it there is nothing to
    // follow and nothing to relate the rate to.
    double valueRate_     = 0.0;
    bool followValueRate_ = true;
    std::optional<spectrum::Statistics> statistics_;
    QString message_;

    EngineeringEdit* sampleRate_ = nullptr;
    QLabel* rateHint_            = nullptr;
    QComboBox* window_           = nullptr;
    QCheckBox* logFrequency_     = nullptr;
    QCheckBox* logAmplitude_     = nullptr;
    QPushButton* copy_           = nullptr;
    QLabel* messageLabel_        = nullptr;
    QWidget* figures_            = nullptr;
    QLabel* aliasingWarning_     = nullptr;
    std::vector<FigureRow> figureRows_;
};

}  // namespace zwe
