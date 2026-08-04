// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "structurepanel.h"

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QMenu>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

#include "theme.h"

namespace zwe {

namespace {

constexpr int ItemKindRole    = Qt::UserRole;
// The path the item was built from. A drag & drop moves items around without
// touching their data, so this still names where the node came from once the
// tree no longer matches the document.
constexpr int SourcePathRole  = Qt::UserRole + 1;
constexpr int SourceIndexRole = Qt::UserRole + 2;
// Which curve of the canvas this item belongs to: the leaf's own number for a
// layer, its owner's for a segment, absent for a group. Stored while the tree is
// built - the numbering is the depth-first order of the leaves, which the tree
// walks anyway, and refreshIcons() must not have to derive it a second time.
constexpr int LeafIndexRole   = Qt::UserRole + 3;
constexpr int LayerItemKind   = 1;
constexpr int SegmentItemKind = 2;
constexpr int GroupItemKind   = 3;

int itemKind(const QTreeWidgetItem* item) {
    return item ? item->data(0, ItemKindRole).toInt() : 0;
}

QVariant pathToVariant(const LayerPath& path) {
    QVariantList list;
    for (const size_t index : path) {
        list.append(static_cast<qulonglong>(index));
    }
    return list;
}

LayerPath pathFromVariant(const QVariant& value) {
    LayerPath path;
    const QVariantList list = value.toList();
    path.reserve(static_cast<size_t>(list.size()));
    for (const QVariant& entry : list) {
        path.push_back(entry.toULongLong());
    }
    return path;
}

QString segmentName(SegmentType type) {
    switch (type) {
        case SegmentType::Dc:
            return StructurePanel::tr("DC");
        case SegmentType::Ramp:
            return StructurePanel::tr("Ramp");
        case SegmentType::Sine:
            return StructurePanel::tr("Sine");
        case SegmentType::Square:
            return StructurePanel::tr("Square");
        case SegmentType::Triangle:
            return StructurePanel::tr("Triangle");
        case SegmentType::Pulse:
            return StructurePanel::tr("Pulse");
        case SegmentType::Exponential:
            return StructurePanel::tr("Exponential");
        case SegmentType::Formula:
            return StructurePanel::tr("Formula");
        case SegmentType::Points:
            return StructurePanel::tr("Points");
        case SegmentType::Chirp:
            return StructurePanel::tr("Chirp");
        case SegmentType::Ricker:
            return StructurePanel::tr("Ricker wavelet");
        case SegmentType::Window:
            return StructurePanel::tr("Window");
        case SegmentType::Npv:
            return StructurePanel::tr("NPV");
        case SegmentType::Swv:
            return StructurePanel::tr("SWV");
        case SegmentType::Dpv:
            return StructurePanel::tr("DPV");
    }
    return {};
}

QString segmentIconName(SegmentType type) {
    switch (type) {
        case SegmentType::Dc:
            return QStringLiteral("wave-dc");
        case SegmentType::Ramp:
            return QStringLiteral("wave-ramp");
        case SegmentType::Sine:
            return QStringLiteral("wave-sine");
        case SegmentType::Square:
            return QStringLiteral("wave-square");
        case SegmentType::Triangle:
            return QStringLiteral("wave-triangle");
        case SegmentType::Pulse:
            return QStringLiteral("wave-pulse");
        case SegmentType::Exponential:
            return QStringLiteral("wave-exponential");
        case SegmentType::Formula:
            return QStringLiteral("wave-formula");
        case SegmentType::Points:
            return QStringLiteral("wave-points");
        case SegmentType::Chirp:
            return QStringLiteral("wave-chirp");
        case SegmentType::Ricker:
            return QStringLiteral("wave-ricker");
        case SegmentType::Window:
            return QStringLiteral("wave-window");
        case SegmentType::Npv:
            return QStringLiteral("wave-npv");
        case SegmentType::Swv:
            return QStringLiteral("wave-swv");
        case SegmentType::Dpv:
            return QStringLiteral("wave-dpv");
    }
    return {};
}

QString layerModeName(LayerMode mode) {
    return mode == LayerMode::Multiply ? StructurePanel::tr("Multiply")
                                       : StructurePanel::tr("Add");
}

QString layerEdgeName(LayerEdge edge) {
    switch (edge) {
        case LayerEdge::Neutral:
            return StructurePanel::tr("neutral");
        case LayerEdge::Zero:
            return StructurePanel::tr("zero");
        case LayerEdge::HoldLast:
            return StructurePanel::tr("hold last");
        case LayerEdge::Loop:
            return StructurePanel::tr("loop");
    }
    return {};
}

// The order of a level matters as soon as one node multiplies, so a multiplying
// node says so right in the tree instead of only in the property panel.
QString layerLabel(const WaveLayer& layer, size_t index) {
    QString label = layer.name;
    if (label.isEmpty()) {
        label = isGroup(layer) ? StructurePanel::tr("Group %1").arg(index + 1)
                               : StructurePanel::tr("Layer %1").arg(index + 1);
    }
    if (layer.mode == LayerMode::Multiply) {
        label += QStringLiteral(" ×");
    }
    return label;
}

// A segment icon draws its waveform in an accent color, which is replaced; the
// layer and group glyphs are drawn in a neutral gray, which has to be tinted as
// a whole to show a color at all.
QIcon treeIcon(const QString& name, const QColor& color, bool tint = false) {
    const QIcon icon(QStringLiteral(":/icons/toolbar/%1.svg").arg(name));
    return tint ? theme::tintedIcon(icon, color) : theme::accentIcon(icon, color);
}

// A layer and its segments carry the color the canvas draws that layer's curve
// in. A group has no curve of its own, so it keeps the neutral glyph - which is
// also what tells a group from a layer at a glance.
QColor itemColor(const QTreeWidgetItem* item) {
    const QPalette palette   = QApplication::palette();
    const QVariant leafIndex = item ? item->data(0, LeafIndexRole) : QVariant();
    return leafIndex.isValid() ? theme::curveColor(leafIndex.toULongLong(), palette)
                               : palette.color(QPalette::Highlight);
}

class StructureTreeWidget final : public QTreeWidget {
public:
    explicit StructureTreeWidget(QWidget* parent = nullptr) : QTreeWidget(parent) {}

