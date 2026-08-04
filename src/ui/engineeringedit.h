// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QLineEdit>

namespace zwe {

class EngineeringEdit final : public QLineEdit {
    Q_OBJECT

public:
    explicit EngineeringEdit(QWidget* parent = nullptr);

    void setValue(double value);
    double value() const;

signals:
    void valueCommitted(double value);

private:
    void updateValidation();
    void commit();

    double value_ = 0.0;
};

}  // namespace zwe
