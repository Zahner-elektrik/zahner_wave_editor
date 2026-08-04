// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <optional>

#include "sampling.h"
#include "segment.h"
#include "wavedocument.h"

namespace zwe::csvio {

struct Error {
    // For import errors tied to a specific row, includes the 1-based line
    // number, e.g. "line 3: unparseable number".
    QString message;
};

// Exports the document's total signal (sampling::sampleDocument) as one value
// per line: plain text, UTF-8 without BOM, Unix line endings, "." as the
// decimal separator regardless of locale, and up to
// document.exportSettings.significantDigits significant digits in shortest
// round-trip style. Refuses (returns an Error, writes nothing) when the
// document is empty (0 samples), any sample is non-finite, or filePath cannot
// be written. `progress` (optional) is called periodically with (done, total)
// over the whole export, sampling and writing combined.
std::optional<Error> exportCsv(
    const WaveDocument& document, const QString& filePath, const sampling::Progress& progress = {}
);

// As above, but samples at `sampleRate` without changing the document. This
// is intended for a one-off export override from the export dialog.
std::optional<Error> exportCsv(
    const WaveDocument& document,
    const QString& filePath,
    double sampleRate,
    const sampling::Progress& progress = {}
);

struct ImportResult {
    std::optional<Segment> segment;  // a ready-to-use Points segment on success
    std::optional<Error> error;
};

// Imports filePath as a superset of the export format into a Points
// segment: 1 column -> a value list, with t_i = i / sampleRate; 2 columns
// (separator "," or ";", an optional non-numeric first row treated as a
// header) -> (t, value) pairs, t shifted so the first point is 0. Rejects
// more than 2 columns, non-finite or unparseable values (naming the line
// number), and fewer than 2 resulting points. `interp` selects the
// resulting segment's point interpolation mode (default linear).
ImportResult importCsv(
    const QString& filePath,
    double sampleRate,
    PointsParams::Interp interp = PointsParams::Interp::Linear
);

}  // namespace zwe::csvio
