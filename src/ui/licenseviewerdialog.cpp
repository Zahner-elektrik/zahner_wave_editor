// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "licenseviewerdialog.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace zwe {

namespace {

constexpr auto LicenseResourceRole = Qt::UserRole;

}  // namespace

LicenseViewerDialog::LicenseViewerDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("licenseViewerDialog"));
    setWindowTitle(tr("Open Source Licenses"));
    resize(900, 600);
    setModal(false);

    auto* outer   = new QVBoxLayout(this);
    auto* content = new QHBoxLayout;
    packages_     = new QListWidget(this);
    packages_->setObjectName(QStringLiteral("licensePackages"));
    packages_->setMaximumWidth(240);
    contents_ = new QPlainTextEdit(this);
    contents_->setObjectName(QStringLiteral("licenseText"));
    contents_->setReadOnly(true);
    contents_->setLineWrapMode(QPlainTextEdit::NoWrap);
    content->addWidget(packages_);
    content->addWidget(contents_, 1);
    outer->addLayout(content, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QWidget::hide);
    connect(packages_, &QListWidget::currentRowChanged, this, [this] { displayCurrentPackage(); });

    auto addPackage = [this](const QString& name, const QString& resource) {
        auto* item = new QListWidgetItem(name, packages_);
        item->setData(LicenseResourceRole, resource);
    };
    addPackage(QStringLiteral("Qt 6"), QStringLiteral(":/licenses/qt/LGPL-3.0.txt"));
    addPackage(QStringLiteral("muParser"), QStringLiteral(":/licenses/muparser/LICENSE"));
    packages_->setCurrentRow(0);
}

QStringList LicenseViewerDialog::packageNames() const {
    QStringList names;
    for (int row = 0; row < packages_->count(); ++row) {
        names.push_back(packages_->item(row)->text());
    }
    return names;
}

QString LicenseViewerDialog::displayedText() const {
    return contents_->toPlainText();
}

bool LicenseViewerDialog::selectPackage(const QString& packageName) {
    const auto matches = packages_->findItems(packageName, Qt::MatchExactly);
    if (matches.isEmpty()) {
        return false;
    }
    packages_->setCurrentItem(matches.constFirst());
    return true;
}

void LicenseViewerDialog::displayCurrentPackage() {
    const auto* item = packages_->currentItem();
    if (! item) {
        contents_->setPlainText(tr("No license package selected."));
        return;
    }
    QFile file(item->data(LicenseResourceRole).toString());
    if (! file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        contents_->setPlainText(tr("The license text for %1 could not be read.").arg(item->text()));
        return;
    }
    contents_->setPlainText(QString::fromUtf8(file.readAll()));
}

}  // namespace zwe
