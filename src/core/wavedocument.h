// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>
#include <vector>

#include "wavelayer.h"

namespace zwe {

// How an export writes the sampled values. Neither format carries the value
// rate (sample i is the value at t = i / rate), so a file only has a time axis
// together with the rate its export was made with.
enum class ExportFormat {
    Csv,    // one decimal value per line, UTF-8, Unix line endings
    Binary  // raw little-endian IEEE-754 binary64, 8 bytes per sample
};

struct ExportSettings {
    // The file the last export went to, whatever its format. The .zwj key is
    // still "last_csv_path" because CSV was the only format when it was named.
    QString lastExportPath;
    ExportFormat format                          = ExportFormat::Csv;
    int significantDigits                        = 9;  // ExportFormat::Csv only

    bool operator==(const ExportSettings&) const = default;
};

struct WaveDocument {
    QString id;
    QString name;
    QString description;
    double sampleRate     = 1000.0;  // values per second, > 0
    double outputDataRate = 1.0;     // informational metadata in 1/s, > 0
    ExportSettings exportSettings;
    std::vector<WaveLayer> layers;

    bool operator==(const WaveDocument&) const = default;
};

// Max over enabled layers of layerDuration(); 0.0 for a document with no
// enabled layer (including an empty document).
double documentDuration(const WaveDocument& document);

}  // namespace zwe
