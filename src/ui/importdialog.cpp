// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "importdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
#include <cmath>

namespace zwe {

namespace {

constexpr auto CsvFilter             = "CSV files (*.csv);;All files (*)";
constexpr int PreviewLineCount       = 8;
constexpr auto SettingsImportPathKey = "fileDialogs/importCsvPath";

int columnCount(const QString& row) {
    const bool hasSemicolon = row.contains(';');
    const bool hasComma     = row.contains(',');
    if (hasSemicolon && hasComma) {
        return 3;
    }
    if (hasSemicolon) {
        return row.count(';') + 1;
    }
    if (hasComma) {
        return row.count(',') + 1;
    }
    return 1;
}

}  // namespace

ImportDialog::ImportDialog(
    double defaultSampleRate,
    const QString& selectedLayerName,
    bool hasSelectedLayer,
    QWidget* parent
)
    : QDialog(parent) {
    setWindowTitle(tr("Import CSV"));
    setModal(true);
    resize(620, 460);

    auto* layout  = new QVBoxLayout(this);
    auto* fileRow = new QHBoxLayout;
    filePathEdit_ = new QLineEdit(this);
    filePathEdit_->setObjectName(QStringLiteral("importFilePathEdit"));
    auto* browse = new QPushButton(tr("Browse..."), this);
    fileRow->addWidget(filePathEdit_, 1);
    fileRow->addWidget(browse);
    layout->addLayout(fileRow);

    preview_ = new QPlainTextEdit(this);
    preview_->setObjectName(QStringLiteral("importPreview"));
    preview_->setReadOnly(true);
    preview_->setPlaceholderText(tr("The first lines of the selected CSV file appear here."));
    layout->addWidget(preview_, 1);

    interpretationLabel_ = new QLabel(tr("Select a CSV file."), this);
    interpretationLabel_->setObjectName(QStringLiteral("columnInterpretationLabel"));
    interpretationLabel_->setWordWrap(true);
    layout->addWidget(interpretationLabel_);

    auto* form  = new QFormLayout;
    sampleRate_ = new QDoubleSpinBox(this);
    sampleRate_->setObjectName(QStringLiteral("importSampleRateSpin"));
    sampleRate_->setDecimals(9);
    sampleRate_->setRange(0.000000001, 1.0e12);
    sampleRate_->setValue(
        std::isfinite(defaultSampleRate) && defaultSampleRate > 0.0 ? defaultSampleRate : 1000.0
    );
    sampleRate_->setSuffix(tr(" 1/s"));
    form->addRow(tr("Value Rate for 1-column files"), sampleRate_);

    interpolation_ = new QComboBox(this);
    interpolation_->setObjectName(QStringLiteral("importInterpolationCombo"));
    interpolation_->addItem(tr("Linear"), static_cast<int>(PointsParams::Interp::Linear));
    interpolation_->addItem(tr("Step"), static_cast<int>(PointsParams::Interp::Step));
    form->addRow(tr("Interpolation"), interpolation_);

    target_ = new QComboBox(this);
    target_->setObjectName(QStringLiteral("importTargetCombo"));
    target_->addItem(tr("Create a new layer"), static_cast<int>(Target::NewLayer));
    if (hasSelectedLayer) {
        target_->addItem(
            selectedLayerName.isEmpty() ? tr("Append to selected layer")
                                        : tr("Append to selected layer: %1").arg(selectedLayerName),
            static_cast<int>(Target::SelectedLayer)
        );
    }
    form->addRow(tr("Target"), target_);
    layout->addLayout(form);

    buttonBox_ = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttonBox_);

    connect(browse, &QPushButton::clicked, this, &ImportDialog::chooseFile);
    connect(filePathEdit_, &QLineEdit::textChanged, this, &ImportDialog::updatePreview);
    connect(sampleRate_, &QDoubleSpinBox::valueChanged, this, &ImportDialog::updateAcceptState);
    connect(buttonBox_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updateAcceptState();
}

QString ImportDialog::filePath() const {
    return filePathEdit_->text().trimmed();
}

double ImportDialog::sampleRate() const {
    return sampleRate_->value();
}

PointsParams::Interp ImportDialog::interpolation() const {
    return static_cast<PointsParams::Interp>(interpolation_->currentData().toInt());
}

ImportDialog::Target ImportDialog::target() const {
    return static_cast<Target>(target_->currentData().toInt());
}

void ImportDialog::setFilePath(const QString& filePath) {
    filePathEdit_->setText(filePath);
}

void ImportDialog::chooseFile() {
    QString startPath = QFileInfo(filePath()).absolutePath();
    if (filePath().isEmpty()) {
        startPath = QSettings().value(QLatin1String(SettingsImportPathKey)).toString();
    }
    const QString selected =
        QFileDialog::getOpenFileName(this, tr("Import CSV"), startPath, tr(CsvFilter));
    if (! selected.isEmpty()) {
        setFilePath(selected);
        QSettings().setValue(
            QLatin1String(SettingsImportPathKey), QFileInfo(selected).absolutePath()
        );
    }
}

void ImportDialog::updatePreview() {
    detectedColumnCount_    = 0;
    selectedFileIsReadable_ = false;
    QFile file(filePath());
    if (! file.open(QIODevice::ReadOnly)) {
        preview_->clear();
        interpretationLabel_->setText(tr("Select a readable CSV file."));
        sampleRate_->setEnabled(true);
        updateAcceptState();
        return;
    }

    selectedFileIsReadable_ = true;
    QStringList lines;
    QString firstContentLine;
    while (! file.atEnd() && lines.size() < PreviewLineCount) {
        QString line = QString::fromUtf8(file.readLine());
        if (line.endsWith('\n')) {
            line.chop(1);
        }
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        lines.push_back(line);
        if (firstContentLine.isEmpty() && ! line.trimmed().isEmpty()) {
            firstContentLine = line.trimmed();
        }
    }
    preview_->setPlainText(lines.join('\n'));
    detectedColumnCount_ = firstContentLine.isEmpty() ? 0 : columnCount(firstContentLine);

    if (detectedColumnCount_ == 1) {
        interpretationLabel_->setText(
            tr("Interpretation: 1-column value list. Times will use the Value Rate below.")
        );
    } else if (detectedColumnCount_ == 2) {
        interpretationLabel_->setText(
            tr("Interpretation: 2-column time/value pairs. An optional header is detected "
               "automatically.")
        );
    } else if (detectedColumnCount_ > 2) {
        interpretationLabel_->setText(tr("Unsupported: more than two columns or mixed separators.")
        );
    } else {
        interpretationLabel_->setText(tr("The selected file is empty."));
    }
    sampleRate_->setEnabled(detectedColumnCount_ != 2);
    updateAcceptState();
}

void ImportDialog::updateAcceptState() {
    const bool validRate = detectedColumnCount_ != 1 || sampleRate_->value() > 0.0;
    buttonBox_->button(QDialogButtonBox::Open)
        ->setEnabled(
            selectedFileIsReadable_ && detectedColumnCount_ >= 1 && detectedColumnCount_ <= 2 &&
            validRate
        );
}

}  // namespace zwe