    std::function<void()> reordered;

protected:
    // A click next to the items selects nothing, the same way a click on the
    // plot background does. Qt keeps the current item in that case, which would
    // leave the plot emphasizing a layer the user has just clicked away from.
    void mousePressEvent(QMouseEvent* event) override {
        if (! indexAt(event->position().toPoint()).isValid()) {
            setCurrentItem(nullptr);
            clearSelection();
        }
        QTreeWidget::mousePressEvent(event);
    }

    void dragMoveEvent(QDragMoveEvent* event) override {
        // The base class updates the drop indicator this decision is based on.
        QTreeWidget::dragMoveEvent(event);
        if (event->isAccepted() && ! dropAllowed(event->position().toPoint())) {
            event->ignore();
        }
    }

    void dropEvent(QDropEvent* event) override {
        if (! dropAllowed(event->position().toPoint())) {
            event->ignore();
            return;
        }
        QTreeWidget::dropEvent(event);
        if (reordered) {
            reordered();
        }
    }

private:
    // Where the item under the cursor would put the dragged one, then the rule
    // itself - which lives in StructurePanel so it can be checked directly.
    bool dropAllowed(const QPoint& position) const {
        const QList<QTreeWidgetItem*> dragged = selectedItems();
        if (dragged.size() != 1) {
            return false;
        }
        QTreeWidgetItem* target = itemAt(position);
        QTreeWidgetItem* parent = nullptr;
        if (target) {
            parent = dropIndicatorPosition() == QAbstractItemView::OnItem ? target
                                                                         : target->parent();
        }
        return StructurePanel::isValidDropTarget(dragged.constFirst(), parent);
    }
};

}  // namespace

StructurePanel::StructurePanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    auto* buttons = new QHBoxLayout;
    addLayer_     = new QToolButton(this);
    addGroup_     = new QToolButton(this);
    addGroup_->setObjectName(QStringLiteral("addGroupButton"));
    addGroup_->hide();
    addSegment_ = new QToolButton(this);
    addSegment_->setObjectName(QStringLiteral("addSegmentButton"));
    moveEarlier_ = new QToolButton(this);
    moveEarlier_->setObjectName(QStringLiteral("moveSegmentUpButton"));
    moveLater_ = new QToolButton(this);
    moveLater_->setObjectName(QStringLiteral("moveSegmentDownButton"));
    remove_ = new QToolButton(this);
    remove_->setObjectName(QStringLiteral("removeSegmentButton"));
    buttons->addWidget(addLayer_);
    buttons->addWidget(addGroup_);
    buttons->addWidget(addSegment_);
    buttons->addWidget(moveEarlier_);
    buttons->addWidget(moveLater_);
    buttons->addWidget(remove_);
    buttons->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum));

    layout->addLayout(buttons);

    tree_ = new StructureTreeWidget(this);
    tree_->setHeaderHidden(true);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->setDragDropMode(QAbstractItemView::InternalMove);
    tree_->setDefaultDropAction(Qt::MoveAction);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(tree_);

    connect(tree_, &QTreeWidget::itemSelectionChanged, this, &StructurePanel::emitSelection);
    connect(tree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        const int kind = itemKind(item);
        if (rebuilding_ || column != 0 ||
            (kind != LayerItemKind && kind != GroupItemKind)) {
            return;
        }
        const LayerPath path = pathFromVariant(item->data(0, SourcePathRole));
        const bool enabled   = item->checkState(0) == Qt::Checked;

        // A checkbox is changed from QStyledItemDelegate::editorEvent().  Emitting
        // synchronously lets a receiver call setDocument(), whose tree rebuild
        // deletes `item` while the delegate is still handling the mouse release.
        // Leave the delegate stack first; using this panel as the timer context
        // also cancels the callback if the panel is destroyed in the meantime.
        QTimer::singleShot(0, this, [this, path, enabled] {
            if (layerAtPath(document_.layers, path)) {
                emit layerEnabledChanged(path, enabled);
            }
        });
    });
    connect(tree_, &QWidget::customContextMenuRequested, this, &StructurePanel::showContextMenu);
    static_cast<StructureTreeWidget*>(tree_)->reordered = [this] { emitReorderedDocument(); };
    updateActionState();
}

