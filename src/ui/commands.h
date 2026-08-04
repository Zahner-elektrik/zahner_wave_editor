// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QString>
#include <QUndoCommand>
#include <functional>
#include <optional>

#include "core/wavedocument.h"

namespace zwe {

struct DocumentSelection {
    // Empty when no layer or group is selected; a segment index is only
    // meaningful together with the path of the leaf layer holding it.
    LayerPath layerPath;
    std::optional<size_t> segmentIndex;
};

// Applies full document snapshots. Keeping commands at this boundary makes all
// model edits (including multi-field structure edits) use the same history path.
class DocumentEditCommand final : public QUndoCommand {
public:
    using ApplyState = std::function<void(const WaveDocument&, const DocumentSelection&)>;

    DocumentEditCommand(
        WaveDocument before,
        WaveDocument after,
        DocumentSelection beforeSelection,
        DocumentSelection afterSelection,
        ApplyState applyState,
        const QString& text,
        QString mergeKey = {}
    );

    void undo() override;
    void redo() override;
    int id() const override;
    bool mergeWith(const QUndoCommand* other) override;

private:
    WaveDocument before_;
    WaveDocument after_;
    DocumentSelection beforeSelection_;
    DocumentSelection afterSelection_;
    ApplyState applyState_;
    QString mergeKey_;
};

}  // namespace zwe
