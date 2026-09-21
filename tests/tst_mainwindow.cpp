// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include <QAction>
#include <QApplication>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QUndoStack>
#include <QWidgetAction>
#include <QtTest>
#include <algorithm>

#include "core/zwjio.h"
#include "ui/canvaswidget.h"
#include "ui/engineeringedit.h"
#include "ui/mainwindow.h"
#include "ui/structurepanel.h"

using namespace zwe;

class TestMainWindow : public QObject {
    Q_OBJECT

private slots:
    void propertyCommitSurvivesFocusChange();
    void toolbarAndPaletteFollowSafeSelection();
    void segmentInsertionMovementAndDockRecovery();
    void newDocumentStartsWithLayerAndAddFollowsSelection();
    void scaleWaveformDialogCreatesUndoCommand();
    void groupsTakeNewLayersAndDissolveBackIntoTheirLevel();
    void openingADocumentLeavesNothingToUndo();
    void savingAssignsADocumentIdOnceAndKeepsIt();
};

void TestMainWindow::openingADocumentLeavesNothingToUndo() {
    MainWindow window;
    QVERIFY(window.openDocument(QStringLiteral(ZWE_TEST_DATA_DIR "/example.zwj")));
    window.show();
    // Property editors commit through a zero-timer, so a freshly opened
    // document only counts as untouched once those have run.
    QApplication::processEvents();
    QCoreApplication::sendPostedEvents();
    QApplication::processEvents();

    auto* undoStack = window.findChild<QUndoStack*>();
    QVERIFY(undoStack);
    QCOMPARE(undoStack->count(), 0);
    QVERIFY2(
        ! window.windowTitle().contains(QLatin1Char('*')), qPrintable(window.windowTitle())
    );

    // The same for a document whose first layer is a group, where the property
    // panel opens its layer form instead of a segment form.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("group.zwj"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"format":"zahner-wave","version":2,"name":"Grouped","description":"",
        "sample_rate":1000,"layers":[
            {"name":"Burst","enabled":true,"kind":"group","mode":"add","edge":"neutral",
             "children":[
                {"name":"Carrier","enabled":true,"kind":"layer","mode":"add","edge":"neutral",
                 "segments":[{"type":"sine","duration":0.05,"repeat":2,"params":{
                    "amplitude":1,"frequency":200,"phase_deg":0,"offset":0}}]},
                {"name":"Envelope","enabled":true,"kind":"layer","mode":"multiply","edge":"loop",
                 "segments":[{"type":"window","duration":0.05,"repeat":1,"params":{
                    "shape":"hann","amplitude":1,"offset":0}}]}]}
        ]})");
    file.close();

    QVERIFY(window.openDocument(path));
    QApplication::processEvents();
    QCoreApplication::sendPostedEvents();
    QApplication::processEvents();
    QCOMPARE(undoStack->count(), 0);
    QVERIFY2(
        ! window.windowTitle().contains(QLatin1Char('*')), qPrintable(window.windowTitle())
    );
}

void TestMainWindow::propertyCommitSurvivesFocusChange() {
    MainWindow window;
    QVERIFY(window.openDocument(QStringLiteral(ZWE_TEST_DATA_DIR "/example.zwj")));
    window.show();
    QApplication::processEvents();

    // Scoped to the property dock on purpose: the grid bar above the plot holds
    // engineering fields of its own, and those are not rebuilt on focus changes.
    auto* properties = window.findChild<QScrollArea*>(QStringLiteral("propertyScrollArea"));
    QVERIFY(properties);
    QPointer<EngineeringEdit> edit = properties->findChild<EngineeringEdit*>();
    auto* canvas                   = window.findChild<CanvasWidget*>();
    QVERIFY(edit);
    QVERIFY(canvas);

    edit->setFocus();
    edit->selectAll();
    QTest::keyClicks(edit, QStringLiteral("2"));
    canvas->setFocus();
    QApplication::processEvents();

    // The editor is rebuilt after the focus event has returned, rather than
    // being destroyed from inside its own focusOutEvent: the old editor is
    // only released via deleteLater(), i.e. once deferred deletes run.
    QVERIFY(! edit.isNull());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(edit.isNull());
    QVERIFY(properties->findChild<EngineeringEdit*>());
}

