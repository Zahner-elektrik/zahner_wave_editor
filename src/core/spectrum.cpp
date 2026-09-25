// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "spectrum.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>

#include "sampling.h"

namespace zwe::spectrum {

namespace {

using Complex        = std::complex<double>;
using Cancel         = std::function<bool()>;

constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

// The chunk length of sampling::sampleDocumentWindow(). Output values computed
// in chunks of exactly this size, each starting on a multiple of it, see the
// very same sample times as sampling::sampleDocument() and therefore come out
// bit for bit identical to the export.
constexpr size_t ValueChunk = size_t{64} * 1024;

// How many analysis samples pass between two looks at cancelled() where no
// natural chunk boundary offers itself.
constexpr size_t CancelInterval = size_t{4} * 1024;

bool isCancelled(const Cancel& cancelled) {
    return cancelled && cancelled();
}

// std::complex's operator* has to care for infinities and NaN, which on some
// compilers means a library call per product. The transforms only ever see
// finite values, so the plain formula is all they need.
Complex multiply(Complex a, Complex b) {
    return {a.real() * b.real() - a.imag() * b.imag(), a.real() * b.imag() + a.imag() * b.real()};
}

// Neumaier's variant of Kahan summation. Mean and RMS run over millions of
// samples, and a naive sum of that many loses digits the figures then miss.
class CompensatedSum {
public:
    void add(double value) {
        const double total = sum_ + value;
        if (std::abs(sum_) >= std::abs(value)) {
            compensation_ += (sum_ - total) + value;
        } else {
            compensation_ += (value - total) + sum_;
        }
        sum_ = total;
    }

    double value() const { return sum_ + compensation_; }

private:
    double sum_          = 0.0;
    double compensation_ = 0.0;
};

double meanOf(const std::vector<double>& samples) {
    CompensatedSum sum;
    for (const double sample : samples) {
        sum.add(sample);
    }
    return sum.value() / static_cast<double>(samples.size());
}

// ---- transforms ----

bool isPowerOfTwo(size_t n) {
    return n != 0 && (n & (n - 1)) == 0;
}

size_t nextPowerOfTwo(size_t n) {
    size_t power = 1;
    while (power < n) {
        power <<= 1;
    }
    return power;
}

// The butterflies of the stages up to this span stay within aligned blocks of
// it. Running all of those stages block by block, while a block sits in the
// cache, instead of each of them over the whole array saves most of the trips
// through memory a long transform otherwise spends its time on. 8192 complex
// values are 128 KiB.
constexpr size_t CacheBlock = size_t{8} * 1024;

// An iterative radix-2 transform of one fixed power-of-two length. The
// twiddle factors are kept so Bluestein's algorithm can run its three
// transforms of the same length on a single table. Every twiddle comes from
// std::polar() or from an exact symmetry of one that did: the usual rotation
// recurrence accumulates a rounding error that grows with the length, which
// the table does not.
class Radix2 {
public:
    explicit Radix2(size_t length) : length_(length), twiddles_(length / 2) {
        const auto direct = [&](size_t k) {
            return std::polar(
                1.0, -2.0 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(length)
            );
        };
        if (length < 8) {
            for (size_t k = 0; k < twiddles_.size(); ++k) {
                twiddles_[k] = direct(k);
            }
            return;
        }
        // Only the first eighth of the circle needs sine and cosine. Its
        // mirror image about 45 degrees swaps them, and the second quarter is
        // the first one turned by -90 degrees; both are exact in floating
        // point, and cheaper than the trigonometry for millions of twiddles.
        const size_t eighth  = length / 8;
        const size_t quarter = length / 4;
        for (size_t k = 0; k <= eighth; ++k) {
            twiddles_[k] = direct(k);
        }
        for (size_t k = eighth + 1; k <= quarter; ++k) {
            const Complex mirrored = twiddles_[quarter - k];
            twiddles_[k]           = {-mirrored.imag(), -mirrored.real()};
        }
        for (size_t k = quarter + 1; k < twiddles_.size(); ++k) {
            const Complex turned = twiddles_[k - quarter];
            twiddles_[k]         = {turned.imag(), -turned.real()};
        }
    }

