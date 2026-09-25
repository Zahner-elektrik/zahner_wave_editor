// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include <QtTest>
#include <cmath>
#include <complex>
#include <numbers>
#include <random>

#include "core/sampling.h"
#include "core/segment.h"
#include "core/spectrum.h"
#include "core/wavedocument.h"
#include "core/wavelayer.h"

using namespace zwe;
using namespace zwe::spectrum;

Q_DECLARE_METATYPE(zwe::spectrum::Window)

class TestSpectrum : public QObject {
    Q_OBJECT

private slots:
    void fftMatchesNaiveDft_data();
    void fftMatchesNaiveDft();
    void fftLeavesTrivialLengthsAlone();
    void windowWeightsArePeriodic();
    void sineAtABinReadsItsAmplitude_data();
    void sineAtABinReadsItsAmplitude();
    void dcReadsInBinZeroAndLeaksNowhere_data();
    void dcReadsInBinZeroAndLeaksNowhere();
    void flatTopReadsBetweenBinsAccurately();
    void rectangularSpectrumMatchesTheTransform();
    void rectangularSpectrumKeepsParseval();
    void tooFewSamplesGiveAnEmptySpectrum();
    void sineStatistics();
    void squareStatistics();
    void offsetShowsInMeanButNotInAcRms();
    void stepGivesMaxStepAndSlewRate();
    void undefinedFiguresAreNaN();
    void thdOfPureSineIsZero_data();
    void thdOfPureSineIsZero();
    void thdOfSineWithTenPercentThirdHarmonic_data();
    void thdOfSineWithTenPercentThirdHarmonic();
    void fundamentalBetweenBinsIsInterpolated();
    void nyquistPowerFractionFlagsContentNearNyquist();
    void heldOutputCountFollowsTheDocumentDuration();
    void heldOutputAtTheValueRateIsTheExport();
    void heldOutputAtTwiceTheRateRepeatsEveryValue();
    void heldOutputAtHalfTheRateTakesEverySecondValue();
    void heldOutputAtOtherRatesHoldsTheRightValue_data();
    void heldOutputAtOtherRatesHoldsTheRightValue();
    void heldOutputStopsWhenCancelled();
    void analyzeRejectsEmptyAndTooLongRequests();
    void analyzeFindsTheImagesOfTheHeldOutput();
    void analyzeIsCancellableAtEveryCheck();
};

namespace {

using Complex = std::complex<double>;

// The textbook O(N^2) transform, the reference the fast ones are held to. The
// exponent is reduced modulo N so the reference itself stays accurate.
std::vector<Complex> naiveDft(const std::vector<Complex>& input) {
    const size_t n = input.size();
    std::vector<Complex> output(n);
    for (size_t k = 0; k < n; ++k) {
        Complex sum = 0.0;
        for (size_t i = 0; i < n; ++i) {
            const double angle =
                -2.0 * std::numbers::pi * static_cast<double>((k * i) % n) / static_cast<double>(n);
            sum += input[i] * std::polar(1.0, angle);
        }
        output[k] = sum;
    }
    return output;
}

// offset + amplitude * sin(2 pi bin n / count + phase): a sine that completes
// bin periods (a fractional number is allowed) in count samples.
std::vector<double> sineSamples(
    size_t count, double bin, double amplitude, double phase = 0.0, double offset = 0.0
) {
    std::vector<double> samples(count);
    for (size_t n = 0; n < count; ++n) {
        const double angle =
            2.0 * std::numbers::pi * bin * static_cast<double>(n) / static_cast<double>(count);
        samples[n] = offset + amplitude * std::sin(angle + phase);
    }
    return samples;
}

std::vector<double> sum(std::vector<double> a, const std::vector<double>& b) {
    for (size_t i = 0; i < a.size(); ++i) {
        a[i] += b[i];
    }
    return a;
}

Statistics statisticsOf(const std::vector<double>& samples, double rate, Window window) {
    Statistics statistics = timeStatistics(samples, rate, rate);
    addSpectralStatistics(statistics, amplitudeSpectrum(samples, rate, window));
    return statistics;
}

void addWindowRows() {
    QTest::addColumn<Window>("window");
    QTest::newRow("rectangular") << Window::Rectangular;
    QTest::newRow("hann") << Window::Hann;
    QTest::newRow("flat top") << Window::FlatTop;
    QTest::newRow("blackman-harris") << Window::BlackmanHarris;
}

Segment sineOf(double amplitude, double frequency, double duration) {
    Segment segment  = defaultSegment(SegmentType::Sine);
    segment.duration = duration;
    auto& params     = std::get<SineParams>(segment.params);
    params.amplitude = amplitude;
    params.frequency = frequency;
    params.phaseDeg  = 0.0;
    params.offset    = 0.0;
    return segment;
}

Segment rampOf(double startValue, double endValue, double duration) {
    Segment segment   = defaultSegment(SegmentType::Ramp);
    segment.duration  = duration;
    auto& params      = std::get<RampParams>(segment.params);
    params.startValue = startValue;
    params.endValue   = endValue;
    return segment;
}

WaveDocument documentOf(std::vector<Segment> segments, double sampleRate) {
    WaveDocument document;
    document.sampleRate = sampleRate;
    WaveLayer layer;
    layer.segments  = std::move(segments);
    document.layers = {layer};
    return document;
}

// 150 000 values at 100 kHz: more than two of the sampler's 64 Ki chunks, a
// sine and a ramp so that every value differs from its neighbors, and a
// segment boundary that falls on a value.
WaveDocument longDocument() {
    return documentOf({sineOf(1.0, 37.0, 0.8), rampOf(-1.0, 2.0, 0.7)}, 100000.0);
}

}  // namespace