void StructurePanel::setStructureActions(
    QAction* addLayer,
    QAction* addGroup,
    QAction* addSegment,
    QAction* insertBefore,
    QAction* insertAfter,
    QAction* moveEarlier,
    QAction* moveLater,
    QAction* removeSegment
) {
    addLayer_->setDefaultAction(addLayer);
    addLayerAction_     = addLayer;
    addSegmentAction_   = addSegment;
    insertBeforeAction_ = insertBefore;
    insertAfterAction_  = insertAfter;
    moveEarlierAction_  = moveEarlier;
    moveLaterAction_    = moveLater;
    removeAction_       = removeSegment;
    if (addGroup) {
        addGroup_->setDefaultAction(addGroup);
        addGroup_->show();
    }
    addSegment_->setDefaultAction(addSegment);
    addSegment_->setPopupMode(QToolButton::InstantPopup);
    remove_->setDefaultAction(removeSegment);
    if (moveEarlier) {
        moveEarlier_->setDefaultAction(moveEarlier);
    }
    if (moveLater) {
        moveLater_->setDefaultAction(moveLater);
    }
    addLayer_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    addGroup_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    addSegment_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    moveEarlier_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    moveLater_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    remove_->setToolButtonStyle(Qt::ToolButtonIconOnly);
}

