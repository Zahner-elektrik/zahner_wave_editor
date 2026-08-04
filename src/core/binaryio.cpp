// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "binaryio.h"

#include <QSaveFile>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "sampling.h"

namespace zwe::binaryio {

namespace {

static_assert(sizeof(double) == 8, "the wave binary format is IEEE-754 binary64");
static_assert(std::numeric_limits<double>::is_iec559, "the wave binary format is IEEE-754 binary64");

// Samples buffered per write() call. 65536 values are 512 KiB, which keeps
// the syscall count low without holding a second copy of a long waveform.
constexpr size_t kChunkSamples = 65536;
constexpr qsizetype kChunkBytes = static_cast<qsizetype>(kChunkSamples * sizeof(double));

Error error(const QString& message) {
    return Error{message};
}

// memcpy through an integer rather than reinterpreting the double directly:
// that is the only way to byte-swap a floating point value without relying on
// type punning, and it makes the file identical on a big-endian host.
void appendLittleEndian(QByteArray& buffer, double value) {
    quint64 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    bits = qToLittleEndian(bits);
    buffer.append(reinterpret_cast<const char*>(&bits), sizeof(bits));
}

}  // namespace

std::optional<Error> exportBinary(
    const WaveDocument& document, const QString& filePath, const sampling::Progress& progress
) {
    // Progress budget: the first half is sampling, the second half writing.
    const std::vector<double> samples = sampling::sampleDocument(
        document,
        progress ? sampling::Progress([&](size_t done, size_t total) {
            progress(done, std::max<size_t>(2 * total, 1));
        })
                 : sampling::Progress{}
    );
    if (samples.empty()) {
        return error(QStringLiteral("cannot export an empty document (zero samples)"));
    }

    for (const double sample : samples) {
        if (! std::isfinite(sample)) {
            return error(QStringLiteral("cannot export a non-finite sample"));
        }
    }

    QSaveFile file(filePath);
    if (! file.open(QIODevice::WriteOnly)) {
        return error(
            QStringLiteral("cannot write binary file '%1': %2").arg(filePath, file.errorString())
        );
    }

    QByteArray buffer;
    buffer.reserve(kChunkBytes);
    for (size_t index = 0; index < samples.size(); ++index) {
        appendLittleEndian(buffer, samples[index]);
        const bool lastSample = index + 1 == samples.size();
        if (buffer.size() < kChunkBytes && ! lastSample) {
            continue;
        }
        if (file.write(buffer) != buffer.size()) {
            file.cancelWriting();
            return error(
                QStringLiteral("cannot write binary file '%1': %2").arg(filePath, file.errorString())
            );
        }
        // resize(0) rather than clear(), which would drop the reserved capacity.
        buffer.resize(0);
        if (progress) {
            progress(samples.size() + index + 1, 2 * samples.size());
        }
    }

    if (! file.commit()) {
        return error(
            QStringLiteral("cannot write binary file '%1': %2").arg(filePath, file.errorString())
        );
    }
    return std::nullopt;
}

std::optional<Error> exportBinary(
    const WaveDocument& document,
    const QString& filePath,
    double sampleRate,
    const sampling::Progress& progress
) {
    WaveDocument exportDocument = document;
    exportDocument.sampleRate   = sampleRate;
    return exportBinary(exportDocument, filePath, progress);
}

}  // namespace zwe::binaryio
