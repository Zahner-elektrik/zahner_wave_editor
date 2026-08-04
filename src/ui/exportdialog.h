// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QDialog>

#include "core/wavedocument.h"

class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace zwe {

class ExportDialog final : public QDialog {
    Q_OBJECT

public:
    explicit ExportDialog(const WaveDocument& document, QWidget* parent = nullptr);

    QString filePath() const;
    ExportFormat format() const;
    double sampleRate() const;
    int significantDigits() const;
    void setFilePath(const QString& filePath);

    // ".csv" or ".bin", the suffix an export in `format` is expected to have.
    static QString suffixFor(ExportFormat format);

private:
    void chooseFile();
    void formatChanged();
    void updateInformation();
    void updateAcceptState();

    WaveDocument document_;
    QFormLayout* form_             = nullptr;
    QLineEdit* filePathEdit_       = nullptr;
    QComboBox* format_             = nullptr;
    QDoubleSpinBox* sampleRate_    = nullptr;
    QSpinBox* significantDigits_   = nullptr;
    QLabel* information_           = nullptr;
    QDialogButtonBox* buttonBox_   = nullptr;
    bool documentHasExportSamples_ = false;
};

}  // namespace zwe