bool StructurePanel::isValidDropTarget(
    const QTreeWidgetItem* dragged, const QTreeWidgetItem* destinationParent
) {
    if (! dragged) {
        return false;
    }
    // A segment is time inside one layer's chain; a group has no chain to put
    // it in, and the top level is not a layer either.
    if (itemKind(dragged) == SegmentItemKind) {
        return itemKind(destinationParent) == LayerItemKind;
    }
    // A layer or group goes next to its kind, which means the top level or the
    // inside of a group - never into a leaf layer.
    if (destinationParent && itemKind(destinationParent) != GroupItemKind) {
        return false;
    }
    // And never into its own subtree, which would detach it from the tree.
    for (const QTreeWidgetItem* ancestor = destinationParent; ancestor;
         ancestor                        = ancestor->parent()) {
        if (ancestor == dragged) {
            return false;
        }
    }
    return true;
}

void StructurePanel::setDocument(const WaveDocument& document) {
    document_ = document;
    rebuildTree();
}

void StructurePanel::refreshIcons() {
    std::function<void(QTreeWidgetItem*)> refresh = [&](QTreeWidgetItem* item) {
        for (int row = 0; row < item->childCount(); ++row) {
            QTreeWidgetItem* child = item->child(row);
            switch (itemKind(child)) {
                case GroupItemKind:
                    child->setIcon(0, treeIcon(QStringLiteral("group"), itemColor(child)));
                    break;
                case LayerItemKind:
                    child->setIcon(0, treeIcon(QStringLiteral("layer"), itemColor(child), true));
                    break;
                case SegmentItemKind: {
                    const WaveLayer* owner = layerAtPath(
                        document_.layers, pathFromVariant(child->data(0, SourcePathRole))
                    );
                    const size_t index = child->data(0, SourceIndexRole).toULongLong();
                    if (owner && ! isGroup(*owner) && index < owner->segments.size()) {
                        child->setIcon(
                            0,
                            treeIcon(
                                segmentIconName(segmentType(owner->segments[index])),
                                itemColor(child)
                            )
                        );
                    }
                    break;
                }
                default:
                    break;
            }
            refresh(child);
        }
    };
    refresh(tree_->invisibleRootItem());
}

void StructurePanel::setSelection(const LayerPath& layerPath, std::optional<size_t> segmentIndex) {
    const QSignalBlocker blocker(tree_);
    tree_->setCurrentItem(nullptr);
    QTreeWidgetItem* layerItem = itemAtPath(layerPath);
    if (! layerItem) {
        updateActionState();
        return;
    }
    if (! segmentIndex) {
        tree_->setCurrentItem(layerItem);
        updateActionState();
        return;
    }
    if (static_cast<int>(*segmentIndex) < layerItem->childCount()) {
        QTreeWidgetItem* segmentItem = layerItem->child(static_cast<int>(*segmentIndex));
        if (itemKind(segmentItem) == SegmentItemKind) {
            tree_->setCurrentItem(segmentItem);
        }
    }
    updateActionState();
}

QTreeWidgetItem* StructurePanel::itemAtPath(const LayerPath& layerPath) const {
    if (layerPath.empty() || ! layerAtPath(document_.layers, layerPath)) {
        return nullptr;
    }
    // The tree mirrors the document, so an index within a level is the row of
    // its item.
    QTreeWidgetItem* item = nullptr;
    for (const size_t index : layerPath) {
        const int row = static_cast<int>(index);
        item          = item ? item->child(row) : tree_->topLevelItem(row);
        if (! item) {
            return nullptr;
        }
    }
    return item;
}

