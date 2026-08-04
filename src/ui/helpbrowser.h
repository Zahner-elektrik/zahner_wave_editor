// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QWidget>

class QTextBrowser;
class QToolBar;

namespace zwe {

class HelpBrowser final : public QWidget {
    Q_OBJECT

public:
    explicit HelpBrowser(QWidget* parent = nullptr);

    void openOverview();
    void openFormulaReference();
    QTextBrowser* browser() const;
    QToolBar* toolBar() const;

private:
    void openPage(const QString& page);

    QTextBrowser* browser_ = nullptr;
    QToolBar* toolbar_     = nullptr;
};

}  // namespace zwe
