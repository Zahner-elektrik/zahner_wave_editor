// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QWidget>
#include <optional>
#include <utility>
#include <vector>

#include "core/wavedocument.h"

class QTreeWidget;
class QTreeWidgetItem;
class QAction;
class QToolButton;

namespace zwe {

class StructurePanel final : public QWidget {
    Q_OBJECT

public:
    explicit StructurePanel(QWidget* parent = nullptr);

    void setDocument(const WaveDocument& document);
    void refreshIcons();
    // An empty layerPath selects nothing; a path without a segment index selects
    // the layer or group itself.
    void setSelection(const LayerPath& layerPath, std::optional<size_t> segmentIndex);
    void setStructureActions(
        QAction* addLayer,
        QAction* addGroup,
        QAction* addSegment,
        QAction* insertBefore,
        QAction* insertAfter,
        QAction* moveEarlier,
        QAction* moveLater,
        QAction* removeSegment
    );
    void setStructureActions(QAction* addLayer, QAction* addSegment, QAction* removeSegment) {
        setStructureActions(
            addLayer, nullptr, addSegment, nullptr, nullptr, nullptr, nullptr, removeSegment
        );
    }

    // Whether moving dragged below destinationParent (null for the top level)
    // leaves a valid structure: a segment belongs into a leaf layer, a layer or
    // group next to its kind or into a group, and nothing into its own subtree.
    // Qt cannot express that through item flags, because one flag governs both
    // kinds of drop, so the tree checks it while a drag is over it. Public
    // because that is where the rule can be verified: an internal move cannot
    // be simulated with synthetic events (QAbstractItemView requires the drag
    // to originate from the view itself).
    static bool isValidDropTarget(
        const QTreeWidgetItem* dragged, const QTreeWidgetItem* destinationParent
    );

signals:
    void selectionChanged(const LayerPath& layerPath, std::optional<size_t> segmentIndex);
    void addLayerRequested();
    void removeLayerRequested(const LayerPath& layerPath);
    void duplicateLayerRequested(const LayerPath& layerPath);
    // Wrap the addressed node in a new group that takes its place.
    void groupLayerRequested(const LayerPath& layerPath);
    // Dissolve the addressed group, its children taking its place.
    void ungroupRequested(const LayerPath& layerPath);
    void addSegmentRequested(const LayerPath& layerPath);
    void removeSegmentRequested(const LayerPath& layerPath, size_t segmentIndex);
    void duplicateSegmentRequested(const LayerPath& layerPath, size_t segmentIndex);
    void layerEnabledChanged(const LayerPath& layerPath, bool enabled);
    void documentReordered(const WaveDocument& document);

private:
    void rebuildTree();
    // Builds the items for one level; parentItem is null for the top level.
    // leafIndex counts the leaf layers built so far, so every layer item knows
    // which curve of the canvas it stands for: recursing into a group before
    // moving on gives the same depth-first numbering the canvas samples in.
    void appendNodes(
        const std::vector<WaveLayer>& nodes, QTreeWidgetItem* parentItem, size_t& leafIndex
    );
    void showContextMenu(const QPoint& position);
    void emitSelection();
    void emitReorderedDocument();
    // Reads one level back out of the tree after a drag & drop, taking each
    // node from the position its item was built from.
    std::vector<WaveLayer> collectNodes(const QTreeWidgetItem* parentItem) const;
    void updateActionState();
    QTreeWidgetItem* itemAtPath(const LayerPath& layerPath) const;
    std::pair<LayerPath, std::optional<size_t>> selection() const;

    WaveDocument document_;
    QTreeWidget* tree_           = nullptr;
    QToolButton* addLayer_       = nullptr;
    QToolButton* addGroup_       = nullptr;
    QToolButton* addSegment_     = nullptr;
    QToolButton* moveEarlier_    = nullptr;
    QToolButton* moveLater_      = nullptr;
    QToolButton* remove_         = nullptr;
    QAction* addLayerAction_     = nullptr;
    QAction* addSegmentAction_   = nullptr;
    QAction* insertBeforeAction_ = nullptr;
    QAction* insertAfterAction_  = nullptr;
    QAction* moveEarlierAction_  = nullptr;
    QAction* moveLaterAction_    = nullptr;
    QAction* removeAction_       = nullptr;
    bool rebuilding_             = false;
};

}  // namespace zwe
