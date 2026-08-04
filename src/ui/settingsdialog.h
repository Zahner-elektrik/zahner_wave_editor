// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QDialog>
#include <QString>

#include "theme.h"

class QCheckBox;
class QComboBox;

namespace zwe {

// The application settings, grouped by topic. Nothing is written while the
// dialog is open: accept() stores every value at once, so rejecting it - or
// closing the window - leaves the installation untouched. New application-wide
// settings belong here rather than in a menu; add a row to the matching group
// box, or a new group box, and store it in accept().
class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    // The selected language id; empty means "follow the system language".
    QString selectedLanguage() const;
    // Fails and changes nothing for an id the editor does not ship.
    bool selectLanguage(const QString& languageId);

    bool darkThemeSelected() const;
    void setDarkThemeSelected(bool dark);

    theme::Accent selectedAccent() const;
    void setSelectedAccent(theme::Accent accent);

    // Whether accept() stored a language that differs from the one the running
    // instance was built with. Only true after accept(); the caller tells the
    // user that this one setting needs a restart.
    bool restartRequired() const;

    // Stores every setting, then closes the dialog.
    void accept() override;

private:
    QComboBox* language_  = nullptr;
    QCheckBox* darkTheme_ = nullptr;
    QComboBox* accent_    = nullptr;
    QString activeLanguage_;
    bool restartRequired_ = false;
};

}  // namespace zwe
