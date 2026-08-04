// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include <QAction>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMenu>
#include <QMimeData>
#include <QSignalSpy>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtTest>

#include "ui/structurepanel.h"

using namespace zwe;

class TestStructurePanel : public QObject {
    Q_OBJECT

private slots:
    void segmentActionsRequireValidSelection();
    void clearingSelectionRemovesCurrentItem();
    void layerCheckboxMayRebuildTreeFromChangeHandler();
    void groupsNestInTheTreeAndReportTheirPath();
    void dropsThatWouldBreakTheStructureAreRefused();
    void clickingBesideTheItemsSelectsNothing();
    void layerIconsCarryTheirCurveColor();
};

namespace {

WaveDocument nestedDocument() {
    WaveDocument document;
    WaveLayer leaf;
    leaf.name = QStringLiteral("Carrier");
    leaf.segments.push_back(defaultSegment(SegmentType::Dc));

    WaveLayer child;
    child.name = QStringLiteral("Envelope");
    child.mode = LayerMode::Multiply;
    child.segments.push_back(defaultSegment(SegmentType::Window));

    WaveLayer group;
    group.name     = QStringLiteral("Group");
    group.kind     = LayerKind::Group;
    group.children = {child};

    document.layers = {leaf, group};
    return document;
}

}  // namespace

void TestStructurePanel::segmentActionsRequireValidSelection() {
    StructurePanel panel;
    QAction addLayer(QStringLiteral("Add Layer"), &panel);
    QAction addSegmentAction(QStringLiteral("Add Segment"), &panel);
    QAction removeSegmentAction(QStringLiteral("Remove Segment"), &panel);
    QMenu segmentMenu;
    addSegmentAction.setMenu(&segmentMenu);
    panel.setStructureActions(&addLayer, &addSegmentAction, &removeSegmentAction);
    auto* addSegment    = panel.findChild<QToolButton*>(QStringLiteral("addSegmentButton"));
    auto* removeSegment = panel.findChild<QToolButton*>(QStringLiteral("removeSegmentButton"));
    QVERIFY(addSegment);
    QVERIFY(removeSegment);
    auto* rootLayout = qobject_cast<QVBoxLayout*>(panel.layout());
    QVERIFY(rootLayout);
    QCOMPARE(rootLayout->indexOf(panel.findChild<QTreeWidget*>()), 1);

    addSegmentAction.setEnabled(false);
    removeSegmentAction.setEnabled(false);
    QVERIFY(! addSegment->isEnabled());
    QVERIFY(! removeSegment->isEnabled());

    addSegmentAction.setEnabled(true);
    QVERIFY(addSegment->isEnabled());
    QVERIFY(! removeSegment->isEnabled());
    removeSegmentAction.setEnabled(true);
    QVERIFY(removeSegment->isEnabled());
}


void TestStructurePanel::clearingSelectionRemovesCurrentItem() {
    StructurePanel panel;
    WaveDocument document;
    document.layers.push_back({});
    document.layers.front().segments.push_back(defaultSegment(SegmentType::Dc));
    panel.setDocument(document);
    panel.setSelection({0}, 0);

    panel.setSelection({}, std::nullopt);
    auto* tree = panel.findChild<QTreeWidget*>();
    QVERIFY(tree);
    QVERIFY(! tree->currentItem());
}

