// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "exportdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cmath>

#include "core/sampling.h"

namespace zwe {

namespace {

constexpr auto CsvFilter             = "CSV files (*.csv)";
constexpr auto BinaryFilter          = "Binary wave files (*.bin)";
constexpr auto SettingsExportPathKey = "fileDialogs/exportCsvPath";

}  // namespace

ExportDialog::ExportDialog(const WaveDocument& document, QWidget* parent)
    : QDialog(parent), document_(document) {
    setWindowTitle(tr("Export Waveform"));
    setModal(true);
    setMinimumWidth(560);

    auto* layout  = new QVBoxLayout(this);
    form_         = new QFormLayout;

    auto* fileRow = new QHBoxLayout;
    filePathEdit_ = new QLineEdit(document.exportSettings.lastExportPath, this);
    filePathEdit_->setObjectName(QStringLiteral("exportFilePathEdit"));
    auto* browse = new QPushButton(tr("Browse..."), this);
    fileRow->addWidget(filePathEdit_, 1);
    fileRow->addWidget(browse);
    form_->addRow(tr("File"), fileRow);

    format_ = new QComboBox(this);
    format_->setObjectName(QStringLiteral("exportFormatCombo"));
    format_->addItem(
        tr("CSV - one value per line (Zahner Lab)"), static_cast<int>(ExportFormat::Csv)
    );
    format_->addItem(
        tr("Binary - little-endian double, 8 bytes per value (zahner_link)"),
        static_cast<int>(ExportFormat::Binary)
    );
    const int formatIndex = format_->findData(static_cast<int>(document.exportSettings.format));
    format_->setCurrentIndex(formatIndex < 0 ? 0 : formatIndex);
    form_->addRow(tr("Format"), format_);

    sampleRate_ = new QDoubleSpinBox(this);
    sampleRate_->setObjectName(QStringLiteral("exportSampleRateSpin"));
    sampleRate_->setDecimals(1);
    sampleRate_->setRange(0.1, 1.0e12);
    sampleRate_->setValue(
        std::isfinite(document.sampleRate) && document.sampleRate > 0.0 ? document.sampleRate
                                                                        : 1000.0
    );
    sampleRate_->setSuffix(tr(" 1/s"));
    form_->addRow(tr("Value rate"), sampleRate_);

    significantDigits_ = new QSpinBox(this);
    significantDigits_->setObjectName(QStringLiteral("significantDigitsSpin"));
    significantDigits_->setRange(1, 17);
    significantDigits_->setValue(document.exportSettings.significantDigits);
    form_->addRow(tr("Significant digits"), significantDigits_);
    layout->addLayout(form_);

    information_ = new QLabel(this);
    information_->setObjectName(QStringLiteral("exportInformationLabel"));
    information_->setWordWrap(true);
    layout->addWidget(information_);
    updateInformation();

    if (! documentHasExportSamples_) {
        auto* warning = new QLabel(tr("This document has no samples to export."), this);
        warning->setObjectName(QStringLiteral("exportWarningLabel"));
        layout->addWidget(warning);
    }

    buttonBox_ = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttonBox_);

    connect(browse, &QPushButton::clicked, this, &ExportDialog::chooseFile);
    connect(filePathEdit_, &QLineEdit::textChanged, this, &ExportDialog::updateAcceptState);
    connect(format_, &QComboBox::currentIndexChanged, this, &ExportDialog::formatChanged);
    connect(sampleRate_, &QDoubleSpinBox::valueChanged, this, &ExportDialog::updateInformation);
    connect(buttonBox_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    formatChanged();
}

QString ExportDialog::suffixFor(ExportFormat format) {
    return format == ExportFormat::Binary ? QStringLiteral(".bin") : QStringLiteral(".csv");
}

ExportFormat ExportDialog::format() const {
    return static_cast<ExportFormat>(format_->currentData().toInt());
}

double ExportDialog::sampleRate() const {
    return sampleRate_->value();
}

QString ExportDialog::filePath() const {
    return filePathEdit_->text().trimmed();
}

int ExportDialog::significantDigits() const {
    return significantDigits_->value();
}

void ExportDialog::setFilePath(const QString& filePath) {
    filePathEdit_->setText(filePath);
}

void ExportDialog::formatChanged() {
    // Significant digits are a property of the decimal text, so they say
    // nothing about a binary export: every value is a full binary64 there.
    form_->setRowVisible(significantDigits_, format() == ExportFormat::Csv);

    // Follow the format with the suffix, but only when the path still carries
    // the other format's suffix. A name the user typed themselves stays.
    const QString wanted = suffixFor(format());
    const QString other  = suffixFor(
        format() == ExportFormat::Binary ? ExportFormat::Csv : ExportFormat::Binary
    );
    const QString path = filePath();
    if (! path.isEmpty() && path.endsWith(other, Qt::CaseInsensitive)) {
        setFilePath(path.chopped(other.size()) + wanted);
    }
    updateInformation();
}

void ExportDialog::chooseFile() {
    QString startPath = filePathEdit_->text();
    if (startPath.isEmpty()) {
        startPath = QSettings().value(QLatin1String(SettingsExportPathKey)).toString();
    }
    const QString filter =
        format() == ExportFormat::Binary ? tr(BinaryFilter) : tr(CsvFilter);
    const QString selected =
        QFileDialog::getSaveFileName(this, tr("Export Waveform"), startPath, filter);
    if (! selected.isEmpty()) {
        setFilePath(selected);
        QSettings().setValue(
            QLatin1String(SettingsExportPathKey), QFileInfo(selected).absolutePath()
        );
    }
}

void ExportDialog::updateInformation() {
    WaveDocument exportDocument = document_;
    exportDocument.sampleRate   = sampleRate();
    const size_t pointCount     = sampling::sampleCount(exportDocument);
    documentHasExportSamples_   = pointCount > 0;
    QString text                = tr("Number of points: %1\n"
                                     "Total duration: %2 s")
                       .arg(static_cast<qulonglong>(pointCount))
                       .arg(documentDuration(document_), 0, 'g', 12);
    // Which format is right depends entirely on what reads the file, so name
    // the consumer here rather than leaving it to the help pages.
    if (format() == ExportFormat::Binary) {
        text += tr("\nFile size: %1 bytes (8 per value)\n"
                   "Raw little-endian doubles without a header - upload it with the "
                   "zahner_link library, e.g. create_resource_from_file(). Zahner Lab "
                   "does not read this format.")
                    .arg(static_cast<qulonglong>(pointCount) * 8);
    } else {
        text += tr("\nDecimal text, one value per line - the format Zahner Lab reads.");
    }
    information_->setText(text);
    updateAcceptState();
}

void ExportDialog::updateAcceptState() {
    if (! buttonBox_) {
        return;
    }
    buttonBox_->button(QDialogButtonBox::Save)
        ->setEnabled(documentHasExportSamples_ && ! filePath().isEmpty());
}

}  // namespace zwe
