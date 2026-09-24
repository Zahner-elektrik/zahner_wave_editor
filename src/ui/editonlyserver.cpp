// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "editonlyserver.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QLocalSocket>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace zwe::editonly {

QString socketName(const QString& filePath) {
    // Hashed rather than taken verbatim: a local socket name has a length limit
    // and may not contain path separators, and the absolute path of a waveform
    // easily exceeds both.
    const QByteArray digest =
        QCryptographicHash::hash(
            QFileInfo(filePath).absoluteFilePath().toUtf8(), QCryptographicHash::Sha256
        )
            .toHex()
            .left(16);
    return QStringLiteral("zahner-wave-editor-") + QString::fromLatin1(digest);
}

bool requestWindow(const QString& filePath) {
    QLocalSocket socket;
    socket.connectToServer(socketName(filePath));
    if (! socket.waitForConnected(500)) {
        return false;
    }

#ifdef Q_OS_WIN
    // Windows lets only the foreground process bring a window forward. This
    // instance was just started by the application the user clicked in, so it
    // may pass that right on -- without it the other instance would merely make
    // its task bar entry flash.
    AllowSetForegroundWindow(ASFW_ANY);
#endif

    // The connection is the whole request; the running instance needs no
    // payload, it is already bound to this document.
    socket.waitForBytesWritten(100);
    socket.disconnectFromServer();
    return true;
}

}  // namespace zwe::editonly
