// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "commands.h"

namespace zwe {

DocumentEditCommand::DocumentEditCommand(
    WaveDocument before,
    WaveDocument after,
    DocumentSelection beforeSelection,
    DocumentSelection afterSelection,
    ApplyState applyState,
    const QString& text,
    QString mergeKey
)
    : QUndoCommand(text),
      before_(std::move(before)),
      after_(std::move(after)),
      beforeSelection_(std::move(beforeSelection)),
      afterSelection_(std::move(afterSelection)),
      applyState_(std::move(applyState)),
      mergeKey_(std::move(mergeKey)) {
}

void DocumentEditCommand::undo() {
    applyState_(before_, beforeSelection_);
}

void DocumentEditCommand::redo() {
    applyState_(after_, afterSelection_);
}

int DocumentEditCommand::id() const {
    return 0x5A5745;
}

bool DocumentEditCommand::mergeWith(const QUndoCommand* other) {
    const auto* edit = dynamic_cast<const DocumentEditCommand*>(other);
    if (! edit || mergeKey_.isEmpty() || mergeKey_ != edit->mergeKey_) {
        return false;
    }

    after_          = edit->after_;
    afterSelection_ = edit->afterSelection_;
    return true;
}

}  // namespace zwe
