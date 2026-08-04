// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QWidget>
#include <functional>
#include <optional>

#include "core/wavedocument.h"

class QFormLayout;
class QTableWidget;

namespace zwe {

class PropertyPanel final : public QWidget {
    Q_OBJECT

public:
    explicit PropertyPanel(QWidget* parent = nullptr);

    void setDocument(const WaveDocument& document);
    // An empty layerPath clears the panel; a path without a segment index shows
    // the properties of that layer or group.
    void setSelection(const LayerPath& layerPath, std::optional<size_t> segmentIndex);

public slots:
    // The point the canvas is working on, -1 for none. Selects the matching row
    // of the points table, so both views always show the same point.
    void setSelectedPoint(int pointIndex);

signals:
    void segmentChanged(const LayerPath& layerPath, size_t segmentIndex, const Segment& segment);
    // Name, enabled state, mode or edge policy of the selected layer or group.
    void layerChanged(const LayerPath& layerPath, const WaveLayer& layer);
    void documentChanged(const WaveDocument& document);
    // A row of the points table was picked, so the canvas can highlight the same
    // point and let the arrow keys act on it.
    void pointSelected(int pointIndex);

private:
    void rebuildForm();
    void addLayerForm(const WaveLayer& layer);
    // An empty tooltip leaves the row without one; a non-empty one is put on
    // both the field and its label, so hovering either explains the parameter.
    void addNumberField(
        const QString& label,
        double value,
        const std::function<void(Segment&, double)>& setter,
        const QString& tooltip = QString()
    );
    // A parameter field of a voltammetry segment. Its value decides how long one
    // sweep is, so the segment's duration is fitted to the sweep along with the
    // edit. The duration field itself stays free: a sweep can still be cut short
    // deliberately, it just snaps back on the next parameter edit.
    void addSweepNumberField(
        const QString& label,
        double value,
        const std::function<void(Segment&, double)>& setter,
        const QString& tooltip = QString()
    );
    void addPointsTable(const PointsParams& params);
    // Writes the coordinates of the selected segment into the existing table
    // without rebuilding it.
    void refreshPointsTable();
    // True when document differs from the current one only in the coordinates of
    // the selected Points segment - what a drag or a nudge produces.
    bool onlyPointsMoved(const WaveDocument& document) const;
    void editPoint(int row, int column, double value);
    void insertPointAfterSelection();
    void removeSelectedPoint();
    void emitChanged(Segment segment);
    // The selected node, and the selected segment of it; both nullptr when the
    // selection does not resolve in the current document.
    const WaveLayer* selectedLayer() const;
    const Segment* selectedSegment() const;
    const PointsParams* selectedPoints() const;

    WaveDocument document_;
    LayerPath layerPath_;
    std::optional<size_t> segmentIndex_;
    QFormLayout* form_ = nullptr;
    // Only set while a Points segment small enough to list is selected.
    QTableWidget* pointsTable_ = nullptr;
    std::optional<size_t> selectedPoint_;
};

}  // namespace zwe