void TestStructurePanel::layerCheckboxMayRebuildTreeFromChangeHandler() {
    StructurePanel panel;
    WaveDocument document;
    document.layers.push_back({});
    panel.setDocument(document);
    panel.show();

    auto* tree = panel.findChild<QTreeWidget*>();
    QVERIFY(tree);
    auto* layerItem = tree->topLevelItem(0);
    QVERIFY(layerItem);
    const QRect itemRect = tree->visualItemRect(layerItem);
    QVERIFY(itemRect.isValid());

    bool handlerRan = false;
    connect(
        &panel,
        &StructurePanel::layerEnabledChanged,
        &panel,
        [&](const LayerPath& layerPath, bool enabled) {
            handlerRan = true;
            QCOMPARE(layerPath, LayerPath{0});
            layerAtPath(document.layers, layerPath)->enabled = enabled;
            // Mirrors MainWindow's response. The tree rebuild inside
            // setDocument() deletes the item the delegate is still working on.
            panel.setDocument(document);
        }
    );

    QTest::mouseClick(
        tree->viewport(),
        Qt::LeftButton,
        Qt::NoModifier,
        QPoint(itemRect.left() + 8, itemRect.center().y())
    );
    QTRY_VERIFY(handlerRan);
    QCOMPARE(tree->topLevelItemCount(), 1);
    QCOMPARE(tree->topLevelItem(0)->checkState(0), Qt::Unchecked);
}

void TestStructurePanel::groupsNestInTheTreeAndReportTheirPath() {
    StructurePanel panel;
    panel.setDocument(nestedDocument());
    panel.show();
    auto* tree = panel.findChild<QTreeWidget*>();
    QVERIFY(tree);

    QCOMPARE(tree->topLevelItemCount(), 2);
    QTreeWidgetItem* groupItem = tree->topLevelItem(1);
    QCOMPARE(groupItem->text(0), QStringLiteral("Group"));
    QCOMPARE(groupItem->childCount(), 1);
    // A multiplying node says so in the tree, because the order of a level only
    // matters once something multiplies.
    QCOMPARE(groupItem->child(0)->text(0), QStringLiteral("Envelope ×"));
    QCOMPARE(groupItem->child(0)->childCount(), 1);  // its window segment
    QVERIFY(! groupItem->child(0)->toolTip(0).isEmpty());
    // A missing icon resource yields a null QIcon without any other complaint,
    // so the group and the window segment say they found theirs.
    QVERIFY(! groupItem->icon(0).isNull());
    QVERIFY(! groupItem->child(0)->child(0)->icon(0).isNull());

    QSignalSpy selection(&panel, &StructurePanel::selectionChanged);
    tree->setCurrentItem(groupItem->child(0)->child(0));
    QCOMPARE(selection.count(), 1);
    QList<QVariant> arguments = selection.takeFirst();
    QCOMPARE(qvariant_cast<LayerPath>(arguments.at(0)), (LayerPath{1, 0}));
    QCOMPARE(qvariant_cast<std::optional<size_t>>(arguments.at(1)), std::optional<size_t>{0});

    // Selecting the group itself reports no segment.
    tree->setCurrentItem(groupItem);
    QCOMPARE(selection.count(), 1);
    arguments = selection.takeFirst();
    QCOMPARE(qvariant_cast<LayerPath>(arguments.at(0)), LayerPath{1});
    QCOMPARE(qvariant_cast<std::optional<size_t>>(arguments.at(1)), std::optional<size_t>{});

    // A path set from outside finds the same items back.
    panel.setSelection({1, 0}, 0);
    QCOMPARE(tree->currentItem(), groupItem->child(0)->child(0));
    panel.setSelection({1}, std::nullopt);
    QCOMPARE(tree->currentItem(), groupItem);
}

