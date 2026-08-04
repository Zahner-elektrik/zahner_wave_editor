// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "appsettings.h"

#include <QApplication>
#include <QCoreApplication>
#include <QLatin1String>
#include <QLocale>
#include <QPointer>
#include <QSettings>
#include <QTranslator>
#include <QVariant>
#include <iterator>

namespace zwe::appsettings {

namespace {

constexpr auto SettingsLanguageKey = "application/language";

// The theme keys keep their original "mainWindow/" prefix from when the theme
// was toggled in the View menu. Renaming them would silently reset the look of
// every existing installation.
constexpr auto SettingsDarkThemeKey     = "mainWindow/darkTheme";
constexpr auto SettingsAccentKey        = "mainWindow/accent";

constexpr auto SettingsShowLegendKey    = "canvas/showLegend";
constexpr auto SettingsSnapToGridKey    = "canvas/snapToGrid";
constexpr auto SettingsSnapDivisionsKey = "canvas/snapDivisions";
constexpr auto SettingsSnapFixedStepKey = "canvas/snapFixedStep";
constexpr auto SettingsSnapTimeStepKey  = "canvas/snapTimeStep";
constexpr auto SettingsSnapValueStepKey = "canvas/snapValueStep";

constexpr int DefaultSnapDivisions      = 10;
constexpr int MinimumSnapDivisions      = 1;
constexpr int MaximumSnapDivisions      = 100;

// The source language of the code: every tr() string is already English, so no
// catalog is loaded for it.
constexpr auto SourceLanguage = "en";

struct ShippedLanguage {
    const char* id;
    const char* name;
};

// Names are endonyms on purpose - "Deutsch" stays recognizable while the
// interface is still in English, and "English" while it is in German.
constexpr ShippedLanguage ShippedLanguages[] = {
    {SourceLanguage, "English"},
    {"de", "Deutsch"},
};

// QLocale::system() ignores LANG/LC_ALL on Windows and macOS; honor them when
// set so the language can be selected from the environment, which is what the
// localization test and the CI machines do.
QLocale systemLocale() {
    QLocale locale = QLocale::system();
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    const QString posixLocale = qEnvironmentVariable("LC_ALL", qEnvironmentVariable("LANG"));
    if (! posixLocale.isEmpty()) {
        locale = QLocale(posixLocale.section(QLatin1Char('.'), 0, 0));
    }
#endif
    return locale;
}

}  // namespace

QVector<Language> availableLanguages() {
    QVector<Language> languages;
    languages.reserve(static_cast<qsizetype>(std::size(ShippedLanguages)));
    for (const ShippedLanguage& shipped : ShippedLanguages) {
        languages.append(
            Language{QString::fromLatin1(shipped.id), QString::fromUtf8(shipped.name)}
        );
    }
    return languages;
}

QString languageName(const QString& languageId) {
    for (const ShippedLanguage& shipped : ShippedLanguages) {
        if (languageId == QLatin1String(shipped.id)) {
            return QString::fromUtf8(shipped.name);
        }
    }
    return {};
}

QString systemLanguage() {
    const QLocale::Language osLanguage = systemLocale().language();
    for (const ShippedLanguage& shipped : ShippedLanguages) {
        if (QLocale(QString::fromLatin1(shipped.id)).language() == osLanguage) {
            return QString::fromLatin1(shipped.id);
        }
    }
    return QString::fromLatin1(SourceLanguage);
}

QString language() {
    const QString stored = QSettings().value(QLatin1String(SettingsLanguageKey)).toString();
    return languageName(stored).isEmpty() ? QString{} : stored;
}

void setLanguage(const QString& languageId) {
    if (! languageId.isEmpty() && languageName(languageId).isEmpty()) {
        return;
    }
    QSettings settings;
    if (languageId.isEmpty()) {
        // Following the system is the default, so store nothing for it: an
        // installation that never opened the dialog and one that explicitly
        // picked the system language then behave identically.
        settings.remove(QLatin1String(SettingsLanguageKey));
    } else {
        settings.setValue(QLatin1String(SettingsLanguageKey), languageId);
    }
}

QString effectiveLanguage() {
    const QString stored = language();
    return stored.isEmpty() ? systemLanguage() : stored;
}

bool installTranslations(QCoreApplication& app) {
    // Removing the catalogs of an earlier call keeps this function repeatable,
    // which the tests rely on. It does not retranslate live widgets.
    static QVector<QPointer<QTranslator>> installed;
    for (const QPointer<QTranslator>& translator : installed) {
        if (translator) {
            QCoreApplication::removeTranslator(translator);
            delete translator;
        }
    }
    installed.clear();

    const QString languageId = effectiveLanguage();
    if (languageId == QLatin1String(SourceLanguage)) {
        return false;
    }

    // Qt's catalog goes in first: it holds the strings of the standard dialogs -
    // the QMessageBox and QDialogButtonBox buttons, the file dialog, the context
    // menu of a text field - which no tr() in this code base ever sees. Ours
    // follows it because Qt asks the translators in reverse order, so a string
    // this application translates itself still wins.
    for (const char* catalog : {"qtbase", "zwe"}) {
        auto* translator = new QTranslator(&app);
        const QString resource =
            QStringLiteral(":/i18n/%1_%2.qm").arg(QLatin1String(catalog), languageId);
        if (translator->load(resource) && QCoreApplication::installTranslator(translator)) {
            installed.append(translator);
        } else {
            delete translator;
        }
    }
    return ! installed.isEmpty();
}

bool darkTheme() {
    const QVariant stored = QSettings().value(QLatin1String(SettingsDarkThemeKey));
    return stored.isValid() ? stored.toBool() : theme::paletteIsDark(QApplication::palette());
}

void setDarkTheme(bool dark) {
    QSettings().setValue(QLatin1String(SettingsDarkThemeKey), dark);
}

theme::Accent accent() {
    return theme::accentFromName(
        QSettings().value(QLatin1String(SettingsAccentKey)).toString(), theme::Accent::Red
    );
}

void setAccent(theme::Accent accent) {
    QSettings().setValue(QLatin1String(SettingsAccentKey), theme::accentName(accent));
}

bool showLegend() {
    return QSettings().value(QLatin1String(SettingsShowLegendKey), true).toBool();
}

void setShowLegend(bool visible) {
    QSettings().setValue(QLatin1String(SettingsShowLegendKey), visible);
}

bool snapToGrid() {
    return QSettings().value(QLatin1String(SettingsSnapToGridKey), true).toBool();
}

void setSnapToGrid(bool enabled) {
    QSettings().setValue(QLatin1String(SettingsSnapToGridKey), enabled);
}

int snapDivisions() {
    const int stored =
        QSettings().value(QLatin1String(SettingsSnapDivisionsKey), DefaultSnapDivisions).toInt();
    return stored >= MinimumSnapDivisions && stored <= MaximumSnapDivisions ? stored
                                                                            : DefaultSnapDivisions;
}

void setSnapDivisions(int divisions) {
    if (divisions < MinimumSnapDivisions || divisions > MaximumSnapDivisions) {
        return;
    }
    QSettings().setValue(QLatin1String(SettingsSnapDivisionsKey), divisions);
}

bool snapFixedStep() {
    return QSettings().value(QLatin1String(SettingsSnapFixedStepKey), false).toBool();
}

void setSnapFixedStep(bool fixed) {
    QSettings().setValue(QLatin1String(SettingsSnapFixedStepKey), fixed);
}

double snapTimeStep() {
    return QSettings().value(QLatin1String(SettingsSnapTimeStepKey), 0.0).toDouble();
}

void setSnapTimeStep(double step) {
    if (step > 0.0) {
        QSettings().setValue(QLatin1String(SettingsSnapTimeStepKey), step);
    }
}

double snapValueStep() {
    return QSettings().value(QLatin1String(SettingsSnapValueStepKey), 0.0).toDouble();
}

void setSnapValueStep(double step) {
    if (step > 0.0) {
        QSettings().setValue(QLatin1String(SettingsSnapValueStepKey), step);
    }
}

}  // namespace zwe::appsettings
