// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include <QUndoStack>
#include <QtTest>

#include "ui/commands.h"

using namespace zwe;

class CommandsTest final : public QObject {
    Q_OBJECT

private slots:
    void mergesConsecutiveEditsOfTheSameField();
    void keepsDifferentFieldsAsSeparateSteps();
};

void CommandsTest::mergesConsecutiveEditsOfTheSameField() {
    WaveDocument initial;
    WaveDocument first = initial;
    WaveDocument last  = initial;
    first.name         = QStringLiteral("First");
    last.name          = QStringLiteral("Last");

    WaveDocument current;
    QUndoStack stack;
    const auto apply = [&current](const WaveDocument& state, const DocumentSelection&) {
        current = state;
    };

    stack.push(new DocumentEditCommand(
        initial, first, {}, {}, apply, QStringLiteral("Edit name"), QStringLiteral("name")
    ));
    stack.push(new DocumentEditCommand(
        first, last, {}, {}, apply, QStringLiteral("Edit name"), QStringLiteral("name")
    ));

    QCOMPARE(stack.count(), 1);
    QVERIFY(current == last);
    stack.undo();
    QVERIFY(current == initial);
    stack.redo();
    QVERIFY(current == last);
}

void CommandsTest::keepsDifferentFieldsAsSeparateSteps() {
    WaveDocument initial;
    WaveDocument renamed = initial;
    WaveDocument sampled = renamed;
    renamed.name         = QStringLiteral("Renamed");
    sampled.sampleRate   = 2000.0;

    WaveDocument current;
    QUndoStack stack;
    const auto apply = [&current](const WaveDocument& state, const DocumentSelection&) {
        current = state;
    };

    stack.push(new DocumentEditCommand(
        initial, renamed, {}, {}, apply, QStringLiteral("Rename"), QStringLiteral("name")
    ));
    stack.push(new DocumentEditCommand(
        renamed, sampled, {}, {}, apply, QStringLiteral("Sample rate"), QStringLiteral("sampleRate")
    ));

    QCOMPARE(stack.count(), 2);
    stack.undo();
    QVERIFY(current == renamed);
    stack.undo();
    QVERIFY(current == initial);
}

QTEST_MAIN(CommandsTest)
#include "tst_commands.moc"