void TestStructurePanel::dropsThatWouldBreakTheStructureAreRefused() {
    // The rule is checked directly rather than through a simulated drag: an
    // internal move only starts from a drag that QAbstractItemView itself
    // began, so synthetic drag events never reach the handlers.
    StructurePanel panel;
    WaveDocument document = nestedDocument();
    WaveLayer other;
    other.name = QStringLiteral("Other");
    document.layers.push_back(other);
    panel.setDocument(document);
    auto* tree = panel.findChild<QTreeWidget*>();
    QVERIFY(tree);

    QTreeWidgetItem* leafItem    = tree->topLevelItem(0);
    QTreeWidgetItem* segmentItem = leafItem->child(0);
    QTreeWidgetItem* groupItem   = tree->topLevelItem(1);
    QTreeWidgetItem* childItem   = groupItem->child(0);
    QTreeWidgetItem* otherLeaf   = tree->topLevelItem(2);
    QVERIFY(segmentItem);
    QVERIFY(childItem);
    QVERIFY(otherLeaf);

    // A segment belongs into a leaf layer - its own or another one - and
    // nowhere else: a group has no segment chain, the top level none either.
    QVERIFY(StructurePanel::isValidDropTarget(segmentItem, leafItem));
    QVERIFY(StructurePanel::isValidDropTarget(segmentItem, otherLeaf));
    QVERIFY(StructurePanel::isValidDropTarget(segmentItem, childItem));
    QVERIFY(! StructurePanel::isValidDropTarget(segmentItem, groupItem));
    QVERIFY(! StructurePanel::isValidDropTarget(segmentItem, nullptr));

    // A layer goes to the top level or into a group, never into a leaf layer.
    QVERIFY(StructurePanel::isValidDropTarget(leafItem, nullptr));
    QVERIFY(StructurePanel::isValidDropTarget(leafItem, groupItem));
    QVERIFY(! StructurePanel::isValidDropTarget(leafItem, otherLeaf));
    QVERIFY(! StructurePanel::isValidDropTarget(childItem, leafItem));

    // A group must not be dropped into its own subtree.
    QVERIFY(StructurePanel::isValidDropTarget(groupItem, nullptr));
    QVERIFY(! StructurePanel::isValidDropTarget(groupItem, groupItem));
    QVERIFY(! StructurePanel::isValidDropTarget(groupItem, childItem));
    QVERIFY(! StructurePanel::isValidDropTarget(nullptr, groupItem));
}

void TestStructurePanel::clickingBesideTheItemsSelectsNothing() {
    StructurePanel panel;
    panel.resize(260, 420);
    panel.setDocument(nestedDocument());
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    auto* tree = panel.findChild<QTreeWidget*>();
    QVERIFY(tree);
    tree->setCurrentItem(tree->topLevelItem(0));

    // Well below the last row, so the click lands on nothing.
    const QPoint empty(12, tree->viewport()->height() - 6);
    QVERIFY(! tree->itemAt(empty));

    QSignalSpy selection(&panel, &StructurePanel::selectionChanged);
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, empty);
    QCOMPARE(selection.count(), 1);
    QCOMPARE(qvariant_cast<LayerPath>(selection.first().at(0)), (LayerPath{}));
    QVERIFY(! tree->currentItem());
}

void TestStructurePanel::layerIconsCarryTheirCurveColor() {
    StructurePanel panel;
    panel.setDocument(nestedDocument());
    panel.show();
    auto* tree = panel.findChild<QTreeWidget*>();
    QVERIFY(tree);

    QTreeWidgetItem* carrier   = tree->topLevelItem(0);
    QTreeWidgetItem* groupItem = tree->topLevelItem(1);
    QTreeWidgetItem* envelope  = groupItem->child(0);
    const QImage first         = carrier->icon(0).pixmap(24, 24).toImage();
    const QImage second        = envelope->icon(0).pixmap(24, 24).toImage();
    QVERIFY(! first.isNull());
    QVERIFY(! second.isNull());
    // Same glyph, different color: that is what connects each layer to its curve
    // in the plot, and it has to survive a theme change.
    QVERIFY(first != second);
    panel.refreshIcons();
    QCOMPARE(carrier->icon(0).pixmap(24, 24).toImage(), first);
    QCOMPARE(envelope->icon(0).pixmap(24, 24).toImage(), second);
    // A group has no curve, so it stays neutral and differs from both.
    QVERIFY(groupItem->icon(0).pixmap(24, 24).toImage() != first);
}

QTEST_MAIN(TestStructurePanel)
#include "tst_structurepanel.moc"