    // Transforms data (of the plan's length) in place. Returns false as soon
    // as cancelled() does; data is garbage then.
    bool forward(std::vector<Complex>& data, const Cancel& cancelled) const {
        const size_t n = length_;
        for (size_t i = 1, j = 0; i < n; ++i) {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) {
                j ^= bit;
            }
            j ^= bit;
            if (i < j) {
                std::swap(data[i], data[j]);
            }
        }
        const size_t block = std::min(n, CacheBlock);
        for (size_t first = 0; first < n; first += block) {
            for (size_t span = 2; span <= block; span <<= 1) {
                stage(data, span, first, first + block);
            }
        }
        if (isCancelled(cancelled)) {
            return false;
        }
        for (size_t span = 2 * block; span <= n; span <<= 1) {
            stage(data, span, 0, n);
            if (isCancelled(cancelled)) {
                return false;
            }
        }
        return true;
    }

    // The unnormalized inverse, sum_k X[k] exp(+2 pi i k n / N), through the
    // forward transform of the conjugate.
    bool inverse(std::vector<Complex>& data, const Cancel& cancelled) const {
        for (Complex& value : data) {
            value = std::conj(value);
        }
        if (! forward(data, cancelled)) {
            return false;
        }
        for (Complex& value : data) {
            value = std::conj(value);
        }
        return true;
    }

private:
    // The butterflies of one stage, combining pairs of transforms of span / 2
    // into transforms of span, over data[from, to).
    void stage(std::vector<Complex>& data, size_t span, size_t from, size_t to) const {
        const size_t half   = span / 2;
        const size_t stride = length_ / span;
        for (size_t start = from; start < to; start += span) {
            for (size_t j = 0; j < half; ++j) {
                const Complex even     = data[start + j];
                const Complex odd      = multiply(data[start + j + half], twiddles_[j * stride]);
                data[start + j]        = even + odd;
                data[start + j + half] = even - odd;
            }
        }
    }