void TestSpectrum::fftMatchesNaiveDft_data() {
    QTest::addColumn<int>("length");
    for (const int length : {2, 3, 4, 5, 6, 8, 16, 97, 1000, 997, 1024, 3000, 4096}) {
        QTest::newRow(qPrintable(QString::number(length))) << length;
    }
}

void TestSpectrum::fftMatchesNaiveDft() {
    QFETCH(int, length);
    std::mt19937 generator(static_cast<unsigned>(length));
    std::uniform_real_distribution<double> distribution(-1.0, 1.0);
    std::vector<Complex> data(static_cast<size_t>(length));
    for (Complex& value : data) {
        value = {distribution(generator), distribution(generator)};
    }

    const std::vector<Complex> expected = naiveDft(data);
    fft(data);

    double error     = 0.0;
    double magnitude = 0.0;
    for (size_t k = 0; k < data.size(); ++k) {
        error     = std::max(error, std::abs(data[k] - expected[k]));
        magnitude = std::max(magnitude, std::abs(expected[k]));
    }
    QVERIFY2(error / magnitude < 1e-9, qPrintable(QString::number(error / magnitude)));
}

void TestSpectrum::fftLeavesTrivialLengthsAlone() {
    std::vector<Complex> empty;
    fft(empty);
    QVERIFY(empty.empty());

    std::vector<Complex> single = {Complex(0.25, -3.0)};
    fft(single);
    QCOMPARE(single.size(), size_t{1});
    QCOMPARE(single[0], Complex(0.25, -3.0));
}

void TestSpectrum::windowWeightsArePeriodic() {
    QVERIFY(windowWeights(Window::Hann, 0).empty());

    const std::vector<double> rectangular = windowWeights(Window::Rectangular, 5);
    QCOMPARE(rectangular, std::vector<double>(5, 1.0));

    // The periodic form: w[0] is the lone minimum, w[N/2] the peak, and w[n]
    // mirrors w[N - n] - one period of the window, not a symmetric window of
    // N points.
    const size_t count = 8;
    for (const Window window : {Window::Hann, Window::FlatTop, Window::BlackmanHarris}) {
        const std::vector<double> weights = windowWeights(window, count);
        QCOMPARE(weights.size(), count);
        for (size_t n = 1; n < count; ++n) {
            QVERIFY(qAbs(weights[n] - weights[count - n]) < 1e-15);
            QVERIFY(weights[n] <= weights[count / 2]);
        }
    }
    const std::vector<double> hann = windowWeights(Window::Hann, count);
    QVERIFY(qAbs(hann[0]) < 1e-15);
    QVERIFY(qAbs(hann[2] - 0.5) < 1e-15);
    QVERIFY(qAbs(hann[4] - 1.0) < 1e-15);
}

