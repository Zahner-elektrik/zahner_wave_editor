// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QObject>
#include <QtGlobal>
#include <atomic>
#include <memory>

#include "core/spectrum.h"
#include "core/wavedocument.h"

class QTimer;

namespace zwe {

// Runs spectrum::analyze() off the GUI thread, so a long document or an
// expensive formula never freezes the editor while the spectrum follows an edit.
//
// Requests are coalesced: a drag produces one per mouse move, and only the
// latest of them is worth analyzing. The analysis starts a short while after the
// last request, and a newer request cuts a running analysis short and drops its
// result instead of showing a spectrum that no longer belongs to the document.
class SpectrumAnalyzer final : public QObject {
    Q_OBJECT

public:
    explicit SpectrumAnalyzer(QObject* parent = nullptr);
    ~SpectrumAnalyzer() override;

    // Analyzes a copy of document, replacing any request not yet delivered.
    void request(const WaveDocument& document, double analysisRate, spectrum::Window window);
    // Drops the pending request and the result of a running one, e.g. once the
    // spectrum is hidden and nobody would look at it.
    void cancel();

signals:
    // Emitted on the GUI thread, for the latest request only; never with
    // Status::Cancelled.
    void finished(const spectrum::Analysis& analysis);

private:
    void start();

    QTimer* delay_ = nullptr;
    WaveDocument document_;
    double analysisRate_     = 0.0;
    spectrum::Window window_ = spectrum::Window::Hann;
    // Counts requests. A worker compares its own number against it to learn that
    // it has been overtaken; shared so it outlives this object for a worker that
    // is still running when the window closes.
    std::shared_ptr<std::atomic<quint64>> generation_;
};

}  // namespace zwe