    size_t length_;
    std::vector<Complex> twiddles_;
};

// Bluestein's algorithm: with kn = (k^2 + n^2 - (k - n)^2) / 2 the DFT becomes a
// convolution with the chirp exp(-i pi n^2 / N), which runs as a circular
// convolution of a power-of-two length L >= 2N - 1.
bool bluestein(std::vector<Complex>& data, const Cancel& cancelled) {
    const size_t n = data.size();
    const size_t L = nextPowerOfTwo(2 * n - 1);

    // n^2 grows past what a double holds exactly long before n gets large, and
    // pi * n^2 / N with a rounded n^2 is a useless angle. The chirp is periodic
    // in n^2 with period 2N, so the exponent is reduced in integer arithmetic -
    // incrementally, as (n + 1)^2 = n^2 + 2n + 1, which cannot overflow.
    std::vector<Complex> chirp(n);
    const size_t period = 2 * n;
    size_t square       = 0;
    for (size_t i = 0; i < n; ++i) {
        chirp[i] = std::polar(
            1.0, -std::numbers::pi * static_cast<double>(square) / static_cast<double>(n)
        );
        square += 2 * i + 1;
        if (square >= period) {
            square -= period;
        }
    }

    std::vector<Complex> a(L);
    for (size_t i = 0; i < n; ++i) {
        a[i] = multiply(data[i], chirp[i]);
    }
    // The convolution kernel conj(chirp) over the lags -(N - 1) .. N - 1, the
    // negative ones wrapped around to the end.
    std::vector<Complex> b(L);
    b[0] = std::conj(chirp[0]);
    for (size_t i = 1; i < n; ++i) {
        b[i] = b[L - i] = std::conj(chirp[i]);
    }

    const Radix2 plan(L);
    if (! plan.forward(a, cancelled) || ! plan.forward(b, cancelled)) {
        return false;
    }
    for (size_t i = 0; i < L; ++i) {
        a[i] = multiply(a[i], b[i]);
    }
    b = {};  // Frees the memory before the last transform, it is the largest.
    if (! plan.inverse(a, cancelled)) {
        return false;
    }
    const double scale = 1.0 / static_cast<double>(L);
    for (size_t i = 0; i < n; ++i) {
        data[i] = multiply(chirp[i], a[i] * scale);
    }
    return true;
}

bool transform(std::vector<Complex>& data, const Cancel& cancelled) {
    if (data.size() < 2) {
        return true;
    }
    if (isPowerOfTwo(data.size())) {
        return Radix2(data.size()).forward(data, cancelled);
    }
    return bluestein(data, cancelled);
}

// ---- windows ----

// The coefficients a_m of a cosine-sum window, w[n] = sum_m (-1)^m a_m cos(2 pi
// m n / N).
std::vector<double> cosineSumCoefficients(Window window) {
    switch (window) {
        case Window::Rectangular:
            return {1.0};
        case Window::Hann:
            return {0.5, 0.5};
        case Window::FlatTop:
            // The five-term flat top Matlab's flattopwin uses.
            return {0.21557895, 0.41663158, 0.277263158, 0.083578947, 0.006947368};
        case Window::BlackmanHarris:
            return {0.35875, 0.48829, 0.14128, 0.01168};
    }
    return {1.0};  // unreachable
}

// The bins 0 .. N/2 of the DFT of the real values x. For an even N they come
// from a transform of half the length: z[m] = x[2m] + i x[2m + 1] packs the
// even and the odd values into one complex sequence, whose transform Z splits
// into theirs again by its conjugate symmetry. That halves both the time and
// the memory of the largest step of the analysis. Empty if cancelled.
std::vector<Complex> realTransform(const std::vector<double>& x, const Cancel& cancelled) {
    const size_t n = x.size();
    if (n % 2 != 0) {
        std::vector<Complex> data(x.begin(), x.end());
        if (! transform(data, cancelled)) {
            return {};
        }
        data.resize(n / 2 + 1);
        return data;
    }

    const size_t half = n / 2;
    std::vector<Complex> z(half);
    for (size_t m = 0; m < half; ++m) {
        z[m] = {x[2 * m], x[2 * m + 1]};
    }
    if (! transform(z, cancelled)) {
        return {};
    }
    std::vector<Complex> bins(half + 1);
    for (size_t k = 0; k <= half; ++k) {
        const Complex upper = z[k % half];
        const Complex lower = std::conj(z[(half - k) % half]);
        const Complex even  = 0.5 * (upper + lower);
        // (upper - lower) / 2i
        const Complex difference = upper - lower;
        const Complex odd        = {0.5 * difference.imag(), -0.5 * difference.real()};
        const Complex twiddle    = std::polar(
            1.0, -2.0 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(n)
        );
        bins[k] = even + multiply(twiddle, odd);
    }
    return bins;
}

std::optional<Spectrum> spectrumOf(
    const std::vector<double>& samples, double rate, Window window, const Cancel& cancelled
) {
    const size_t n = samples.size();
    if (n < 2) {
        return Spectrum{};
    }
    const double mean           = meanOf(samples);
    std::vector<double> weights = windowWeights(window, n);

    CompensatedSum gain;
    std::vector<double> windowed(n);
    for (size_t i = 0; i < n; ++i) {
        windowed[i] = weights[i] * (samples[i] - mean);
        gain.add(weights[i]);
    }
    weights = {};
    if (isCancelled(cancelled)) {
        return std::nullopt;
    }
    const std::vector<Complex> bins = realTransform(windowed, cancelled);
    if (bins.empty()) {
        return std::nullopt;
    }

    Spectrum spectrum;
    spectrum.binWidth = rate / static_cast<double>(n);
    spectrum.amplitude.resize(n / 2 + 1);
    spectrum.amplitude[0] = std::abs(mean);
    // Bins 1 .. N/2 - 1 stand for a pair of conjugate components and read
    // twice their magnitude; the Nyquist bin of an even N has no partner.
    const double coherentGain = gain.value();
    for (size_t k = 1; k < spectrum.amplitude.size(); ++k) {
        const bool unpaired   = n % 2 == 0 && k == n / 2;
        spectrum.amplitude[k] = std::abs(bins[k]) / coherentGain * (unpaired ? 1.0 : 2.0);
    }
    return spectrum;
}

// ---- held output ----

// Whether numerator / denominator is, up to rounding, a whole number >= 1, and
// which one.
std::optional<size_t> integralRatio(double numerator, double denominator) {
    const double quotient = numerator / denominator;
    const double whole    = std::round(quotient);
    if (! (whole >= 1.0) || whole > 9.0e15) {
        return std::nullopt;
    }
    if (std::abs(quotient - whole) > whole * 1e-12) {
        return std::nullopt;
    }
    return static_cast<size_t>(whole);
}

// Which output value analysis sample j sees: floor(j * valueRate /
// analysisRate), clamped to the values there are.
class HeldIndex {
public:
    HeldIndex(double valueRate, double analysisRate, size_t valueCount)
        : valueRate_(valueRate),
          analysisRate_(analysisRate),
          last_(valueCount - 1),
          upsampling_(integralRatio(analysisRate, valueRate)),
          downsampling_(integralRatio(valueRate, analysisRate)) {}