void TestSpectrum::sineAtABinReadsItsAmplitude_data() {
    addWindowRows();
}

void TestSpectrum::sineAtABinReadsItsAmplitude() {
    QFETCH(Window, window);
    // A power of two and a length Bluestein's algorithm has to handle.
    for (const size_t count : {size_t{1024}, size_t{1000}}) {
        const double rate               = 2000.0;
        const std::vector<double> input = sineSamples(count, 37.0, 1.7, 0.5);
        const Spectrum spectrum         = amplitudeSpectrum(input, rate, window);

        QCOMPARE(spectrum.amplitude.size(), count / 2 + 1);
        QCOMPARE(spectrum.binWidth, rate / static_cast<double>(count));
        QVERIFY2(
            qAbs(spectrum.amplitude[37] - 1.7) < 1e-9,
            qPrintable(QString::number(spectrum.amplitude[37], 'g', 17))
        );
        // The windows spread the sine over their main lobe, but nowhere else.
        for (size_t k = 0; k < spectrum.amplitude.size(); ++k) {
            if (k < 32 || k > 42 || (window == Window::Rectangular && k != 37)) {
                QVERIFY2(spectrum.amplitude[k] < 1e-9, qPrintable(QString::number(k)));
            }
        }
    }

    // The unpaired Nyquist bin of an even count: +-A reads A, not 2A.
    std::vector<double> alternating(64);
    for (size_t n = 0; n < alternating.size(); ++n) {
        alternating[n] = n % 2 == 0 ? 0.6 : -0.6;
    }
    const Spectrum nyquist = amplitudeSpectrum(alternating, 64.0, window);
    QVERIFY(qAbs(nyquist.amplitude[32] - 0.6) < 1e-9);
}

void TestSpectrum::dcReadsInBinZeroAndLeaksNowhere_data() {
    addWindowRows();
}

void TestSpectrum::dcReadsInBinZeroAndLeaksNowhere() {
    QFETCH(Window, window);
    const std::vector<double> dc = std::vector<double>(1000, -0.8);
    const Spectrum constant      = amplitudeSpectrum(dc, 1000.0, window);
    QVERIFY(qAbs(constant.amplitude[0] - 0.8) < 1e-12);
    for (size_t k = 1; k < constant.amplitude.size(); ++k) {
        QVERIFY(constant.amplitude[k] < 1e-12);
    }
    // No fundamental in a constant.
    QVERIFY(std::isnan(statisticsOf(dc, 1000.0, window).fundamentalFrequency));

    // A large offset under a small sine: the window's main lobe must not turn
    // the offset into low-frequency content that outweighs the sine.
    const std::vector<double> offsetSine = sineSamples(1000, 20.0, 0.3, 0.0, -0.8);
    const Spectrum spectrum              = amplitudeSpectrum(offsetSine, 1000.0, window);
    QVERIFY(qAbs(spectrum.amplitude[0] - 0.8) < 1e-12);
    QVERIFY(qAbs(spectrum.amplitude[20] - 0.3) < 1e-9);
    for (size_t k = 1; k < 10; ++k) {
        QVERIFY2(spectrum.amplitude[k] < 1e-9, qPrintable(QString::number(k)));
    }
    const Statistics statistics = statisticsOf(offsetSine, 1000.0, window);
    QVERIFY(qAbs(statistics.fundamentalFrequency - 20.0) < 1e-9);
    QVERIFY(qAbs(statistics.fundamentalAmplitude - 0.3) < 1e-9);
}

void TestSpectrum::flatTopReadsBetweenBinsAccurately() {
    // Halfway between two bins is where a window loses the most amplitude. The
    // flat top loses about a hundredth of a dB, 0.11 %.
    const std::vector<double> input = sineSamples(1024, 40.5, 1.0);
    const Spectrum flatTop          = amplitudeSpectrum(input, 1024.0, Window::FlatTop);
    const double flatTopPeak =
        *std::max_element(flatTop.amplitude.begin(), flatTop.amplitude.end());
    QVERIFY2(qAbs(flatTopPeak - 1.0) < 1.5e-3, qPrintable(QString::number(flatTopPeak, 'g', 10)));

    // The rectangular window, for contrast, loses a third there.
    const Spectrum rectangular = amplitudeSpectrum(input, 1024.0, Window::Rectangular);
    const double rectangularPeak =
        *std::max_element(rectangular.amplitude.begin() + 1, rectangular.amplitude.end());
    QVERIFY(rectangularPeak < 0.7);
}

