// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <complex>
#include <cstddef>
#include <functional>
#include <vector>

#include "wavedocument.h"

namespace zwe::spectrum {

// The window applied to the analysis samples before the transform. Every
// amplitude is corrected by the window's coherent gain, so a sine of amplitude A
// reads A at its bin whichever window is chosen; the windows differ in how far a
// frequency between two bins leaks into its neighbors.
enum class Window {
    Rectangular,  // no window: exact for a signal periodic in the analyzed stretch
    Hann,
    FlatTop,        // the most accurate amplitude, the widest peaks
    BlackmanHarris  // 4-term, the lowest leakage
};

// Unnormalized forward DFT, X[k] = sum_n x[n] exp(-2 pi i k n / N), in place and
// for any N: radix-2 for a power of two, Bluestein's algorithm otherwise. N of 0
// or 1 leaves data as it is.
void fft(std::vector<std::complex<double>>& data);

// The weights of window for count samples, in the periodic (DFT-even) form.
std::vector<double> windowWeights(Window window, size_t count);

// Single-sided amplitude spectrum: amplitude[k] is the peak amplitude of the
// component at k * binWidth, k in [0, count / 2]; amplitude[0] is the magnitude
// of the mean. Empty for fewer than two samples.
//
// The mean is taken out of the samples before the window is applied. A window
// would otherwise spread a DC offset over the lowest bins of its main lobe - a
// flat top turns 1 V of DC into almost 2 V at bin 1 - and those would pass for
// signal content, the fundamental included.
struct Spectrum {
    double binWidth = 0.0;  // rate / count
    std::vector<double> amplitude;

    bool operator==(const Spectrum&) const = default;
};

Spectrum amplitudeSpectrum(const std::vector<double>& samples, double rate, Window window);

// Figures of the analyzed signal. A figure that is undefined for it - the crest
// factor of an all-zero signal, the THD without a fundamental - is NaN.
struct Statistics {
    size_t count       = 0;    // analysis samples
    double rate        = 0.0;  // analysis rate, 1/s
    double duration    = 0.0;  // count / rate, s

    double mean        = 0.0;  // DC
    double rms         = 0.0;
    double acRms       = 0.0;  // RMS without the DC, i.e. the standard deviation
    double minimum     = 0.0;
    double maximum     = 0.0;
    double peak        = 0.0;  // max |x|
    double peakToPeak  = 0.0;
    double crestFactor = 0.0;  // peak / rms
    double formFactor  = 0.0;  // rms / mean(|x|)
    // The largest jump between two consecutive output values, and that jump per
    // value period: the slew rate the output has to deliver. Both are measured
    // between consecutive analysis samples, so below the value rate a jump can
    // span several output values.
    double maxStep              = 0.0;
    double maxSlewRate          = 0.0;  // 1/s * value unit

    double binWidth             = 0.0;
    double nyquist              = 0.0;  // rate / 2
    double fundamentalFrequency = 0.0;  // the strongest component above DC
    double fundamentalAmplitude = 0.0;
    // Ratio, harmonics 2 .. 10 below Nyquist; NaN as well when not even the
    // second harmonic lies below Nyquist.
    double thd = 0.0;
    // The share of the AC power in the top tenth of the band below Nyquist: a
    // large one says the analysis rate is too low for the signal.
    double nyquistPowerFraction = 0.0;
};

// The time-domain figures of samples taken at rate. valueRate is the rate of
// the output values those samples hold, which is what maxSlewRate refers to.
Statistics timeStatistics(const std::vector<double>& samples, double rate, double valueRate);

// Fills in the spectral figures of statistics from the spectrum of the same
// samples.
void addSpectralStatistics(Statistics& statistics, const Spectrum& spectrum);

// The number of samples sampleHeldOutput() takes: llround(documentDuration *
// analysisRate), 0 for an empty document or a rate <= 0.
size_t heldOutputCount(const WaveDocument& document, double analysisRate);

// The document's output as a device plays it - value k of
// sampling::sampleDocument() held from k / sampleRate until the next one -
// sampled at analysisRate: sample j is the output value at j / analysisRate.
// Returns an empty vector as soon as cancelled() returns true.
std::vector<double> sampleHeldOutput(
    const WaveDocument& document, double analysisRate, const std::function<bool()>& cancelled = {}
);

// Beyond this many analysis samples a transform takes too long and too much
// memory to follow edits live.
constexpr size_t MaximumAnalysisCount = size_t{1} << 22;

struct Analysis {
    enum class Status {
        Ok,
        Empty,     // no document content, or an analysis rate <= 0
        TooLong,   // requestedCount > the maximum count; nothing was computed
        Cancelled  // cancelled() returned true
    };
    Status status         = Status::Empty;
    size_t requestedCount = 0;  // heldOutputCount(document, analysisRate)
    Spectrum spectrum;
    Statistics statistics;
};

// The whole pipeline: samples the held output, computes its spectrum and all of
// its statistics. Safe to call from a worker thread with a copy of the
// document; checks cancelled() between its stages and inside the long ones.
Analysis analyze(
    const WaveDocument& document,
    double analysisRate,
    Window window,
    size_t maximumCount                    = MaximumAnalysisCount,
    const std::function<bool()>& cancelled = {}
);

}  // namespace zwe::spectrum
