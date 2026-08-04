// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>

#include "core/version.h"
#include "ui/appsettings.h"
#include "ui/mainwindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Zahner-Elektrik"));
    QApplication::setApplicationName(QStringLiteral("ZahnerWaveEditor"));
    QApplication::setApplicationVersion(zwe::applicationVersion());
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/waveeditor.png")));

    // The stored language is read through QSettings, so this has to follow the
    // organization and application name, and precede everything that
    // translates strings: the command line parser and the main window.
    zwe::appsettings::installTranslations(app);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QCoreApplication::translate("main", "Waveform editor for Zahner Lab wave jobs")
    );
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QStringLiteral("file"),
        QCoreApplication::translate("main", "A .zwj waveform document to open."),
        QStringLiteral("[file]")
    );
    parser.process(app);

    zwe::MainWindow window;
    const QStringList args = parser.positionalArguments();
    if (! args.isEmpty()) {
        window.openDocument(args.first());
    }
    window.show();

    return QApplication::exec();
}
