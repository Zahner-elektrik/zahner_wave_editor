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

    // Bound to one document, for an editor started by another application: the
    // ways of switching to a different file are taken out of the interface, so
    // what the caller opened is what gets edited and saved.
    const QCommandLineOption editOnly(
        QStringLiteral("edit-only"),
        QCoreApplication::translate(
            "main",
            "Edit only the given document: no new, open, save as or recent files."
        )
    );
    parser.addOption(editOnly);
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    if (parser.isSet(editOnly) && args.isEmpty()) {
        qCritical(
            "%s",
            qPrintable(QCoreApplication::translate(
                "main", "--edit-only needs the document to edit as an argument."
            ))
        );
        return 2;
    }

    zwe::MainWindow window;
    if (! args.isEmpty()) {
        if (! window.openDocument(args.first()) && parser.isSet(editOnly)) {
            // Nothing to edit and no way to open anything else, so an empty
            // locked-down window would be a dead end.
            return 1;
        }
    }
    if (parser.isSet(editOnly)) {
        window.setEditOnly(true);
    }
    window.show();

    return QApplication::exec();
}
