// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QDialog>
#include <QString>

#include "core/segment.h"

class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;

namespace zwe {

class ImportDialog final : public QDialog {
    Q_OBJECT

public:
    enum class Target { NewLayer, SelectedLayer };

    explicit ImportDialog(
        double defaultSampleRate,
        const QString& selectedLayerName,
        bool hasSelectedLayer,
        QWidget* parent = nullptr
    );

    QString filePath() const;
    double sampleRate() const;
    PointsParams::Interp interpolation() const;
    Target target() const;
    void setFilePath(const QString& filePath);

private:
    void chooseFile();
    void updatePreview();
    void updateAcceptState();

    QLineEdit* filePathEdit_     = nullptr;
    QPlainTextEdit* preview_     = nullptr;
    QLabel* interpretationLabel_ = nullptr;
    QDoubleSpinBox* sampleRate_  = nullptr;
    QComboBox* interpolation_    = nullptr;
    QComboBox* target_           = nullptr;
    QDialogButtonBox* buttonBox_ = nullptr;
    int detectedColumnCount_     = 0;
    bool selectedFileIsReadable_ = false;
};

}  // namespace zwe
