// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <optional>

#include "sampling.h"
#include "wavedocument.h"

namespace zwe::binaryio {

struct Error {
    QString message;
};

// Exports the document's total signal (sampling::sampleDocument) as raw
// little-endian IEEE-754 binary64 values: 8 bytes per sample, no header, no
// padding, no terminator. That is byte for byte what Python's
// struct.pack("<d", value) produces per value, so the file goes into
// zahner_link unchanged: read it as bytes for Link.append_to_resource(), or
// hand its path to Link.create_resource_from_file(ResourceTypeEnum.WAVE, ...).
//
// The value rate is not part of the file. Sample i is the value at
// t = i / rate, so pass the rate the export was made with as the job's
// value_rate. Refuses (returns an Error, writes nothing) when the document is
// empty (0 samples), any sample is non-finite, or filePath cannot be written.
// `progress` (optional) is called periodically with (done, total) over the
// whole export, sampling and writing combined.
std::optional<Error> exportBinary(
    const WaveDocument& document, const QString& filePath, const sampling::Progress& progress = {}
);

// As above, but samples at `sampleRate` without changing the document. This
// is intended for a one-off export override from the export dialog.
std::optional<Error> exportBinary(
    const WaveDocument& document,
    const QString& filePath,
    double sampleRate,
    const sampling::Progress& progress = {}
);

}  // namespace zwe::binaryio
