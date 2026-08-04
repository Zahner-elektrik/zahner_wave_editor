// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QWidget>

#include "canvaswidget.h"

class QCheckBox;
class QComboBox;

namespace zwe {

class EngineeringEdit;

// The strip directly above the plot: whether canvas edits snap to the grid and
// how fine that grid is. It sits there rather than in a menu or in the settings
// dialog because the fineness is part of the editing itself - one point needs
// millisecond resolution, the next one a tenth of a second, and both happen in
// the same document.
//
// The bar owns the persistence of its own settings: every change is written
// immediately and read back on the next start, so MainWindow only has to pass
// settings() on to the canvas.
class GridBar final : public QWidget {
    Q_OBJECT

public:
    explicit GridBar(QWidget* parent = nullptr);

    // The stored settings, ready to hand to CanvasWidget::setSnapSettings().
    SnapSettings settings() const;

public slots:
    // The step the canvas currently derives from the zoom level. Shown in the
    // two fields so they always read as the step actually in effect, and so
    // switching to a fixed step starts from the value one was just working with.
    // Ignored once a fixed step is set: that number is the user's own.
    void showAutomaticStep(double timeStep, double valueStep);

signals:
    // Emitted after a change has been stored, so a receiver only has to apply
    // settings() to the canvas.
    void settingsChanged();

protected:
    void changeEvent(QEvent* event) override;

private:
    void store();
    void updateFieldState();

    QCheckBox* enabled_         = nullptr;
    QComboBox* fineness_        = nullptr;
    EngineeringEdit* timeStep_  = nullptr;
    EngineeringEdit* valueStep_ = nullptr;
};

}  // namespace zwe
