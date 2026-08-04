// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "helpbrowser.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QTextBrowser>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

#include "appsettings.h"
#include "theme.h"

namespace zwe {

namespace {

QDir helpDirectory() {
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QStringList candidates = {
        appDir.filePath(QStringLiteral("../share/zahner-wave-editor/help")),
        appDir.filePath(QStringLiteral("../resources/help")),
        QStringLiteral(ZWE_HELP_SOURCE_DIR),
    };
    for (const QString& candidate : candidates) {
        const QDir directory(candidate);
        if (QFileInfo::exists(directory.filePath(QStringLiteral("en/index.html")))) {
            return directory;
        }
    }
    return QDir(QStringLiteral(ZWE_HELP_SOURCE_DIR));
}

}  // namespace

HelpBrowser::HelpBrowser(QWidget* parent) : QWidget(parent, Qt::Window) {
    setObjectName(QStringLiteral("helpBrowser"));
    setWindowTitle(tr("Zahner Wave Editor Help"));
    resize(820, 620);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    toolbar_ = new QToolBar(this);
    toolbar_->setObjectName(QStringLiteral("helpToolBar"));
    const QColor accent = QApplication::palette().color(QPalette::Highlight);
    auto* backAction    = toolbar_->addAction(
        theme::accentIcon(QIcon(QStringLiteral(":/icons/help/back.svg")), accent), tr("Back")
    );
    auto* homeAction = toolbar_->addAction(
        theme::accentIcon(QIcon(QStringLiteral(":/icons/help/home.svg")), accent), tr("Overview")
    );
    auto* formulaAction = toolbar_->addAction(
        theme::accentIcon(QIcon(QStringLiteral(":/icons/help/formula.svg")), accent),
        tr("Formula Reference")
    );
    auto* forwardAction = toolbar_->addAction(
        theme::accentIcon(QIcon(QStringLiteral(":/icons/help/forward.svg")), accent), tr("Forward")
    );
    backAction->setObjectName(QStringLiteral("helpBackAction"));
    homeAction->setObjectName(QStringLiteral("helpHomeAction"));
    formulaAction->setObjectName(QStringLiteral("helpFormulaAction"));
    forwardAction->setObjectName(QStringLiteral("helpForwardAction"));
    browser_ = new QTextBrowser(this);
    browser_->setObjectName(QStringLiteral("helpTextBrowser"));
    browser_->setOpenExternalLinks(false);
    browser_->setOpenLinks(false);

    layout->addWidget(toolbar_);
    layout->addWidget(browser_, 1);

    connect(backAction, &QAction::triggered, browser_, &QTextBrowser::backward);
    connect(forwardAction, &QAction::triggered, browser_, &QTextBrowser::forward);
    connect(homeAction, &QAction::triggered, this, &HelpBrowser::openOverview);
    connect(formulaAction, &QAction::triggered, this, &HelpBrowser::openFormulaReference);
    connect(browser_, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        if (url.scheme() == QStringLiteral("qrc") || url.isRelative()) {
            browser_->setSource(url);
        } else {
            QDesktopServices::openUrl(url);
        }
    });

    openOverview();
}

void HelpBrowser::openOverview() {
    openPage(QStringLiteral("index.html"));
}

void HelpBrowser::openFormulaReference() {
    openPage(QStringLiteral("formula.html"));
}

QTextBrowser* HelpBrowser::browser() const {
    return browser_;
}

QToolBar* HelpBrowser::toolBar() const {
    return toolbar_;
}

void HelpBrowser::openPage(const QString& page) {
    // Follows the interface language, and falls back to English for a language
    // that has translated strings but no translated help pages yet.
    const QDir helpRoot = helpDirectory();
    QString path        = QStringLiteral("%1/%2").arg(appsettings::effectiveLanguage(), page);
    if (! QFileInfo::exists(helpRoot.filePath(path))) {
        path = QStringLiteral("en/%1").arg(page);
    }
    browser_->setSource(QUrl::fromLocalFile(helpRoot.filePath(path)));
}

}  // namespace zwe
