// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "filedialogpaths.h"

#include <QDir>
#include <QFileInfo>
#include <QLatin1String>
#include <QSettings>
#include <QStandardPaths>

namespace zwe::filedialogpaths {

namespace {

constexpr auto LastDirectoryKey = "fileDialogs/lastPath";

bool isExistingAbsoluteDirectory(const QString& directory) {
    if (directory.isEmpty()) {
        return false;
    }
    const QFileInfo info(directory);
    return info.isAbsolute() && info.isDir();
}

QString storedDirectory(const char* key) {
    const QString directory = QSettings().value(QLatin1String(key)).toString();
    return isExistingAbsoluteDirectory(directory) ? QDir::cleanPath(directory) : QString();
}

QString userDirectory() {
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return isExistingAbsoluteDirectory(documents) ? QDir::cleanPath(documents) : QDir::homePath();
}

}  // namespace

QString startDirectory(const char* key) {
    QString directory = storedDirectory(key);
    if (directory.isEmpty()) {
        directory = storedDirectory(LastDirectoryKey);
    }
    return directory.isEmpty() ? userDirectory() : directory;
}

QString startPath(const char* key, const QString& suggestedFile) {
    const QFileInfo suggested(suggestedFile);
    if (! suggestedFile.isEmpty() && suggested.isAbsolute() &&
        isExistingAbsoluteDirectory(suggested.absolutePath())) {
        return suggested.absoluteFilePath();
    }
    const QString directory = startDirectory(key);
    const QString fileName  = suggestedFile.isEmpty() ? QString() : suggested.fileName();
    return fileName.isEmpty() ? directory : QDir(directory).filePath(fileName);
}

void remember(const char* key, const QString& filePath) {
    if (filePath.isEmpty()) {
        return;
    }
    const QString directory = QFileInfo(filePath).absolutePath();
    QSettings settings;
    settings.setValue(QLatin1String(key), directory);
    settings.setValue(QLatin1String(LastDirectoryKey), directory);
}

}  // namespace zwe::filedialogpaths
