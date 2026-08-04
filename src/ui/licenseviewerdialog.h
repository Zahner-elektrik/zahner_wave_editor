// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QDialog>
#include <QStringList>

class QListWidget;
class QPlainTextEdit;

namespace zwe {

class LicenseViewerDialog final : public QDialog {
    Q_OBJECT

public:
    explicit LicenseViewerDialog(QWidget* parent = nullptr);

    QStringList packageNames() const;
    QString displayedText() const;
    bool selectPackage(const QString& packageName);

private:
    void displayCurrentPackage();

    QListWidget* packages_    = nullptr;
    QPlainTextEdit* contents_ = nullptr;
};

}  // namespace zwe