    size_t operator()(size_t j) const {
        size_t k = 0;
        if (upsampling_) {
            k = j / *upsampling_;
        } else if (downsampling_) {
            k = j * *downsampling_;
        } else {
            // j * valueRate / analysisRate lands on a whole number whenever an
            // analysis sample coincides with a value boundary, and rounding
            // must not drop it one short of it: that would read the value
            // before the boundary. A relative nudge far below any timing a
            // device keeps resolves the boundary to the value that starts there.
            const double position = static_cast<double>(j) * valueRate_ / analysisRate_;
            k                     = static_cast<size_t>(std::floor(position * (1.0 + 1e-12)));
        }
        return std::min(k, last_);
    }

    bool isIdentity() const { return upsampling_ == size_t{1}; }

    std::optional<size_t> downsampling() const { return downsampling_; }

private:
    double valueRate_;
    double analysisRate_;
    size_t last_;
    std::optional<size_t> upsampling_;
    std::optional<size_t> downsampling_;
};

// The time sampling::sampleDocument() evaluates output value k at. Its chunked
// evaluation adds the offset within a chunk to the chunk's start time, which
// is not always the same double as k / rate.
double exportTime(size_t k, double rate) {
    const size_t offset = k % ValueChunk;
    return static_cast<double>(k - offset) / rate + static_cast<double>(offset) / rate;
}

// All count output values, exactly as sampling::sampleDocument() produces
// them; empty if cancelled.
std::vector<double> outputValues(
    const WaveDocument& document, size_t count, const Cancel& cancelled
) {
    std::vector<double> values;
    values.reserve(count);
    for (size_t first = 0; first < count; first += ValueChunk) {
        if (isCancelled(cancelled)) {
            return {};
        }
        const size_t chunk               = std::min(ValueChunk, count - first);
        const std::vector<double> window = sampling::sampleDocumentWindow(
            document, static_cast<double>(first) / document.sampleRate, document.sampleRate, chunk
        );
        values.insert(values.end(), window.begin(), window.end());
    }
    return values;
}

// Picking from all output values costs every one of them, which is the fast
// path as long as most of them are needed. Beyond this many values per
// analysis sample, evaluating only the needed ones is cheaper.
constexpr size_t DenseStride = 4;

}  // namespace

void fft(std::vector<std::complex<double>>& data) {
    transform(data, {});
}

std::vector<double> windowWeights(Window window, size_t count) {
    const std::vector<double> coefficients = cosineSumCoefficients(window);
    std::vector<double> weights(count, 0.0);
    for (size_t n = 0; n < count; ++n) {
        double weight = 0.0;
        double sign   = 1.0;
        for (size_t m = 0; m < coefficients.size(); ++m) {
            // m * n reduced modulo count first, so the angle stays exact for
            // large counts instead of growing into the millions of radians.
            const size_t turn = (m * n) % count;
            const double angle =
                2.0 * std::numbers::pi * static_cast<double>(turn) / static_cast<double>(count);
            weight += sign * coefficients[m] * std::cos(angle);
            sign = -sign;
        }
        weights[n] = weight;
    }
    return weights;
}