void TestSpectrum::rectangularSpectrumMatchesTheTransform() {
    // Even counts take the half-length real transform, odd ones the full one.
    std::mt19937 generator(3);
    std::uniform_real_distribution<double> distribution(-1.0, 4.0);
    for (const size_t count :
         {size_t{2}, size_t{3}, size_t{6}, size_t{999}, size_t{1000}, size_t{1024}}) {
        std::vector<double> samples(count);
        double mean = 0.0;
        for (double& sample : samples) {
            sample = distribution(generator);
            mean += sample;
        }
        mean /= static_cast<double>(count);
        std::vector<Complex> centered(count);
        for (size_t i = 0; i < count; ++i) {
            centered[i] = samples[i] - mean;
        }
        const std::vector<Complex> reference = naiveDft(centered);

        const Spectrum spectrum              = amplitudeSpectrum(samples, 1.0, Window::Rectangular);
        QCOMPARE(spectrum.amplitude.size(), count / 2 + 1);
        QVERIFY(qAbs(spectrum.amplitude[0] - std::abs(mean)) < 1e-12);
        for (size_t k = 1; k < spectrum.amplitude.size(); ++k) {
            const bool unpaired = count % 2 == 0 && k == count / 2;
            const double expected =
                std::abs(reference[k]) / static_cast<double>(count) * (unpaired ? 1.0 : 2.0);
            QVERIFY2(
                qAbs(spectrum.amplitude[k] - expected) < 1e-12,
                qPrintable(QStringLiteral("N = %1, k = %2").arg(count).arg(k))
            );
        }
    }
}

void TestSpectrum::rectangularSpectrumKeepsParseval() {
    std::mt19937 generator(7);
    std::uniform_real_distribution<double> distribution(-2.0, 3.0);
    for (const size_t count : {size_t{1024}, size_t{1000}, size_t{999}}) {
        std::vector<double> samples(count);
        double power = 0.0;
        for (double& sample : samples) {
            sample = distribution(generator);
            power += sample * sample;
        }
        power /= static_cast<double>(count);

        const Spectrum spectrum = amplitudeSpectrum(samples, 1.0, Window::Rectangular);
        double spectral         = spectrum.amplitude[0] * spectrum.amplitude[0];
        for (size_t k = 1; k < spectrum.amplitude.size(); ++k) {
            const double squared = spectrum.amplitude[k] * spectrum.amplitude[k];
            const bool unpaired  = count % 2 == 0 && k == count / 2;
            spectral += unpaired ? squared : squared / 2.0;
        }
        QVERIFY2(
            qAbs(spectral - power) < power * 1e-12,
            qPrintable(QStringLiteral("%1 vs %2").arg(spectral, 0, 'g', 17).arg(power, 0, 'g', 17))
        );
    }
}

void TestSpectrum::tooFewSamplesGiveAnEmptySpectrum() {
    QCOMPARE(amplitudeSpectrum({}, 10.0, Window::Hann), Spectrum{});
    QCOMPARE(amplitudeSpectrum({1.0}, 10.0, Window::Hann), Spectrum{});
    QCOMPARE(amplitudeSpectrum({1.0, 3.0}, 10.0, Window::Rectangular).amplitude.size(), size_t{2});
}

void TestSpectrum::sineStatistics() {
    // 100 samples per period put a sample on every crest.
    const std::vector<double> samples = sineSamples(1000, 10.0, 2.0);
    const Statistics statistics       = timeStatistics(samples, 1000.0, 1000.0);

    QCOMPARE(statistics.count, size_t{1000});
    QCOMPARE(statistics.rate, 1000.0);
    QCOMPARE(statistics.duration, 1.0);
    QVERIFY(qAbs(statistics.mean) < 1e-12);
    QVERIFY(qAbs(statistics.rms - std::numbers::sqrt2) < 1e-12);
    QVERIFY(qAbs(statistics.acRms - std::numbers::sqrt2) < 1e-12);
    QVERIFY(qAbs(statistics.minimum + 2.0) < 1e-12);
    QVERIFY(qAbs(statistics.maximum - 2.0) < 1e-12);
    QVERIFY(qAbs(statistics.peak - 2.0) < 1e-12);
    QVERIFY(qAbs(statistics.peakToPeak - 4.0) < 1e-12);
    QVERIFY(qAbs(statistics.crestFactor - std::numbers::sqrt2) < 1e-12);
    // pi / (2 sqrt 2) of the continuous sine, which 100 samples per period
    // approximate closely.
    QVERIFY(qAbs(statistics.formFactor - std::numbers::pi / (2.0 * std::numbers::sqrt2)) < 1e-3);
}

