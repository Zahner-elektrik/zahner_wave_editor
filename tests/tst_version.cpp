// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include <QRegularExpression>
#include <QtTest>

#include "core/version.h"

class TestVersion : public QObject
{
    Q_OBJECT

private slots:
    void versionIsNotEmpty();
    void versionIsNumeric();
    void fullVersionIsTaggedOrACommitHash();
};

void TestVersion::versionIsNotEmpty()
{
    QVERIFY(! zwe::applicationVersion().isEmpty());
    QVERIFY(! zwe::applicationVersionFull().isEmpty());
}

// Everything that consumes the version - project(), the Windows VERSIONINFO
// block, Info.plist, the IFW update check - needs a plain number, so a tag whose
// name does not parse has to fall back instead of leaking its text through.
void TestVersion::versionIsNumeric()
{
    const QRegularExpression numeric(QStringLiteral("^\\d+\\.\\d+(\\.\\d+)?(\\.\\d+)?$"));
    QVERIFY2(
        numeric.match(zwe::applicationVersion()).hasMatch(),
        qPrintable(zwe::applicationVersion())
    );
}

// The full version identifies the build, and it is what the window title and the
// installer artifacts carry: either it is built from the tag - possibly with a
// commit distance, hash and dirty marker appended - or the repository has no tag
// and it is the bare commit hash.
void TestVersion::fullVersionIsTaggedOrACommitHash()
{
    const QString full = zwe::applicationVersionFull();
    if (full.contains(zwe::applicationVersion())) {
        return;
    }

    const QRegularExpression commitHash(QStringLiteral("^[0-9a-f]{7,}(-dirty)?$"));
    QVERIFY2(commitHash.match(full).hasMatch(), qPrintable(full));
    QCOMPARE(zwe::applicationVersion(), QStringLiteral("0.0.0"));
}

QTEST_GUILESS_MAIN(TestVersion)
#include "tst_version.moc"
