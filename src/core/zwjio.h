// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <optional>

#include "wavedocument.h"

namespace zwe::zwjio {

struct Error {
    // Names the offending JSON path, e.g.
    // "layers[0].segments[2].params.duty: must be in (0, 1)".
    QString message;
};

struct LoadResult {
    std::optional<WaveDocument> document;  // nullopt on failure
    std::optional<Error> error;            // nullopt on success
};

// Loads and validates a .zwj document from filePath. Rejects a wrong
// "format", a "version" newer than this reader supports, an unknown segment
// type or mode, missing required fields, and non-finite or out-of-range
// numbers, each with an error naming the offending JSON path. Unknown JSON
// keys are ignored for forward compatibility.
LoadResult load(const QString& filePath);

// Writes document to filePath as indented JSON, current format version,
// every field explicit (no omitted defaults), keys exactly as documented.
// Returns std::nullopt on success.
std::optional<Error> save(const WaveDocument& document, const QString& filePath);

}  // namespace zwe::zwjio