void TestSpectrum::squareStatistics() {
    std::vector<double> samples(1000);
    for (size_t n = 0; n < samples.size(); ++n) {
        samples[n] = (n / 50) % 2 == 0 ? 1.5 : -1.5;
    }
    const Statistics statistics = timeStatistics(samples, 1000.0, 1000.0);
    QCOMPARE(statistics.rms, 1.5);
    QCOMPARE(statistics.crestFactor, 1.0);
    QCOMPARE(statistics.formFactor, 1.0);
    QCOMPARE(statistics.maxStep, 3.0);
}

void TestSpectrum::offsetShowsInMeanButNotInAcRms() {
    const std::vector<double> samples = sineSamples(1000, 10.0, 2.0, 0.3, 0.5);
    const Statistics statistics       = timeStatistics(samples, 1000.0, 1000.0);
    QVERIFY(qAbs(statistics.mean - 0.5) < 1e-12);
    QVERIFY(qAbs(statistics.acRms - std::numbers::sqrt2) < 1e-12);
    QVERIFY(qAbs(statistics.rms - std::sqrt(0.25 + 2.0)) < 1e-12);

    // A large offset must not swallow the AC RMS.
    const std::vector<double> tiny = sineSamples(4096, 16.0, 1e-6, 0.0, 1000.0);
    const Statistics small         = timeStatistics(tiny, 1.0, 1.0);
    QVERIFY(qAbs(small.acRms / (1e-6 / std::numbers::sqrt2) - 1.0) < 1e-6);
}

void TestSpectrum::stepGivesMaxStepAndSlewRate() {
    const std::vector<double> samples = {0.0, 0.0, 1.0, 1.0, 1.0, -0.5, -0.4};
    const Statistics statistics       = timeStatistics(samples, 2000.0, 1000.0);
    QCOMPARE(statistics.maxStep, 1.5);
    // Per value period, not per analysis sample.
    QCOMPARE(statistics.maxSlewRate, 1500.0);
    QCOMPARE(statistics.minimum, -0.5);
    QCOMPARE(statistics.maximum, 1.0);
    QCOMPARE(statistics.peakToPeak, 1.5);
}

void TestSpectrum::undefinedFiguresAreNaN() {
    const Statistics zeros = statisticsOf(std::vector<double>(100, 0.0), 100.0, Window::Hann);
    QCOMPARE(zeros.rms, 0.0);
    QVERIFY(std::isnan(zeros.crestFactor));
    QVERIFY(std::isnan(zeros.formFactor));
    QVERIFY(std::isnan(zeros.fundamentalFrequency));
    QVERIFY(std::isnan(zeros.fundamentalAmplitude));
    QVERIFY(std::isnan(zeros.thd));
    QVERIFY(std::isnan(zeros.nyquistPowerFraction));

    const Statistics empty = statisticsOf({}, 100.0, Window::Hann);
    QCOMPARE(empty.count, size_t{0});
    QVERIFY(std::isnan(empty.mean));
    QVERIFY(std::isnan(empty.rms));
    QVERIFY(std::isnan(empty.maxStep));
    QVERIFY(std::isnan(empty.fundamentalFrequency));

    // A fundamental so high that its second harmonic is past Nyquist.
    const Statistics high = statisticsOf(sineSamples(1000, 300.0, 1.0), 1000.0, Window::Hann);
    QVERIFY(qAbs(high.fundamentalFrequency - 300.0) < 1e-9);
    QVERIFY(std::isnan(high.thd));
}

void TestSpectrum::thdOfPureSineIsZero_data() {
    addWindowRows();
}

