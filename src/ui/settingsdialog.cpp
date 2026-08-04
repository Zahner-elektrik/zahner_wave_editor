// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "settingsdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QVariant>

#include "appsettings.h"

namespace zwe {

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent), activeLanguage_(appsettings::effectiveLanguage()) {
    setObjectName(QStringLiteral("settingsDialog"));
    setWindowTitle(tr("Settings"));
    setModal(true);
    setMinimumWidth(420);

    auto* layout      = new QVBoxLayout(this);

    auto* general     = new QGroupBox(tr("General"), this);
    auto* generalForm = new QFormLayout(general);
    language_         = new QComboBox(general);
    language_->setObjectName(QStringLiteral("languageComboBox"));
    // The system entry names the language it resolves to, so the choice is not
    // a guess about what the operating system reports.
    language_->addItem(
        tr("System language (%1)").arg(appsettings::languageName(appsettings::systemLanguage())),
        QString{}
    );
    for (const appsettings::Language& language : appsettings::availableLanguages()) {
        language_->addItem(language.name, language.id);
    }
    language_->setToolTip(tr("Language of the menus, dialogs and the built-in help"));
    generalForm->addRow(tr("Language"), language_);

    auto* languageHint = new QLabel(
        tr("A changed language is applied the next time the Zahner Wave Editor starts."), general
    );
    languageHint->setObjectName(QStringLiteral("languageHintLabel"));
    languageHint->setWordWrap(true);
    generalForm->addRow(QString{}, languageHint);
    layout->addWidget(general);

    auto* appearance     = new QGroupBox(tr("Appearance"), this);
    auto* appearanceForm = new QFormLayout(appearance);
    darkTheme_           = new QCheckBox(tr("Use the dark color theme"), appearance);
    darkTheme_->setObjectName(QStringLiteral("darkThemeCheckBox"));
    appearanceForm->addRow(tr("Theme"), darkTheme_);

    accent_ = new QComboBox(appearance);
    accent_->setObjectName(QStringLiteral("accentComboBox"));
    accent_->addItem(tr("Zahner Red"), static_cast<int>(theme::Accent::Red));
    accent_->addItem(tr("Zahner Blue"), static_cast<int>(theme::Accent::Blue));
    appearanceForm->addRow(tr("Accent color"), accent_);
    layout->addWidget(appearance);

    layout->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);

    selectLanguage(appsettings::language());
    setDarkThemeSelected(appsettings::darkTheme());
    setSelectedAccent(appsettings::accent());
}

QString SettingsDialog::selectedLanguage() const {
    return language_->currentData().toString();
}

bool SettingsDialog::selectLanguage(const QString& languageId) {
    const int index = language_->findData(languageId);
    if (index < 0) {
        return false;
    }
    language_->setCurrentIndex(index);
    return true;
}

bool SettingsDialog::darkThemeSelected() const {
    return darkTheme_->isChecked();
}

void SettingsDialog::setDarkThemeSelected(bool dark) {
    darkTheme_->setChecked(dark);
}

theme::Accent SettingsDialog::selectedAccent() const {
    return static_cast<theme::Accent>(accent_->currentData().toInt());
}

void SettingsDialog::setSelectedAccent(theme::Accent accent) {
    const int index = accent_->findData(static_cast<int>(accent));
    if (index >= 0) {
        accent_->setCurrentIndex(index);
    }
}

bool SettingsDialog::restartRequired() const {
    return restartRequired_;
}

void SettingsDialog::accept() {
    appsettings::setLanguage(selectedLanguage());
    appsettings::setDarkTheme(darkThemeSelected());
    appsettings::setAccent(selectedAccent());
    // Compared after storing: picking "system language" on a German system is
    // not a change when German was already in use.
    restartRequired_ = appsettings::effectiveLanguage() != activeLanguage_;
    QDialog::accept();
}

}  // namespace zwe