void TestMainWindow::segmentInsertionMovementAndDockRecovery() {
    QSettings().remove(QStringLiteral("mainWindow/state"));
    MainWindow window;
    QVERIFY(window.openDocument(QStringLiteral(ZWE_TEST_DATA_DIR "/example.zwj")));
    window.show();
    QApplication::processEvents();

    auto* tree           = window.findChild<QTreeWidget*>();
    auto* insertAfter    = window.findChild<QAction*>(QStringLiteral("insertAfterAction"));
    auto* moveEarlier    = window.findChild<QAction*>(QStringLiteral("moveEarlierAction"));
    auto* propertyDock   = window.findChild<QDockWidget*>(QStringLiteral("propertyDock"));
    auto* propertyScroll = window.findChild<QScrollArea*>(QStringLiteral("propertyScrollArea"));
    auto* undoStack      = window.findChild<QUndoStack*>();
    QVERIFY(tree);
    QVERIFY(insertAfter);
    QVERIFY(moveEarlier);
    QVERIFY(propertyDock);
    QVERIFY(propertyScroll);
    QVERIFY(propertyScroll->widgetResizable());
    QVERIFY(undoStack);
    QCOMPARE(window.dockWidgetArea(propertyDock), Qt::RightDockWidgetArea);

    auto* layer = tree->topLevelItem(0);
    QVERIFY(layer);
    const int originalCount     = layer->childCount();
    const QString originalFirst = layer->child(0)->text(0);
    tree->setCurrentItem(layer->child(0));
    QVERIFY(insertAfter->isEnabled());
    auto* basicCategory = insertAfter->menu()->actions().front()->menu();
    QVERIFY(basicCategory);
    basicCategory->actions().front()->trigger();
    layer = tree->topLevelItem(0);
    QCOMPARE(layer->childCount(), originalCount + 1);
    QCOMPARE(layer->child(1)->text(0), QStringLiteral("DC"));
    QVERIFY(! layer->child(1)->icon(0).isNull());

    QVERIFY(moveEarlier->isEnabled());
    moveEarlier->trigger();
    layer = tree->topLevelItem(0);
    QCOMPARE(layer->child(0)->text(0), QStringLiteral("DC"));
    undoStack->undo();
    layer = tree->topLevelItem(0);
    QCOMPARE(layer->child(0)->text(0), originalFirst);

    propertyDock->hide();
    QVERIFY(! propertyDock->isVisible());
    propertyDock->toggleViewAction()->trigger();
    QVERIFY(propertyDock->isVisible());
}

void TestMainWindow::toolbarAndPaletteFollowSafeSelection() {
    MainWindow window;
    QVERIFY(window.openDocument(QStringLiteral(ZWE_TEST_DATA_DIR "/example.zwj")));

    auto* toolbar    = window.findChild<QToolBar*>(QStringLiteral("fileToolbar"));
    auto* add        = window.findChild<QAction*>(QStringLiteral("addSegmentAction"));
    auto* remove     = window.findChild<QAction*>(QStringLiteral("removeSegmentAction"));
    auto* tree       = window.findChild<QTreeWidget*>();
    auto* sampleRate = window.findChild<QSpinBox*>(QStringLiteral("toolbarSampleRateSpin"));
    QVERIFY(toolbar);
    QVERIFY(add);
    QVERIFY(remove);
    QVERIFY(tree);
    QVERIFY(sampleRate);
    QCOMPARE(sampleRate->value(), 10000.0);
    QVERIFY(! window.findChild<EngineeringEdit*>(QStringLiteral("sampleRateEdit")));
    QVERIFY(! window.findChild<EngineeringEdit*>(QStringLiteral("outputDataRateEdit")));
    QCOMPARE(toolbar->toolButtonStyle(), Qt::ToolButtonIconOnly);
    QVERIFY(! toolbar->isMovable());
    QVERIFY(! toolbar->isFloatable());
    for (QAction* action : toolbar->actions()) {
        if (! action->isSeparator() && ! qobject_cast<QWidgetAction*>(action)) {
            QVERIFY2(! action->icon().isNull(), qPrintable(action->text()));
            QVERIFY(! action->toolTip().isEmpty());
            QVERIFY(! action->statusTip().isEmpty());
        }
    }

    QVERIFY(add->isEnabled());
    QVERIFY(remove->isEnabled());
    tree->setCurrentItem(nullptr);
    QVERIFY(! add->isEnabled());
    QVERIFY(! remove->isEnabled());
    tree->setCurrentItem(tree->topLevelItem(0));
    QVERIFY(add->isEnabled());
    QVERIFY(! remove->isEnabled());

    QList<QAction*> waveformActions;
    for (QAction* categoryAction : add->menu()->actions()) {
        if (! categoryAction->menu()) {
            continue;
        }
        for (QAction* waveformAction : categoryAction->menu()->actions()) {
            waveformActions.push_back(waveformAction);
        }
    }
    QCOMPARE(waveformActions.size(), 15);
    const auto offers = [&waveformActions](const QString& label) {
        return std::any_of(
            waveformActions.cbegin(),
            waveformActions.cend(),
            [&label](const QAction* action) { return action->text() == label; }
        );
    };
    QVERIFY(offers(QStringLiteral("Chirp")));
    QVERIFY(offers(QStringLiteral("Ricker wavelet")));
    QVERIFY(offers(QStringLiteral("Normal Pulse Voltammetry")));
    QVERIFY(offers(QStringLiteral("Differential Pulse Voltammetry")));
    QVERIFY(offers(QStringLiteral("Square Wave Voltammetry")));

    // The tree labels a segment more tersely than the menu, which has the room
    // for a spelled-out method name.
    const QHash<QString, QString> treeLabel = {
        {QStringLiteral("Hold / DC"), QStringLiteral("DC")},
        {QStringLiteral("Normal Pulse Voltammetry"), QStringLiteral("NPV")},
        {QStringLiteral("Differential Pulse Voltammetry"), QStringLiteral("DPV")},
        {QStringLiteral("Square Wave Voltammetry"), QStringLiteral("SWV")},
    };
    const int oldCount = tree->topLevelItem(0)->childCount();
    for (QAction* waveformAction : waveformActions) {
        waveformAction->trigger();
        QCOMPARE(tree->topLevelItem(0)->childCount(), oldCount + 1);
        QCOMPARE(
            tree->topLevelItem(0)->child(oldCount)->text(0),
            treeLabel.value(waveformAction->text(), waveformAction->text())
        );
        QVERIFY(remove->isEnabled());
        remove->trigger();
        QCOMPARE(tree->topLevelItem(0)->childCount(), oldCount);
        QVERIFY(! remove->isEnabled());
        tree->setCurrentItem(tree->topLevelItem(0));
    }
}