void StructurePanel::appendNodes(
    const std::vector<WaveLayer>& nodes, QTreeWidgetItem* parentItem, size_t& leafIndex
) {
    const LayerPath parentPath =
        parentItem ? pathFromVariant(parentItem->data(0, SourcePathRole)) : LayerPath{};
    for (size_t index = 0; index < nodes.size(); ++index) {
        const WaveLayer& node = nodes[index];
        const bool group      = isGroup(node);
        LayerPath path        = parentPath;
        path.push_back(index);

        auto* item = parentItem ? new QTreeWidgetItem(parentItem) : new QTreeWidgetItem(tree_);
        item->setText(0, layerLabel(node, index));
        item->setToolTip(
            0,
            tr("Mode: %1, outside its duration: %2")
                .arg(layerModeName(node.mode), layerEdgeName(node.edge))
        );
        item->setData(0, ItemKindRole, group ? GroupItemKind : LayerItemKind);
        item->setData(0, SourcePathRole, pathToVariant(path));
        if (! group) {
            item->setData(0, LeafIndexRole, static_cast<qulonglong>(leafIndex++));
        }
        // The tree shows what a node is, so it uses the plain shapes - the "+"
        // of the toolbar icons belongs to the action that adds one. The color is
        // the layer's curve color, so tree and plot name the same thing.
        item->setIcon(
            0,
            group ? treeIcon(QStringLiteral("group"), itemColor(item))
                  : treeIcon(QStringLiteral("layer"), itemColor(item), true)
        );
        item->setFlags(
            item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled |
            Qt::ItemIsDropEnabled
        );
        item->setCheckState(0, node.enabled ? Qt::Checked : Qt::Unchecked);

        if (group) {
            appendNodes(node.children, item, leafIndex);
        } else {
            for (size_t segmentIndex = 0; segmentIndex < node.segments.size(); ++segmentIndex) {
                const Segment& segment = node.segments[segmentIndex];
                auto* segmentItem      = new QTreeWidgetItem(item);
                segmentItem->setText(0, segmentName(segmentType(segment)));
                segmentItem->setData(0, ItemKindRole, SegmentItemKind);
                segmentItem->setData(0, SourcePathRole, pathToVariant(path));
                segmentItem->setData(0, SourceIndexRole, static_cast<qulonglong>(segmentIndex));
                // A segment belongs to its layer's curve, so it is tinted with it.
                segmentItem->setData(0, LeafIndexRole, item->data(0, LeafIndexRole));
                segmentItem->setIcon(
                    0, treeIcon(segmentIconName(segmentType(segment)), itemColor(segmentItem))
                );
                segmentItem->setFlags(segmentItem->flags() | Qt::ItemIsDragEnabled);
            }
        }
        item->setExpanded(true);
    }
}

void StructurePanel::rebuildTree() {
    const QSignalBlocker blocker(tree_);
    rebuilding_ = true;
    tree_->clear();
    size_t leafIndex = 0;
    appendNodes(document_.layers, nullptr, leafIndex);
    rebuilding_ = false;
    updateActionState();
}

void StructurePanel::showContextMenu(const QPoint& position) {
    auto* item = tree_->itemAt(position);
    if (! item) {
        return;
    }
    tree_->setCurrentItem(item);
    const auto [layerPath, segmentIndex] = selection();
    const WaveLayer* node                = layerAtPath(document_.layers, layerPath);
    if (! node) {
        return;
    }

    QMenu menu(this);
    if (segmentIndex) {
        if (insertBeforeAction_ && insertBeforeAction_->menu()) {
            menu.addMenu(insertBeforeAction_->menu());
        }
        if (insertAfterAction_ && insertAfterAction_->menu()) {
            menu.addMenu(insertAfterAction_->menu());
        }
        menu.addSeparator();
        if (moveEarlierAction_) {
            menu.addAction(moveEarlierAction_);
        }
        if (moveLaterAction_) {
            menu.addAction(moveLaterAction_);
        }
        menu.addSeparator();
        auto* duplicate = menu.addAction(tr("Duplicate Segment"));
        if (removeAction_) {
            menu.addAction(removeAction_);
        }
        QAction* chosen = menu.exec(tree_->viewport()->mapToGlobal(position));
        if (chosen == duplicate) {
            emit duplicateSegmentRequested(layerPath, *segmentIndex);
        }
        return;
    }

    const bool group = isGroup(*node);
    if (group) {
        if (addLayerAction_) {
            menu.addAction(addLayerAction_);
        }
    } else if (addSegmentAction_ && addSegmentAction_->menu()) {
        menu.addMenu(addSegmentAction_->menu());
    }
    menu.addSeparator();
    auto* intoGroup  = menu.addAction(tr("Group into New Group"));
    QAction* ungroup = group ? menu.addAction(tr("Ungroup")) : nullptr;
    menu.addSeparator();
    auto* duplicate = menu.addAction(group ? tr("Duplicate Group") : tr("Duplicate Layer"));
    auto* remove    = menu.addAction(group ? tr("Remove Group") : tr("Remove Layer"));
    QAction* chosen = menu.exec(tree_->viewport()->mapToGlobal(position));
    if (chosen == intoGroup) {
        emit groupLayerRequested(layerPath);
    } else if (ungroup && chosen == ungroup) {
        emit ungroupRequested(layerPath);
    } else if (chosen == duplicate) {
        emit duplicateLayerRequested(layerPath);
    } else if (chosen == remove) {
        emit removeLayerRequested(layerPath);
    }
}