Spectrum amplitudeSpectrum(const std::vector<double>& samples, double rate, Window window) {
    return *spectrumOf(samples, rate, window, {});
}

Statistics timeStatistics(const std::vector<double>& samples, double rate, double valueRate) {
    Statistics statistics;
    statistics.count    = samples.size();
    statistics.rate     = rate;
    statistics.duration = rate > 0.0 ? static_cast<double>(samples.size()) / rate : 0.0;

    // The spectral figures are addSpectralStatistics()' business; until it ran
    // they are unknown rather than zero.
    statistics.binWidth             = NaN;
    statistics.nyquist              = rate / 2.0;
    statistics.fundamentalFrequency = NaN;
    statistics.fundamentalAmplitude = NaN;
    statistics.thd                  = NaN;
    statistics.nyquistPowerFraction = NaN;

    if (samples.empty()) {
        statistics.mean = statistics.rms = statistics.acRms = NaN;
        statistics.minimum = statistics.maximum = statistics.peak = statistics.peakToPeak = NaN;
        statistics.crestFactor = statistics.formFactor = NaN;
        statistics.maxStep = statistics.maxSlewRate = NaN;
        return statistics;
    }

    const double count = static_cast<double>(samples.size());
    const double mean  = meanOf(samples);
    CompensatedSum squares;
    CompensatedSum deviations;
    CompensatedSum magnitudes;
    double minimum = samples.front();
    double maximum = samples.front();
    double maxStep = 0.0;
    for (size_t i = 0; i < samples.size(); ++i) {
        const double sample = samples[i];
        squares.add(sample * sample);
        // The AC RMS from the deviations themselves, not as sqrt(rms^2 -
        // mean^2): with a large offset that difference cancels away the very
        // digits it is after.
        deviations.add((sample - mean) * (sample - mean));
        magnitudes.add(std::abs(sample));
        minimum = std::min(minimum, sample);
        maximum = std::max(maximum, sample);
        if (i > 0) {
            maxStep = std::max(maxStep, std::abs(sample - samples[i - 1]));
        }
    }

    statistics.mean            = mean;
    statistics.rms             = std::sqrt(squares.value() / count);
    statistics.acRms           = std::sqrt(deviations.value() / count);
    statistics.minimum         = minimum;
    statistics.maximum         = maximum;
    statistics.peak            = std::max(std::abs(minimum), std::abs(maximum));
    statistics.peakToPeak      = maximum - minimum;

    const double meanMagnitude = magnitudes.value() / count;
    statistics.crestFactor     = statistics.rms > 0.0 ? statistics.peak / statistics.rms : NaN;
    statistics.formFactor      = meanMagnitude > 0.0 ? statistics.rms / meanMagnitude : NaN;

    // A single sample has no neighbor to jump from.
    statistics.maxStep     = samples.size() > 1 ? maxStep : NaN;
    statistics.maxSlewRate = statistics.maxStep * valueRate;
    return statistics;
}