void TestMainWindow::newDocumentStartsWithLayerAndAddFollowsSelection() {
    MainWindow window;
    auto* newAction = window.findChild<QAction*>(QStringLiteral("newAction"));
    auto* add       = window.findChild<QAction*>(QStringLiteral("addSegmentAction"));
    auto* tree      = window.findChild<QTreeWidget*>();
    QVERIFY(newAction);
    QVERIFY(add);
    QVERIFY(tree);

    newAction->trigger();
    QCOMPARE(tree->topLevelItemCount(), 1);
    auto* layer = tree->topLevelItem(0);
    QCOMPARE(layer->childCount(), 0);
    QVERIFY(add->isEnabled());

    auto* basic = add->menu()->actions().front()->menu();
    QVERIFY(basic);
    basic->actions().front()->trigger();
    layer = tree->topLevelItem(0);
    basic->actions().at(1)->trigger();
    layer = tree->topLevelItem(0);
    QCOMPARE(layer->childCount(), 2);
    QCOMPARE(layer->child(0)->text(0), QStringLiteral("DC"));
    QCOMPARE(layer->child(1)->text(0), QStringLiteral("Ramp"));

    tree->setCurrentItem(layer->child(0));
    basic->actions().at(1)->trigger();
    layer = tree->topLevelItem(0);
    QCOMPARE(layer->childCount(), 3);
    QCOMPARE(layer->child(0)->text(0), QStringLiteral("DC"));
    QCOMPARE(layer->child(1)->text(0), QStringLiteral("Ramp"));
}

void TestMainWindow::scaleWaveformDialogCreatesUndoCommand() {
    MainWindow window;
    QVERIFY(window.openDocument(QStringLiteral(ZWE_TEST_DATA_DIR "/example.zwj")));
    window.show();
    QApplication::processEvents();

    auto* action    = window.findChild<QAction*>(QStringLiteral("scaleWaveformAction"));
    auto* undoStack = window.findChild<QUndoStack*>();
    QVERIFY(action);
    QVERIFY(undoStack);
    const int undoCount = undoStack->count();

    QTimer::singleShot(0, [&] {
        auto* dialog = QApplication::activeModalWidget();
        QVERIFY(dialog);
        auto* amplitude = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("amplitudeScaleSpin"));
        auto* time      = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("timeScaleSpin"));
        auto* buttons   = dialog->findChild<QDialogButtonBox*>();
        QVERIFY(amplitude);
        QVERIFY(time);
        QVERIFY(buttons);
        amplitude->setValue(2.0);
        time->setValue(3.0);
        buttons->button(QDialogButtonBox::Ok)->click();
    });

    action->trigger();
    QCOMPARE(undoStack->count(), undoCount + 1);
    QCOMPARE(undoStack->undoText(), QStringLiteral("Scale Entire Waveform"));
}

