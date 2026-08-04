// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "engineeringedit.h"

#include "engineeringnotation.h"

namespace zwe {

EngineeringEdit::EngineeringEdit(QWidget* parent) : QLineEdit(parent) {
    connect(this, &QLineEdit::textChanged, this, &EngineeringEdit::updateValidation);
    connect(this, &QLineEdit::editingFinished, this, &EngineeringEdit::commit);
    setValue(value_);
}

void EngineeringEdit::setValue(double value) {
    value_ = value;
    setText(engineering::format(value));
    updateValidation();
}

double EngineeringEdit::value() const {
    return value_;
}

void EngineeringEdit::updateValidation() {
    const bool valid = engineering::parse(text()).has_value();
    setStyleSheet(valid ? QString{} : QStringLiteral("QLineEdit { border: 1px solid #c62828; }"));
}

void EngineeringEdit::commit() {
    const auto parsed = engineering::parse(text());
    if (! parsed) {
        return;
    }
    value_ = *parsed;
    setText(engineering::format(value_));
    emit valueCommitted(value_);
}

}  // namespace zwe