void StructurePanel::emitSelection() {
    updateActionState();
    const auto [layerPath, segmentIndex] = selection();
    emit selectionChanged(layerPath, segmentIndex);
}

void StructurePanel::updateActionState() {
    // Shared actions are enabled by MainWindow from its validated selection.
}

std::vector<WaveLayer> StructurePanel::collectNodes(const QTreeWidgetItem* parentItem) const {
    std::vector<WaveLayer> nodes;
    const int count =
        parentItem ? parentItem->childCount() : tree_->topLevelItemCount();
    for (int row = 0; row < count; ++row) {
        const QTreeWidgetItem* item =
            parentItem ? parentItem->child(row) : tree_->topLevelItem(row);
        const int kind = itemKind(item);
        if (kind != LayerItemKind && kind != GroupItemKind) {
            continue;
        }
        const WaveLayer* source =
            layerAtPath(document_.layers, pathFromVariant(item->data(0, SourcePathRole)));
        if (! source) {
            continue;
        }
        WaveLayer node = *source;
        node.children.clear();
        node.segments.clear();
        if (kind == GroupItemKind) {
            node.children = collectNodes(item);
        } else {
            for (int segmentRow = 0; segmentRow < item->childCount(); ++segmentRow) {
                const QTreeWidgetItem* segmentItem = item->child(segmentRow);
                if (itemKind(segmentItem) != SegmentItemKind) {
                    continue;
                }
                const WaveLayer* owner = layerAtPath(
                    document_.layers, pathFromVariant(segmentItem->data(0, SourcePathRole))
                );
                const size_t index = segmentItem->data(0, SourceIndexRole).toULongLong();
                if (owner && ! isGroup(*owner) && index < owner->segments.size()) {
                    node.segments.push_back(owner->segments[index]);
                }
            }
        }
        nodes.push_back(std::move(node));
    }
    return nodes;
}

void StructurePanel::emitReorderedDocument() {
    WaveDocument reordered = document_;
    reordered.layers       = collectNodes(nullptr);
    emit documentReordered(reordered);
}

std::pair<LayerPath, std::optional<size_t>> StructurePanel::selection() const {
    const auto* item = tree_->currentItem();
    if (! item) {
        return {};
    }
    const LayerPath path = pathFromVariant(item->data(0, SourcePathRole));
    const WaveLayer* node = layerAtPath(document_.layers, path);
    if (! node) {
        return {};
    }
    if (itemKind(item) != SegmentItemKind) {
        return {path, std::nullopt};
    }
    const size_t segmentIndex = item->data(0, SourceIndexRole).toULongLong();
    if (isGroup(*node) || segmentIndex >= node->segments.size()) {
        return {};
    }
    return {path, segmentIndex};
}

}  // namespace zwe