void TestMainWindow::groupsTakeNewLayersAndDissolveBackIntoTheirLevel() {
    MainWindow window;
    window.show();
    QApplication::processEvents();

    auto* tree      = window.findChild<QTreeWidget*>();
    auto* structure = window.findChild<StructurePanel*>();
    auto* addLayer  = window.findChild<QAction*>(QStringLiteral("addLayerAction"));
    auto* addGroup  = window.findChild<QAction*>(QStringLiteral("addGroupAction"));
    auto* addSegment = window.findChild<QAction*>(QStringLiteral("addSegmentAction"));
    auto* undoStack = window.findChild<QUndoStack*>();
    QVERIFY(tree);
    QVERIFY(structure);
    QVERIFY(addLayer);
    QVERIFY(addGroup);
    QVERIFY(addSegment);
    QVERIFY(undoStack);
    QCOMPARE(tree->topLevelItemCount(), 1);  // the layer a new document starts with

    addGroup->trigger();
    QCOMPARE(tree->topLevelItemCount(), 2);
    QTreeWidgetItem* groupItem = tree->topLevelItem(1);
    QVERIFY(groupItem);
    QCOMPARE(groupItem->childCount(), 0);
    QVERIFY(! groupItem->icon(0).isNull());
    // A group holds layers, not segments, so the segment actions are off while
    // it is selected - and the freshly added group is what is selected.
    QVERIFY(! addSegment->isEnabled());

    // A new layer goes into the selected group rather than to the top level.
    addLayer->trigger();
    QCOMPARE(tree->topLevelItemCount(), 2);
    groupItem = tree->topLevelItem(1);
    QCOMPARE(groupItem->childCount(), 1);
    QVERIFY(addSegment->isEnabled());  // the new leaf inside the group is selected
    addSegment->menu()->actions().front()->menu()->actions().front()->trigger();
    QCOMPARE(tree->topLevelItem(1)->child(0)->childCount(), 1);

    // Wrapping a top-level layer puts a group in its place, holding it.
    emit structure->groupLayerRequested({0});
    QCOMPARE(undoStack->undoText(), QStringLiteral("Group Layer"));
    QCOMPARE(tree->topLevelItemCount(), 2);
    QCOMPARE(tree->topLevelItem(0)->childCount(), 1);

    // Dissolving it puts the children back where the group was.
    emit structure->ungroupRequested({0});
    QCOMPARE(undoStack->undoText(), QStringLiteral("Ungroup"));
    QCOMPARE(tree->topLevelItemCount(), 2);
    QCOMPARE(tree->topLevelItem(0)->childCount(), 0);  // the plain layer again

    undoStack->undo();
    QCOMPARE(tree->topLevelItem(0)->childCount(), 1);  // the group is back
}

QTEST_MAIN(TestMainWindow)
void TestMainWindow::savingAssignsADocumentIdOnceAndKeepsIt() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("doc.zwj"));
    QVERIFY(QFile::copy(QStringLiteral(ZWE_TEST_DATA_DIR "/example.zwj"), path));
    QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner));

    const zwjio::LoadResult before = zwjio::load(path);
    QVERIFY(before.document.has_value());
    QVERIFY(before.document->id.isEmpty());

    QString assigned;
    {
        MainWindow window;
        QVERIFY(window.openDocument(path));
        QVERIFY(QMetaObject::invokeMethod(&window, "saveDocument"));

        const zwjio::LoadResult saved = zwjio::load(path);
        QVERIFY2(
            ! saved.error.has_value(), qPrintable(saved.error.value_or(zwjio::Error{}).message)
        );
        QVERIFY(saved.document.has_value());
        assigned = saved.document->id;
        QVERIFY(! assigned.isEmpty());
    }

    {
        MainWindow window;
        QVERIFY(window.openDocument(path));
        QVERIFY(QMetaObject::invokeMethod(&window, "saveDocument"));

        const zwjio::LoadResult resaved = zwjio::load(path);
        QVERIFY(resaved.document.has_value());
        QCOMPARE(resaved.document->id, assigned);
    }
}

#include "tst_mainwindow.moc"