void TestSpectrum::thdOfPureSineIsZero() {
    QFETCH(Window, window);
    const Statistics statistics = statisticsOf(sineSamples(2000, 50.0, 1.0), 2000.0, window);
    QVERIFY(qAbs(statistics.fundamentalFrequency - 50.0) < 1e-9);
    QVERIFY(qAbs(statistics.fundamentalAmplitude - 1.0) < 1e-9);
    QVERIFY2(statistics.thd < 1e-9, qPrintable(QString::number(statistics.thd)));
    QCOMPARE(statistics.binWidth, 1.0);
    QCOMPARE(statistics.nyquist, 1000.0);
}

void TestSpectrum::thdOfSineWithTenPercentThirdHarmonic_data() {
    addWindowRows();
}

void TestSpectrum::thdOfSineWithTenPercentThirdHarmonic() {
    QFETCH(Window, window);
    const std::vector<double> samples =
        sum(sineSamples(2000, 50.0, 1.0), sineSamples(2000, 150.0, 0.1, 1.0));
    const Statistics statistics = statisticsOf(samples, 2000.0, window);
    QVERIFY(qAbs(statistics.fundamentalFrequency - 50.0) < 1e-9);
    QVERIFY2(
        qAbs(statistics.thd - 0.1) < 1e-9, qPrintable(QString::number(statistics.thd, 'g', 12))
    );

    // Off the bin grid only the flat top keeps both amplitudes, and with them
    // their ratio, accurate.
    if (window == Window::FlatTop) {
        const std::vector<double> between =
            sum(sineSamples(2000, 50.3, 1.0), sineSamples(2000, 150.9, 0.1, 1.0));
        const Statistics offGrid = statisticsOf(between, 2000.0, window);
        QVERIFY2(qAbs(offGrid.thd - 0.1) < 1e-3, qPrintable(QString::number(offGrid.thd, 'g', 12)));
    }
}

void TestSpectrum::fundamentalBetweenBinsIsInterpolated() {
    for (const Window window : {Window::Hann, Window::FlatTop, Window::BlackmanHarris}) {
        // The parabola fits the rounded main lobes well, the flat top's flat
        // one less so - it is the window for amplitudes, not for frequencies.
        const double tolerance = window == Window::FlatTop ? 0.2 : 0.1;
        for (const double bin : {60.25, 60.5, 60.8}) {
            const Statistics statistics = statisticsOf(sineSamples(1000, bin, 1.0), 1000.0, window);
            QVERIFY2(
                qAbs(statistics.fundamentalFrequency - bin) < tolerance,
                qPrintable(QStringLiteral("%1: %2").arg(bin).arg(statistics.fundamentalFrequency))
            );
        }
    }
}

void TestSpectrum::nyquistPowerFractionFlagsContentNearNyquist() {
    const double rate    = 1000.0;
    const Statistics low = statisticsOf(sineSamples(1000, 20.0, 1.0), rate, Window::Rectangular);
    QVERIFY(low.nyquistPowerFraction < 1e-12);

    const Statistics high = statisticsOf(sineSamples(1000, 480.0, 1.0), rate, Window::Rectangular);
    QVERIFY(qAbs(high.nyquistPowerFraction - 1.0) < 1e-12);

    const Statistics both = statisticsOf(
        sum(sineSamples(1000, 20.0, 1.0), sineSamples(1000, 480.0, 1.0)), rate, Window::Rectangular
    );
    QVERIFY(qAbs(both.nyquistPowerFraction - 0.5) < 1e-12);
}

void TestSpectrum::heldOutputCountFollowsTheDocumentDuration() {
    const WaveDocument document = longDocument();
    QCOMPARE(heldOutputCount(document, 100000.0), size_t{150000});
    QCOMPARE(heldOutputCount(document, 1000.0), size_t{1500});
    QCOMPARE(heldOutputCount(document, 100000.0 / 7.5), size_t{20000});
    QCOMPARE(heldOutputCount(document, 0.0), size_t{0});
    QCOMPARE(heldOutputCount(document, -5.0), size_t{0});
    QCOMPARE(heldOutputCount(WaveDocument{}, 1000.0), size_t{0});
    QVERIFY(sampleHeldOutput(WaveDocument{}, 1000.0).empty());
    QVERIFY(sampleHeldOutput(document, 0.0).empty());
}

