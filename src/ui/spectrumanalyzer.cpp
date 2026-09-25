// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "spectrumanalyzer.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QThreadPool>
#include <QTimer>
#include <utility>

namespace zwe {

namespace {

// Long enough that a drag, which requests on every mouse move, only starts an
// analysis once it pauses; short enough that the spectrum still seems to follow.
constexpr int RequestDelayMs = 120;

}  // namespace

SpectrumAnalyzer::SpectrumAnalyzer(QObject* parent)
    : QObject(parent), generation_(std::make_shared<std::atomic<quint64>>(0)) {
    delay_ = new QTimer(this);
    delay_->setSingleShot(true);
    delay_->setInterval(RequestDelayMs);
    connect(delay_, &QTimer::timeout, this, &SpectrumAnalyzer::start);
}

SpectrumAnalyzer::~SpectrumAnalyzer() {
    // A worker still running holds its own copy of the document and of
    // generation_; moving the count on makes it stop at its next check and keeps
    // it from delivering to this object once it is gone.
    ++*generation_;
}

void SpectrumAnalyzer::request(
    const WaveDocument& document, double analysisRate, spectrum::Window window
) {
    document_     = document;
    analysisRate_ = analysisRate;
    window_       = window;
    // A running analysis is overtaken now, not only once the delay is over: it
    // would otherwise keep a core busy with a document that is already stale.
    ++*generation_;
    delay_->start();
}

void SpectrumAnalyzer::cancel() {
    delay_->stop();
    ++*generation_;
}

void SpectrumAnalyzer::start() {
    const quint64 generation = ++*generation_;
    const auto counter       = generation_;
    // Captured by value: the worker must not touch this object, which may be
    // gone before it finishes. It only reaches back through the event loop of
    // the GUI thread, where the destructor cannot run at the same time.
    QThreadPool::globalInstance()->start([this,
                                          counter,
                                          generation,
                                          document = document_,
                                          rate     = analysisRate_,
                                          window   = window_] {
        const auto cancelled = [counter, generation] { return counter->load() != generation; };
        spectrum::Analysis analysis = spectrum::analyze(
            document, rate, window, spectrum::MaximumAnalysisCount, cancelled
        );
        if (cancelled()) {
            return;
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [this, counter, generation, analysis = std::move(analysis)] {
                // Checked again on the GUI thread: a request, a cancel or the
                // destructor may have come in while the result was queued.
                if (counter->load() == generation) {
                    emit finished(analysis);
                }
            },
            Qt::QueuedConnection
        );
    });
}

}  // namespace zwe
