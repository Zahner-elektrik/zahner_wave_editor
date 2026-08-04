// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include <QAction>
#include <QFileInfo>
#include <QTextBrowser>
#include <QToolBar>
#include <QtTest>

#include "ui/helpbrowser.h"
#include "ui/licenseviewerdialog.h"
#include "ui/mainwindow.h"

using namespace zwe;

class TestHelp : public QObject {
    Q_OBJECT

private slots:
    void helpActionsOpenOneReusableBrowser();
    void licenseViewerContainsOnlyShippedDependencies();
};

void TestHelp::helpActionsOpenOneReusableBrowser() {
    MainWindow window;
    auto* helpAction    = window.findChild<QAction*>(QStringLiteral("helpContentsAction"));
    auto* formulaAction = window.findChild<QAction*>(QStringLiteral("formulaHelpAction"));
    QVERIFY(helpAction);
    QVERIFY(formulaAction);
    QCOMPARE(helpAction->shortcut(), QKeySequence::HelpContents);

    helpAction->trigger();
    auto* browser = window.findChild<HelpBrowser*>(QStringLiteral("helpBrowser"));
    QVERIFY(browser);
    QTRY_VERIFY(browser->isVisible());
    QVERIFY(browser->browser()->source().isLocalFile());
    QVERIFY(QFileInfo::exists(browser->browser()->source().toLocalFile()));
    QVERIFY(browser->toolBar());
    QCOMPARE(browser->toolBar()->actions().size(), 4);
    for (const QAction* action : browser->toolBar()->actions()) {
        QVERIFY(! action->icon().isNull());
    }
    QVERIFY(browser->browser()->toPlainText().contains(QStringLiteral("Zahner Wave Editor")));

    browser->hide();
    formulaAction->trigger();
    QCOMPARE(window.findChildren<HelpBrowser*>().size(), 1);
    QTRY_VERIFY(browser->isVisible());
    QVERIFY(browser->browser()->toPlainText().contains(QStringLiteral("sin(2*pi*10*t)")));
}

void TestHelp::licenseViewerContainsOnlyShippedDependencies() {
    MainWindow window;
    auto* action = window.findChild<QAction*>(QStringLiteral("licensesAction"));
    QVERIFY(action);
    action->trigger();

    auto* viewer = window.findChild<LicenseViewerDialog*>(QStringLiteral("licenseViewerDialog"));
    QVERIFY(viewer);
    QCOMPARE(
        viewer->packageNames(), QStringList({QStringLiteral("Qt 6"), QStringLiteral("muParser")})
    );
    QVERIFY(viewer->displayedText().contains(QStringLiteral("GNU LESSER GENERAL PUBLIC LICENSE")));
    QVERIFY(viewer->selectPackage(QStringLiteral("muParser")));
    QVERIFY(viewer->displayedText().contains(QStringLiteral("Copyright 2020 Ingo Berg")));

    viewer->hide();
    action->trigger();
    QCOMPARE(window.findChildren<LicenseViewerDialog*>().size(), 1);
    QTRY_VERIFY(viewer->isVisible());
}

QTEST_MAIN(TestHelp)
#include "tst_help.moc"