void TestSpectrum::heldOutputAtTheValueRateIsTheExport() {
    const WaveDocument document = longDocument();
    QCOMPARE(sampleHeldOutput(document, document.sampleRate), sampling::sampleDocument(document));
}

void TestSpectrum::heldOutputAtTwiceTheRateRepeatsEveryValue() {
    const WaveDocument document       = longDocument();
    const std::vector<double> values  = sampling::sampleDocument(document);
    const std::vector<double> samples = sampleHeldOutput(document, 2.0 * document.sampleRate);
    QCOMPARE(samples.size(), 2 * values.size());
    for (size_t k = 0; k < values.size(); ++k) {
        QCOMPARE(samples[2 * k], values[k]);
        QCOMPARE(samples[2 * k + 1], values[k]);
    }
}

void TestSpectrum::heldOutputAtHalfTheRateTakesEverySecondValue() {
    const WaveDocument document       = longDocument();
    const std::vector<double> values  = sampling::sampleDocument(document);
    const std::vector<double> samples = sampleHeldOutput(document, 0.5 * document.sampleRate);
    QCOMPARE(samples.size(), values.size() / 2);
    for (size_t j = 0; j < samples.size(); ++j) {
        QCOMPARE(samples[j], values[2 * j]);
    }
}

void TestSpectrum::heldOutputAtOtherRatesHoldsTheRightValue_data() {
    // The value a sample sees is floor(j * numerator / denominator).
    QTest::addColumn<int>("numerator");
    QTest::addColumn<int>("denominator");
    QTest::newRow("2 / 3 of the value rate") << 3 << 2;
    QTest::newRow("1.5 times the value rate") << 2 << 3;
    QTest::newRow("a tenth of the value rate") << 10 << 1;
    QTest::newRow("1 / 7.5 of the value rate") << 15 << 2;
    QTest::newRow("1 / 1000 of the value rate") << 1000 << 1;
}

void TestSpectrum::heldOutputAtOtherRatesHoldsTheRightValue() {
    QFETCH(int, numerator);
    QFETCH(int, denominator);
    const WaveDocument document      = longDocument();
    const std::vector<double> values = sampling::sampleDocument(document);
    const double rate = document.sampleRate * denominator / static_cast<double>(numerator);
    const std::vector<double> samples = sampleHeldOutput(document, rate);
    QCOMPARE(samples.size(), heldOutputCount(document, rate));
    for (size_t j = 0; j < samples.size(); ++j) {
        const size_t k = std::min(
            j * static_cast<size_t>(numerator) / static_cast<size_t>(denominator), values.size() - 1
        );
        // Off the export's chunk grid the times may differ in the last bit,
        // which the values follow no further than a few ulps.
        QVERIFY2(
            qAbs(samples[j] - values[k]) < 1e-12,
            qPrintable(QStringLiteral("j = %1: %2 vs %3").arg(j).arg(samples[j]).arg(values[k]))
        );
    }
}

void TestSpectrum::heldOutputStopsWhenCancelled() {
    const WaveDocument document = longDocument();
    for (const double rate : {document.sampleRate, 2.0 * document.sampleRate, 10000.0, 1234.5}) {
        QVERIFY(sampleHeldOutput(document, rate, [] { return true; }).empty());
        // Cancelled at the second look, once the work is under way.
        int calls = 0;
        QVERIFY(sampleHeldOutput(document, rate, [&] { return ++calls > 1; }).empty());
    }
}

void TestSpectrum::analyzeRejectsEmptyAndTooLongRequests() {
    const Analysis empty = analyze(WaveDocument{}, 1000.0, Window::Hann);
    QCOMPARE(empty.status, Analysis::Status::Empty);
    QCOMPARE(empty.requestedCount, size_t{0});

    const WaveDocument document = longDocument();
    QCOMPARE(analyze(document, 0.0, Window::Hann).status, Analysis::Status::Empty);
    QCOMPARE(analyze(document, -1.0, Window::Hann).status, Analysis::Status::Empty);

    const Analysis tooLong = analyze(document, document.sampleRate, Window::Hann, 1000);
    QCOMPARE(tooLong.status, Analysis::Status::TooLong);
    QCOMPARE(tooLong.requestedCount, size_t{150000});
    QVERIFY(tooLong.spectrum.amplitude.empty());
    QCOMPARE(tooLong.statistics.count, size_t{0});

    // The default maximum.
    const Analysis huge = analyze(document, 1e9, Window::Hann);
    QCOMPARE(huge.status, Analysis::Status::TooLong);
    QCOMPARE(huge.requestedCount, size_t{1500000000});

    QCOMPARE(
        analyze(document, 1000.0, Window::Hann, MaximumAnalysisCount, [] { return true; }).status,
        Analysis::Status::Cancelled
    );
}