void addSpectralStatistics(Statistics& statistics, const Spectrum& spectrum) {
    const std::vector<double>& amplitude = spectrum.amplitude;
    statistics.binWidth                  = spectrum.binWidth;
    statistics.nyquist                   = statistics.rate / 2.0;
    statistics.fundamentalFrequency      = NaN;
    statistics.fundamentalAmplitude      = NaN;
    statistics.thd                       = NaN;
    statistics.nyquistPowerFraction      = NaN;
    if (amplitude.size() < 2) {
        return;
    }
    const size_t last = amplitude.size() - 1;

    // The power a bin stands for: A^2 / 2 of a sine, except for the unpaired
    // Nyquist bin of an even count, whose alternating +-A carries A^2. Bin
    // last is that one exactly when binWidth * last reaches rate / 2.
    const bool nyquistBin =
        statistics.rate > 0.0 &&
        std::abs(spectrum.binWidth * static_cast<double>(last) - statistics.nyquist) <=
            spectrum.binWidth * 1e-9;
    const auto power = [&](size_t k) {
        const double squared = amplitude[k] * amplitude[k];
        return k == last && nyquistBin ? squared : squared / 2.0;
    };

    // The spectrum carries the mean in bin 0 only - amplitudeSpectrum() takes
    // it out before the window - so every bin above it is AC content.
    size_t peakBin = 1;
    CompensatedSum totalPower;
    CompensatedSum topPower;
    const double topBand = 0.9 * statistics.nyquist;
    for (size_t k = 1; k <= last; ++k) {
        if (amplitude[k] > amplitude[peakBin]) {
            peakBin = k;
        }
        totalPower.add(power(k));
        if (spectrum.binWidth * static_cast<double>(k) >= topBand) {
            topPower.add(power(k));
        }
    }
    if (totalPower.value() > 0.0) {
        statistics.nyquistPowerFraction = topPower.value() / totalPower.value();
    }

    // Rounding leaves a few 1e-16 of the signal's scale in every bin of a
    // constant signal, which must not pass for a fundamental.
    double scale = std::max(amplitude[0], amplitude[peakBin]);
    if (std::isfinite(statistics.peak)) {
        scale = std::max(scale, statistics.peak);
    }
    const double fundamental = amplitude[peakBin];
    if (! (fundamental > scale * 1e-12)) {
        return;
    }

    // A parabola through the peak and its two neighbors places a component
    // that falls between two bins. Bin 0 holds the mean rather than the lower
    // flank of the peak, so a peak at bin 1 is taken as it is.
    double offset = 0.0;
    if (peakBin > 1 && peakBin < last) {
        const double below = amplitude[peakBin - 1];
        const double above = amplitude[peakBin + 1];
        const double curve = below - 2.0 * fundamental + above;
        if (curve < 0.0) {
            offset = std::clamp(0.5 * (below - above) / curve, -0.5, 0.5);
        }
    }
    const double frequency          = (static_cast<double>(peakBin) + offset) * spectrum.binWidth;
    statistics.fundamentalFrequency = frequency;
    statistics.fundamentalAmplitude = fundamental;

    // Each harmonic is looked for within two bins of where it should be: the
    // fundamental's frequency is only known to a fraction of a bin, and that
    // error grows with the harmonic's order. A fundamental within a few bins
    // of DC lets the window's main lobe reach its harmonics' search ranges,
    // which reads a too large THD there.
    CompensatedSum harmonicPower;
    bool anyHarmonic = false;
    for (int order = 2; order <= 10; ++order) {
        const double harmonic = order * frequency;
        if (! (harmonic < statistics.nyquist)) {
            break;
        }
        const double center = std::round(harmonic / spectrum.binWidth);
        const size_t from   = static_cast<size_t>(std::max(1.0, center - 2.0));
        const size_t to     = std::min(last, static_cast<size_t>(center + 2.0));
        double strongest    = 0.0;
        for (size_t k = from; k <= to; ++k) {
            strongest = std::max(strongest, amplitude[k]);
        }
        harmonicPower.add(strongest * strongest);
        anyHarmonic = true;
    }
    if (anyHarmonic) {
        statistics.thd = std::sqrt(harmonicPower.value()) / fundamental;
    }
}

size_t heldOutputCount(const WaveDocument& document, double analysisRate) {
    if (! (analysisRate > 0.0) || sampling::sampleCount(document) == 0) {
        return 0;
    }
    const double count = documentDuration(document) * analysisRate;
    // llround() is undefined beyond the range of long long. Such a count is
    // far beyond any maximum anyway, and saturating keeps it reported as that.
    if (! (count < 9.0e18)) {
        return std::numeric_limits<size_t>::max();
    }
    return static_cast<size_t>(std::llround(count));
}

