// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include "ui/filedialogpaths.h"

using namespace zwe;

// F-297: with nothing remembered, the file dialogs started in the working
// directory - the installation folder when Zahner Lab starts the editor - and
// saving a document did not change where Open started.
class TestFileDialogPaths : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanupTestCase();
    void startsInUserFolderNotWorkingDirectory();
    void rememberedFolderIsSharedUntilAKindHasItsOwn();
    void vanishedFolderFallsBackToUserFolder();
    void startPathKeepsOnlyUsableSuggestions();

private:
    QTemporaryDir workingDirectory_;
    QString originalWorkingDirectory_;
};

void TestFileDialogPaths::initTestCase() {
    // Own application name: ctest runs the suites in parallel, and the native
    // QSettings backend needs an organization name (see tst_settings).
    QCoreApplication::setOrganizationName(QStringLiteral("Zahner-Elektrik"));
    QCoreApplication::setApplicationName(QStringLiteral("ZahnerWaveEditorFileDialogPathsTests"));
    QSettings::setDefaultFormat(QSettings::IniFormat);

    // Stand-in for the installation folder the editor is started in.
    QVERIFY(workingDirectory_.isValid());
    originalWorkingDirectory_ = QDir::currentPath();
    QVERIFY(QDir::setCurrent(workingDirectory_.path()));
}

void TestFileDialogPaths::init() {
    QSettings settings;
    settings.remove(QStringLiteral("fileDialogs"));
    settings.sync();
}

void TestFileDialogPaths::cleanupTestCase() {
    init();
    QDir::setCurrent(originalWorkingDirectory_);
}

void TestFileDialogPaths::startsInUserFolderNotWorkingDirectory() {
    for (const char* key : {
             filedialogpaths::DocumentKey, filedialogpaths::ImportCsvKey, filedialogpaths::ExportKey
         }) {
        const QString directory = filedialogpaths::startDirectory(key);
        QVERIFY2(QFileInfo(directory).isAbsolute(), qPrintable(directory));
        QVERIFY2(QFileInfo(directory).isDir(), qPrintable(directory));
        QVERIFY2(directory != QDir::currentPath(), qPrintable(directory));
    }

    // Save As of a new document suggests a bare file name; that must not land
    // in the working directory either.
    const QString suggested =
        filedialogpaths::startPath(filedialogpaths::DocumentKey, QStringLiteral("untitled.zwj"));
    QCOMPARE(QFileInfo(suggested).fileName(), QStringLiteral("untitled.zwj"));
    QCOMPARE(
        QFileInfo(suggested).absolutePath(),
        filedialogpaths::startDirectory(filedialogpaths::DocumentKey)
    );
    QVERIFY(QFileInfo(suggested).absolutePath() != QDir::currentPath());
}

void TestFileDialogPaths::rememberedFolderIsSharedUntilAKindHasItsOwn() {
    QTemporaryDir documents;
    QTemporaryDir csv;
    QVERIFY(documents.isValid() && csv.isValid());

    // Saving a document decides where Open starts, and where the CSV dialogs
    // start as long as they have no folder of their own.
    filedialogpaths::remember(filedialogpaths::DocumentKey, documents.filePath("wave.zwj"));
    QCOMPARE(filedialogpaths::startDirectory(filedialogpaths::DocumentKey), documents.path());
    QCOMPARE(filedialogpaths::startDirectory(filedialogpaths::ImportCsvKey), documents.path());
    QCOMPARE(filedialogpaths::startDirectory(filedialogpaths::ExportKey), documents.path());

    // Once a kind was used somewhere else, it keeps that folder.
    filedialogpaths::remember(filedialogpaths::ImportCsvKey, csv.filePath("data.csv"));
    QCOMPARE(filedialogpaths::startDirectory(filedialogpaths::ImportCsvKey), csv.path());
    QCOMPARE(filedialogpaths::startDirectory(filedialogpaths::DocumentKey), documents.path());
    QCOMPARE(filedialogpaths::startDirectory(filedialogpaths::ExportKey), csv.path());
}

void TestFileDialogPaths::vanishedFolderFallsBackToUserFolder() {
    const QString userFolder = filedialogpaths::startDirectory(filedialogpaths::DocumentKey);
    QString removedFolder;
    {
        QTemporaryDir gone;
        QVERIFY(gone.isValid());
        removedFolder = gone.path();
        filedialogpaths::remember(filedialogpaths::DocumentKey, gone.filePath("wave.zwj"));
        QCOMPARE(filedialogpaths::startDirectory(filedialogpaths::DocumentKey), removedFolder);
    }
    QVERIFY(! QFileInfo::exists(removedFolder));
    QCOMPARE(filedialogpaths::startDirectory(filedialogpaths::DocumentKey), userFolder);
}

void TestFileDialogPaths::startPathKeepsOnlyUsableSuggestions() {
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    const QString start = filedialogpaths::startDirectory(filedialogpaths::ExportKey);

    QCOMPARE(filedialogpaths::startPath(filedialogpaths::ExportKey), start);
    // An absolute path into an existing folder - the document's last export -
    // is kept as it is.
    const QString existing = folder.filePath("wave.csv");
    QCOMPARE(filedialogpaths::startPath(filedialogpaths::ExportKey, existing), existing);
    // A relative path or one into a folder that is gone keeps only its name.
    QCOMPARE(
        filedialogpaths::startPath(filedialogpaths::ExportKey, QStringLiteral("wave.csv")),
        QDir(start).filePath(QStringLiteral("wave.csv"))
    );
    QCOMPARE(
        filedialogpaths::startPath(filedialogpaths::ExportKey, folder.filePath("gone/wave.csv")),
        QDir(start).filePath(QStringLiteral("wave.csv"))
    );
}

QTEST_GUILESS_MAIN(TestFileDialogPaths)
#include "tst_filedialogpaths.moc"
