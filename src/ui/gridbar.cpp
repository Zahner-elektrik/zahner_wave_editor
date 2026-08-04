// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "gridbar.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QVariant>
#include <algorithm>

#include "appsettings.h"
#include "engineeringedit.h"

namespace zwe {

namespace {

// The divisions offered by the fineness box. Every one of them keeps the snapped
// values round: a grid cell is always 1, 2 or 5 times a power of ten.
constexpr int FinenessDivisions[] = {1, 2, 5, 10};

// The entry that replaces the automatic step with a fixed one; not a division,
// so it cannot collide with the values above.
constexpr int FixedStepData = 0;

}  // namespace

GridBar::GridBar(QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 3, 8, 3);
    layout->setSpacing(6);

    enabled_ = new QCheckBox(tr("Snap to grid"), this);
    enabled_->setObjectName(QStringLiteral("snapEnabledCheckBox"));
    enabled_->setToolTip(
        tr("Let dragged points and segment boundaries land on the grid. "
           "Hold Ctrl while dragging to place one freely.")
    );
    layout->addWidget(enabled_);

    auto* finenessLabel = new QLabel(tr("Grid"), this);
    finenessLabel->setContentsMargins(10, 0, 0, 0);
    layout->addWidget(finenessLabel);

    fineness_ = new QComboBox(this);
    fineness_->setObjectName(QStringLiteral("snapFinenessComboBox"));
    fineness_->addItem(tr("Grid lines"), FinenessDivisions[0]);
    fineness_->addItem(tr("1/2"), FinenessDivisions[1]);
    fineness_->addItem(tr("1/5"), FinenessDivisions[2]);
    fineness_->addItem(tr("1/10"), FinenessDivisions[3]);
    fineness_->addItem(tr("Fixed step"), FixedStepData);
    fineness_->setToolTip(
        tr("How far apart the snap positions are: a fraction of one grid cell, so "
           "they get finer as you zoom in, or a fixed step. This is also how far "
           "the arrow keys move the selected point.")
    );
    layout->addWidget(fineness_);

    timeStep_ = new EngineeringEdit(this);
    timeStep_->setObjectName(QStringLiteral("snapTimeStepEdit"));
    timeStep_->setMaximumWidth(90);
    timeStep_->setToolTip(tr("Snap step along the time axis, in seconds"));
    layout->addWidget(new QLabel(tr("Time"), this));
    layout->addWidget(timeStep_);

    valueStep_ = new EngineeringEdit(this);
    valueStep_->setObjectName(QStringLiteral("snapValueStepEdit"));
    valueStep_->setMaximumWidth(90);
    valueStep_->setToolTip(tr("Snap step along the value axis"));
    layout->addWidget(new QLabel(tr("Value"), this));
    layout->addWidget(valueStep_);

    layout->addStretch(1);

    enabled_->setChecked(appsettings::snapToGrid());
    const int stored = appsettings::snapFixedStep() ? FixedStepData : appsettings::snapDivisions();
    fineness_->setCurrentIndex(std::max(0, fineness_->findData(stored)));
    timeStep_->setValue(appsettings::snapTimeStep());
    valueStep_->setValue(appsettings::snapValueStep());
    updateFieldState();

    connect(enabled_, &QCheckBox::toggled, this, [this] {
        store();
        emit settingsChanged();
    });
    connect(fineness_, &QComboBox::activated, this, [this] {
        // Switching to a fixed step adopts whatever the fields show, which is the
        // automatic step of the current zoom level - the one just in use.
        updateFieldState();
        store();
        emit settingsChanged();
    });
    for (EngineeringEdit* edit : {timeStep_, valueStep_}) {
        connect(edit, &EngineeringEdit::valueCommitted, this, [this, edit](double value) {
            // A step of zero or less would quietly stop that axis from snapping;
            // restoring the stored value says so instead.
            if (value <= 0.0) {
                edit->setValue(
                    edit == timeStep_ ? appsettings::snapTimeStep() : appsettings::snapValueStep()
                );
                return;
            }
            store();
            emit settingsChanged();
        });
    }
}

SnapSettings GridBar::settings() const {
    return SnapSettings{
        .enabled   = appsettings::snapToGrid(),
        .fixedStep = appsettings::snapFixedStep(),
        .divisions = appsettings::snapDivisions(),
        .timeStep  = appsettings::snapTimeStep(),
        .valueStep = appsettings::snapValueStep()
    };
}

void GridBar::showAutomaticStep(double timeStep, double valueStep) {
    if (appsettings::snapFixedStep()) {
        return;
    }
    timeStep_->setValue(timeStep);
    valueStep_->setValue(valueStep);
}

void GridBar::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    // The readout state of the step fields is painted with a color taken from the
    // palette, and setting a palette on a child stops it from following later
    // changes. Switching the theme therefore has to recompute it.
    if (event->type() == QEvent::PaletteChange) {
        updateFieldState();
    }
}

void GridBar::store() {
    const int selected = fineness_->currentData().toInt();
    appsettings::setSnapToGrid(enabled_->isChecked());
    appsettings::setSnapFixedStep(selected == FixedStepData);
    if (selected != FixedStepData) {
        appsettings::setSnapDivisions(selected);
    }
    appsettings::setSnapTimeStep(timeStep_->value());
    appsettings::setSnapValueStep(valueStep_->value());
}

void GridBar::updateFieldState() {
    // While the step follows the grid the two fields are a readout of it, not an
    // input: what to type there is decided by the fineness box.
    const bool fixed = fineness_->currentData().toInt() == FixedStepData;
    timeStep_->setReadOnly(! fixed);
    valueStep_->setReadOnly(! fixed);
    for (EngineeringEdit* edit : {timeStep_, valueStep_}) {
        QPalette fieldPalette = palette();
        fieldPalette.setColor(
            QPalette::Base, palette().color(fixed ? QPalette::Base : QPalette::Window)
        );
        edit->setPalette(fieldPalette);
    }
}

}  // namespace zwe
