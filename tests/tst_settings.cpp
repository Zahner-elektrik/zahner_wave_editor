// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QSettings>
#include <QTimer>
#include <QtTest>
#include <algorithm>

#include "ui/appsettings.h"
#include "ui/mainwindow.h"
#include "ui/settingsdialog.h"
#include "ui/theme.h"

using namespace zwe;

namespace {

constexpr auto LanguageKey  = "application/language";
constexpr auto DarkThemeKey = "mainWindow/darkTheme";
constexpr auto AccentKey    = "mainWindow/accent";

}  // namespace

class TestSettings : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanupTestCase();
    void languageFollowsSystemUntilOneIsStored();
    void dialogStoresNothingBeforeItIsAccepted();
    void settingsActionAppliesAppearanceAndReplacesViewMenuEntries();
};

void TestSettings::initTestCase() {
    // The real application sets these in main.cpp before constructing any
    // QSettings. Test binaries never run that setup, so without this, QSettings
    // has no organization name and its native (Windows registry) backend
    // silently fails every read/write with an AccessError. The application name
    // has to differ from the one every other test binary uses: this suite
    // rewrites the theme keys, and ctest runs the suites in parallel.
    QCoreApplication::setOrganizationName(QStringLiteral("Zahner-Elektrik"));
    QCoreApplication::setApplicationName(QStringLiteral("ZahnerWaveEditorSettingsTests"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
}

void TestSettings::init() {
    QSettings settings;
    settings.remove(QLatin1String(LanguageKey));
    settings.remove(QLatin1String(DarkThemeKey));
    settings.remove(QLatin1String(AccentKey));
    settings.sync();
    // The stored theme falls back to the current palette, so pin it.
    theme::apply(false, theme::Accent::Red);
}

void TestSettings::cleanupTestCase() {
    theme::apply(false, theme::Accent::Red);
}

void TestSettings::languageFollowsSystemUntilOneIsStored() {
    QVERIFY(appsettings::language().isEmpty());
    QVERIFY(! appsettings::systemLanguage().isEmpty());
    QVERIFY(! appsettings::languageName(appsettings::systemLanguage()).isEmpty());
    QCOMPARE(appsettings::effectiveLanguage(), appsettings::systemLanguage());

    const QVector<appsettings::Language> languages = appsettings::availableLanguages();
    QCOMPARE(languages.front().id, QStringLiteral("en"));
    QVERIFY(
        std::any_of(
            languages.cbegin(), languages.cend(), [](const appsettings::Language& language) {
                return language.id == QStringLiteral("de");
            }
        )
    );
    for (const appsettings::Language& language : languages) {
        QVERIFY(! language.id.isEmpty());
        QCOMPARE(appsettings::languageName(language.id), language.name);
    }

    appsettings::setLanguage(QStringLiteral("de"));
    QCOMPARE(appsettings::language(), QStringLiteral("de"));
    QCOMPARE(appsettings::effectiveLanguage(), QStringLiteral("de"));
    QCOMPARE(QSettings().value(QLatin1String(LanguageKey)).toString(), QStringLiteral("de"));

    // A language the editor does not ship must neither be stored nor read back:
    // an outdated or hand-edited settings file cannot leave the editor without
    // a language.
    appsettings::setLanguage(QStringLiteral("kl"));
    QCOMPARE(appsettings::language(), QStringLiteral("de"));
    QSettings().setValue(QLatin1String(LanguageKey), QStringLiteral("kl"));
    QVERIFY(appsettings::language().isEmpty());
    QCOMPARE(appsettings::effectiveLanguage(), appsettings::systemLanguage());

    // English is the source language of every tr() string, so it needs no
    // catalog. (The German .qm lives in the application's resources, which a
    // test binary does not link.)
    appsettings::setLanguage(QStringLiteral("en"));
    QVERIFY(! appsettings::installTranslations(*QCoreApplication::instance()));

    // Following the system is the default and is stored as "no value".
    appsettings::setLanguage(QString{});
    QVERIFY(appsettings::language().isEmpty());
    QVERIFY(! QSettings().contains(QLatin1String(LanguageKey)));
}

void TestSettings::dialogStoresNothingBeforeItIsAccepted() {
    appsettings::setLanguage(QStringLiteral("en"));

    SettingsDialog dialog;
    QVERIFY(dialog.findChild<QComboBox*>(QStringLiteral("languageComboBox")));
    QVERIFY(dialog.findChild<QCheckBox*>(QStringLiteral("darkThemeCheckBox")));
    QVERIFY(dialog.findChild<QComboBox*>(QStringLiteral("accentComboBox")));
    QCOMPARE(dialog.selectedLanguage(), QStringLiteral("en"));
    QVERIFY(! dialog.darkThemeSelected());
    QCOMPARE(dialog.selectedAccent(), theme::Accent::Red);

    QVERIFY(dialog.selectLanguage(QStringLiteral("de")));
    QVERIFY(! dialog.selectLanguage(QStringLiteral("kl")));
    QCOMPARE(dialog.selectedLanguage(), QStringLiteral("de"));
    dialog.setDarkThemeSelected(true);
    dialog.setSelectedAccent(theme::Accent::Blue);
    QCOMPARE(appsettings::language(), QStringLiteral("en"));
    QVERIFY(! appsettings::darkTheme());
    QVERIFY(! dialog.restartRequired());

    dialog.accept();
    QCOMPARE(appsettings::language(), QStringLiteral("de"));
    QVERIFY(appsettings::darkTheme());
    QCOMPARE(appsettings::accent(), theme::Accent::Blue);
    QVERIFY(dialog.restartRequired());

    // Reopening shows the stored values, and accepting them unchanged is not a
    // language change - no restart hint for a dialog the user only looked at.
    SettingsDialog unchanged;
    QCOMPARE(unchanged.selectedLanguage(), QStringLiteral("de"));
    QVERIFY(unchanged.darkThemeSelected());
    QCOMPARE(unchanged.selectedAccent(), theme::Accent::Blue);
    unchanged.accept();
    QVERIFY(! unchanged.restartRequired());

    // Rejecting must not write anything.
    SettingsDialog rejected;
    QVERIFY(rejected.selectLanguage(QString{}));
    rejected.setDarkThemeSelected(false);
    rejected.reject();
    QCOMPARE(appsettings::language(), QStringLiteral("de"));
    QVERIFY(appsettings::darkTheme());
}

void TestSettings::settingsActionAppliesAppearanceAndReplacesViewMenuEntries() {
    MainWindow window;
    auto* action = window.findChild<QAction*>(QStringLiteral("settingsAction"));
    QVERIFY(action);
    QCOMPARE(action->shortcut(), QKeySequence(QKeySequence::Preferences));
    QCOMPARE(action->menuRole(), QAction::PreferencesRole);
    QVERIFY(! action->toolTip().isEmpty());
    // Theme and accent color moved out of the View menu into the dialog.
    QVERIFY(! window.findChild<QAction*>(QStringLiteral("darkThemeAction")));

    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<SettingsDialog*>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        dialog->setDarkThemeSelected(true);
        dialog->setSelectedAccent(theme::Accent::Blue);
        dialog->accept();
    });
    action->trigger();

    QVERIFY(appsettings::darkTheme());
    QCOMPARE(appsettings::accent(), theme::Accent::Blue);
    QCOMPARE(QApplication::palette().color(QPalette::Window), QColor(QStringLiteral("#1e2823")));
    QCOMPARE(QApplication::palette().color(QPalette::Highlight), QColor(QStringLiteral("#145099")));
}

QTEST_MAIN(TestSettings)
#include "tst_settings.moc"