std::vector<double> sampleHeldOutput(
    const WaveDocument& document, double analysisRate, const std::function<bool()>& cancelled
) {
    const size_t count      = heldOutputCount(document, analysisRate);
    const size_t valueCount = sampling::sampleCount(document);
    if (count == 0 || valueCount == 0 || isCancelled(cancelled)) {
        return {};
    }
    const double valueRate = document.sampleRate;
    const HeldIndex index(valueRate, analysisRate, valueCount);

    // Every value is needed, or most of them: compute them all once, exactly
    // as the export does, and pick.
    if (valueCount <= count * DenseStride) {
        std::vector<double> values = outputValues(document, valueCount, cancelled);
        if (values.empty()) {
            return {};
        }
        if (index.isIdentity() && count == valueCount) {
            return values;
        }
        std::vector<double> samples(count);
        for (size_t j = 0; j < count; ++j) {
            if (j % CancelInterval == 0 && isCancelled(cancelled)) {
                return {};
            }
            samples[j] = values[index(j)];
        }
        return samples;
    }

    std::vector<double> samples(count);
    size_t done = 0;

    // A whole number of values per analysis sample puts the needed values on
    // an evenly spaced grid again, which the segment-major window sampler
    // evaluates much faster than value by value, formulas and point
    // interpolation included.
    if (const std::optional<size_t> stride = index.downsampling()) {
        const size_t reachable = std::min(count, (valueCount - 1) / *stride + 1);
        const double rate      = valueRate / static_cast<double>(*stride);
        for (size_t first = 0; first < reachable; first += ValueChunk) {
            if (isCancelled(cancelled)) {
                return {};
            }
            const size_t chunk               = std::min(ValueChunk, reachable - first);
            const std::vector<double> window = sampling::sampleDocumentWindow(
                document, exportTime(first * *stride, valueRate), rate, chunk
            );
            std::copy(
                window.begin(), window.end(), samples.begin() + static_cast<std::ptrdiff_t>(first)
            );
        }
        done = reachable;
    }

    // Everything else, and the samples past the last value that rounding may
    // leave, value by value.
    for (size_t j = done; j < count; ++j) {
        if (j % CancelInterval == 0 && isCancelled(cancelled)) {
            return {};
        }
        samples[j] = sampling::documentValueAt(document, exportTime(index(j), valueRate));
    }
    return samples;
}

Analysis analyze(
    const WaveDocument& document,
    double analysisRate,
    Window window,
    size_t maximumCount,
    const std::function<bool()>& cancelled
) {
    Analysis analysis;
    analysis.requestedCount = heldOutputCount(document, analysisRate);
    if (analysis.requestedCount == 0) {
        analysis.status = Analysis::Status::Empty;
        return analysis;
    }
    if (analysis.requestedCount > maximumCount) {
        analysis.status = Analysis::Status::TooLong;
        return analysis;
    }

    // Once cancelled stays cancelled, whatever the callback says afterwards:
    // an empty sample vector alone would not tell a cancellation apart.
    bool stopped      = false;
    const Cancel stop = [&] {
        stopped = stopped || isCancelled(cancelled);
        return stopped;
    };
    // A cancelled analysis carries no half-finished figures.
    const auto cancel = [&] {
        Analysis result;
        result.status         = Analysis::Status::Cancelled;
        result.requestedCount = analysis.requestedCount;
        return result;
    };

    const std::vector<double> samples = sampleHeldOutput(document, analysisRate, stop);
    if (stop()) {
        return cancel();
    }
    std::optional<Spectrum> spectrum = spectrumOf(samples, analysisRate, window, stop);
    if (! spectrum || stop()) {
        return cancel();
    }
    analysis.statistics = timeStatistics(samples, analysisRate, document.sampleRate);
    if (stop()) {
        return cancel();
    }
    addSpectralStatistics(analysis.statistics, *spectrum);
    analysis.spectrum = std::move(*spectrum);
    analysis.status   = Analysis::Status::Ok;
    return analysis;
}

}  // namespace zwe::spectrum
