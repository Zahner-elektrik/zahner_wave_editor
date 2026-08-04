// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "csvio.h"

#include <QFile>
#include <QSaveFile>
#include <QStringList>
#include <algorithm>
#include <cmath>

#include "pointinterpolation.h"
#include "sampling.h"

namespace zwe::csvio {

namespace {

Error error(const QString& message) {
    return Error{message};
}

std::optional<double> parseFiniteNumber(const QString& text) {
    bool ok            = false;
    const double value = text.trimmed().toDouble(&ok);
    if (! ok || ! std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

ImportResult importError(int line, const QString& reason) {
    return {.error = error(QStringLiteral("line %1: %2").arg(line).arg(reason))};
}

QStringList splitRow(const QString& row, QChar separator) {
    QStringList columns = row.split(separator, Qt::KeepEmptyParts);
    for (QString& column : columns) {
        column = column.trimmed();
    }
    return columns;
}

}  // namespace

std::optional<Error> exportCsv(
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
            QStringLiteral("cannot write CSV file '%1': %2").arg(filePath, file.errorString())
        );
    }

    const int significantDigits = std::clamp(document.exportSettings.significantDigits, 1, 17);
    for (size_t index = 0; index < samples.size(); ++index) {
        const QByteArray line =
            QString::number(samples[index], 'g', significantDigits).toLatin1() + '\n';
        if (file.write(line) != line.size()) {
            file.cancelWriting();
            return error(
                QStringLiteral("cannot write CSV file '%1': %2").arg(filePath, file.errorString())
            );
        }
        if (progress && ((index + 1) % 65536 == 0 || index + 1 == samples.size())) {
            progress(samples.size() + index + 1, 2 * samples.size());
        }
    }

    if (! file.commit()) {
        return error(
            QStringLiteral("cannot write CSV file '%1': %2").arg(filePath, file.errorString())
        );
    }
    return std::nullopt;
}

std::optional<Error> exportCsv(
    const WaveDocument& document,
    const QString& filePath,
    double sampleRate,
    const sampling::Progress& progress
) {
    WaveDocument exportDocument = document;
    exportDocument.sampleRate   = sampleRate;
    return exportCsv(exportDocument, filePath, progress);
}

ImportResult importCsv(const QString& filePath, double sampleRate, PointsParams::Interp interp) {
    QFile file(filePath);
    if (! file.open(QIODevice::ReadOnly)) {
        return {
            .error = error(
                QStringLiteral("cannot read CSV file '%1': %2").arg(filePath, file.errorString())
            )
        };
    }

    QString contents = QString::fromUtf8(file.readAll());
    if (! contents.isEmpty() && contents.front() == QChar::ByteOrderMark) {
        contents.remove(0, 1);
    }
    const QStringList rows = contents.split('\n', Qt::KeepEmptyParts);

    std::vector<std::pair<double, double>> points;
    int columnCount   = 0;
    int firstDataLine = 0;

    for (qsizetype index = 0; index < rows.size(); ++index) {
        QString row = rows[index];
        if (row.endsWith('\r')) {
            row.chop(1);
        }
        const int line = static_cast<int>(index) + 1;

        // A final newline produces one empty split item; it is not an empty
        // CSV row. Empty rows elsewhere are always invalid input.
        if (index == rows.size() - 1 && row.isEmpty() && contents.endsWith('\n')) {
            continue;
        }
        if (row.trimmed().isEmpty()) {
            return importError(line, QStringLiteral("empty row"));
        }

        const bool hasSemicolon = row.contains(';');
        const bool hasComma     = row.contains(',');
        if (hasSemicolon && hasComma) {
            return importError(line, QStringLiteral("mixed column separators"));
        }
        const QChar separator     = hasSemicolon ? ';' : ',';
        const QStringList columns = splitRow(row, separator);

        if (columns.size() > 2) {
            return importError(line, QStringLiteral("expected one or two columns"));
        }
        if (columnCount == 0) {
            columnCount = columns.size();
            if (columnCount == 2) {
                const auto time  = parseFiniteNumber(columns[0]);
                const auto value = parseFiniteNumber(columns[1]);
                if (! time && ! value) {
                    continue;  // Optional two-column header.
                }
                if (! time || ! value) {
                    return importError(line, QStringLiteral("unparseable number"));
                }
                points.emplace_back(*time, *value);
                firstDataLine = line;
                continue;
            }
        }

        if (columns.size() != columnCount) {
            return importError(line, QStringLiteral("inconsistent column count"));
        }
        if (columnCount == 1) {
            const auto value = parseFiniteNumber(columns[0]);
            if (! value) {
                return importError(line, QStringLiteral("unparseable number"));
            }
            if (! (std::isfinite(sampleRate) && sampleRate > 0.0)) {
                return {
                    .error = error(QStringLiteral(
                        "value rate must be finite and greater than zero for a value list"
                    ))
                };
            }
            points.emplace_back(static_cast<double>(points.size()) / sampleRate, *value);
            firstDataLine = firstDataLine == 0 ? line : firstDataLine;
        } else {
            const auto time  = parseFiniteNumber(columns[0]);
            const auto value = parseFiniteNumber(columns[1]);
            if (! time || ! value) {
                return importError(line, QStringLiteral("unparseable number"));
            }
            if (! points.empty() && *time <= points.back().first) {
                return importError(line, QStringLiteral("point times must be strictly increasing"));
            }
            points.emplace_back(*time, *value);
            firstDataLine = firstDataLine == 0 ? line : firstDataLine;
        }
    }

    if (points.size() < 2) {
        return {.error = error(QStringLiteral("CSV import requires at least two points"))};
    }

    if (columnCount == 2) {
        const double firstTime = points.front().first;
        for (auto& [time, value] : points) {
            Q_UNUSED(value);
            time -= firstTime;
        }
    }
    if (! pointinterpolation::arePointsValid(points)) {
        const int line = firstDataLine == 0 ? 1 : firstDataLine;
        return importError(
            line, QStringLiteral("point times must be finite and strictly increasing")
        );
    }

    Segment segment;
    segment.params             = PointsParams{interp, std::move(points)};
    const auto& importedPoints = std::get<PointsParams>(segment.params).points;
    if (columnCount == 1) {
        segment.duration = static_cast<double>(importedPoints.size()) / sampleRate;
    } else {
        segment.duration = importedPoints.back().first;
    }
    return {.segment = std::move(segment)};
}

}  // namespace zwe::csvio
