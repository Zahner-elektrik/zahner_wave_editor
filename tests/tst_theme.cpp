// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include <QApplication>
#include <QSettings>
#include <QtTest>

#include "ui/appsettings.h"
#include "ui/mainwindow.h"
#include "ui/theme.h"

using namespace zwe;

class TestTheme : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void palettesHaveExpectedColorsAndContrast();
    void accentsCanBeSerializedAndApplied();
    void applicationIconsAreAvailable();
    void storedThemeSurvivesAndFallsBackToThePalette();
};

void TestTheme::initTestCase() {
    // The real application sets these in main.cpp before constructing any
    // QSettings. Test binaries never run that setup, so without this,
    // QSettings has no organization name and its native (Windows registry)
    // backend silently fails every read/write with an AccessError. Use a
    // dedicated application name so the test doesn't touch the real app's
    // persisted settings - and so that no other suite ctest runs in parallel
    // writes the same keys (see tst_settings).
    QCoreApplication::setOrganizationName(QStringLiteral("Zahner-Elektrik"));
    QCoreApplication::setApplicationName(QStringLiteral("ZahnerWaveEditorTests"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
}

void TestTheme::palettesHaveExpectedColorsAndContrast() {
    const QPalette dark  = theme::darkPalette();
    const QPalette light = theme::lightPalette();

    QCOMPARE(dark.color(QPalette::Window), QColor(QStringLiteral("#1e2823")));
    QCOMPARE(dark.color(QPalette::Highlight), QColor(QStringLiteral("#a02832")));
    QCOMPARE(light.color(QPalette::Highlight), QColor(QStringLiteral("#a02832")));
    QVERIFY(theme::paletteIsDark(dark));
    QVERIFY(! theme::paletteIsDark(light));
    QVERIFY(dark.color(QPalette::Text).lightness() > dark.color(QPalette::Base).lightness());
    QVERIFY(light.color(QPalette::Text).lightness() < light.color(QPalette::Base).lightness());
}

void TestTheme::accentsCanBeSerializedAndApplied() {
    QCOMPARE(theme::accentColor(theme::Accent::Red), QColor(QStringLiteral("#a02832")));
    QCOMPARE(theme::accentColor(theme::Accent::Blue), QColor(QStringLiteral("#145099")));
    QCOMPARE(theme::accentName(theme::Accent::Red), QStringLiteral("red"));
    QCOMPARE(theme::accentName(theme::Accent::Blue), QStringLiteral("blue"));
    QCOMPARE(theme::accentFromName(QStringLiteral(" BLUE ")), theme::Accent::Blue);
    QCOMPARE(
        theme::accentFromName(QStringLiteral("invalid"), theme::Accent::Blue), theme::Accent::Blue
    );

    theme::apply(true, theme::Accent::Blue);
    QCOMPARE(QApplication::palette().color(QPalette::Highlight), QColor(QStringLiteral("#145099")));
    QCOMPARE(QApplication::palette().color(QPalette::Mid), QColor(QStringLiteral("#3b5c69")));
}

void TestTheme::applicationIconsAreAvailable() {
    QVERIFY(! theme::applicationIcon(false).isNull());
    QVERIFY(! theme::applicationIcon(true).isNull());
    const QIcon blue = theme::accentIcon(
        QIcon(QStringLiteral(":/icons/toolbar/add-layer.svg")),
        theme::accentColor(theme::Accent::Blue)
    );
    QVERIFY(! blue.isNull());
    const QImage image = blue.pixmap(64, 64).toImage();
    bool foundBlue     = false;
    for (int y = 0; y < image.height() && ! foundBlue; ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixelColor(x, y).rgb() == theme::accentColor(theme::Accent::Blue).rgb()) {
                foundBlue = true;
                break;
            }
        }
    }
    QVERIFY(foundBlue);
}

void TestTheme::storedThemeSurvivesAndFallsBackToThePalette() {
    // The user-facing path - Edit > Settings applying and persisting the theme -
    // is covered by tst_settings; this pins the storage contract of the keys,
    // which existing installations depend on.
    QSettings settings;
    settings.remove(QStringLiteral("mainWindow/darkTheme"));
    settings.remove(QStringLiteral("mainWindow/accent"));
    settings.sync();

    // Earlier tests in this suite leave the application palette in whatever state
    // theme::apply() last set it to; reset it so the fallback used when no
    // setting is stored starts from a known light state.
    theme::apply(false, theme::Accent::Red);
    QVERIFY(! appsettings::darkTheme());
    QCOMPARE(appsettings::accent(), theme::Accent::Red);
    theme::apply(true, theme::Accent::Red);
    QVERIFY(appsettings::darkTheme());

    appsettings::setDarkTheme(false);
    appsettings::setAccent(theme::Accent::Blue);
    QCOMPARE(QSettings().value(QStringLiteral("mainWindow/darkTheme")).toBool(), false);
    QCOMPARE(
        QSettings().value(QStringLiteral("mainWindow/accent")).toString(), QStringLiteral("blue")
    );
    QVERIFY(! appsettings::darkTheme());
    QCOMPARE(appsettings::accent(), theme::Accent::Blue);

    // A new window follows what was stored.
    MainWindow window;
    QCOMPARE(QApplication::palette().color(QPalette::Highlight), QColor(QStringLiteral("#145099")));
    QVERIFY(! theme::paletteIsDark(QApplication::palette()));
}

QTEST_MAIN(TestTheme)
#include "tst_theme.moc"