void TestSpectrum::analyzeFindsTheImagesOfTheHeldOutput() {
    // 50 Hz at 1 kHz for a second: exactly 50 periods in 1000 values.
    const WaveDocument document = documentOf({sineOf(1.0, 50.0, 1.0)}, 1000.0);

    const Analysis direct       = analyze(document, 1000.0, Window::Rectangular);
    QCOMPARE(direct.status, Analysis::Status::Ok);
    QCOMPARE(direct.requestedCount, size_t{1000});
    QCOMPARE(direct.statistics.count, size_t{1000});
    QCOMPARE(direct.statistics.rate, 1000.0);
    QCOMPARE(direct.statistics.duration, 1.0);
    QCOMPARE(direct.statistics.binWidth, 1.0);
    QCOMPARE(direct.statistics.nyquist, 500.0);
    QVERIFY(qAbs(direct.statistics.fundamentalFrequency - 50.0) < 1e-9);
    QVERIFY(qAbs(direct.statistics.fundamentalAmplitude - 1.0) < 1e-9);
    QVERIFY(direct.statistics.thd < 1e-9);
    QCOMPARE(direct.statistics.maxSlewRate, direct.statistics.maxStep * 1000.0);

    // Sampled at twice the value rate, each value is seen twice. The hold
    // shapes the spectrum with its sinc: the sine itself reads cos(pi f / 2 R)
    // of its amplitude and its image at R - f the sine of the same angle.
    const Analysis held = analyze(document, 2000.0, Window::Rectangular);
    QCOMPARE(held.status, Analysis::Status::Ok);
    QCOMPARE(held.spectrum.amplitude.size(), size_t{1001});
    QCOMPARE(held.statistics.binWidth, 1.0);
    const double angle = std::numbers::pi * 50.0 / 2000.0;
    QVERIFY(qAbs(held.spectrum.amplitude[50] - std::cos(angle)) < 1e-9);
    QVERIFY(qAbs(held.spectrum.amplitude[950] - std::sin(angle)) < 1e-9);
    QVERIFY(qAbs(held.statistics.fundamentalFrequency - 50.0) < 1e-9);
    // Per value period, although the steps are seen at twice that rate.
    QCOMPARE(held.statistics.maxSlewRate, held.statistics.maxStep * 1000.0);
}

void TestSpectrum::analyzeIsCancellableAtEveryCheck() {
    // 15 000 values: Bluestein's algorithm on a 32 Ki transform, three of them.
    const WaveDocument document =
        documentOf({sineOf(1.0, 37.0, 0.8), rampOf(-1.0, 2.0, 0.7)}, 10000.0);
    const Analysis reference = analyze(document, document.sampleRate, Window::Hann);
    QCOMPARE(reference.status, Analysis::Status::Ok);

    // Whichever check it is that says stop, analyze() stops and reports it.
    int allowed = 0;
    for (; allowed < 1000; ++allowed) {
        int calls = 0;
        const Analysis analysis =
            analyze(document, document.sampleRate, Window::Hann, MaximumAnalysisCount, [&] {
                return ++calls > allowed;
            });
        if (analysis.status == Analysis::Status::Ok) {
            QCOMPARE(analysis.spectrum, reference.spectrum);
            break;
        }
        QCOMPARE(analysis.status, Analysis::Status::Cancelled);
        QCOMPARE(analysis.requestedCount, size_t{15000});
        QVERIFY(analysis.spectrum.amplitude.empty());
    }
    // Sampling, the stages between them and the transforms, the latter once
    // per stage beyond the cache blocks, all offer a chance.
    QVERIFY2(allowed > 8, qPrintable(QString::number(allowed)));
    QVERIFY(allowed < 1000);
}

QTEST_GUILESS_MAIN(TestSpectrum)
#include "tst_spectrum.moc"
