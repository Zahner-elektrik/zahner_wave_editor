// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QMainWindow>
#include <QStringList>
#include <optional>

#include "core/wavedocument.h"

class QAction;
class QLabel;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QMenu;
class QProgressBar;
class QUndoStack;
class QSpinBox;

namespace zwe {

class CanvasWidget;
class GridBar;
class HelpBrowser;
class LicenseViewerDialog;
class PropertyPanel;
class StructurePanel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Opens the given .zwj document. Used for the command line argument and
    // drag & drop. Returns false and shows a message box on failure.
    bool openDocument(const QString& filePath);

    // Binds the window to the document it currently holds: new, open, save as,
    // the recent files list and dropping another document are taken out, so the
    // application that started the editor keeps control over which file is
    // edited. Everything about editing and saving that document stays.
    void setEditOnly(bool editOnly);

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void newDocument();
    void openDocumentDialog();
    bool saveDocument();
    bool saveDocumentAs();
    void importCsv();
    void exportWaveform();
    void scaleWaveform();
    void showSettings();
    void showHelp();
    void showFormulaHelp();
    void showLicenses();
    void showAbout();

private:
    static constexpr int MaxRecentFiles = 10;

    bool confirmDiscardChanges();
    void setDocument(WaveDocument document, const QString& filePath);
    void applyDocumentState(
        WaveDocument document,
        const LayerPath& layerPath,
        std::optional<size_t> segmentIndex,
        bool resetView
    );
    void applyDocumentMutation(
        WaveDocument document,
        const LayerPath& layerPath,
        std::optional<size_t> segmentIndex,
        const QString& text,
        QString mergeKey = {}
    );
    void replaceSegment(
        const LayerPath& layerPath,
        size_t segmentIndex,
        const Segment& segment,
        bool continuous = false
    );
    void replaceLayer(const LayerPath& layerPath, const WaveLayer& layer);
    // Adds a leaf layer or an empty group into the selected group, next to the
    // selection, or at the top level - whichever the selection makes possible.
    void addLayerNode(LayerKind kind);
    // Wraps the addressed node in a new group, and dissolves a group into the
    // level it sits in.
    void groupLayer(const LayerPath& layerPath);
    void ungroupLayer(const LayerPath& layerPath);
    void addSegment(SegmentType type);
    void insertSegment(SegmentType type, bool afterSelection);
    void moveSelectedSegment(int offset);
    void removeSelectedSegment();
    void updateStructureActionState();
    QString segmentMergeKey(
        const LayerPath& layerPath, size_t segmentIndex, const Segment& segment
    ) const;
    void setSelection(const LayerPath& layerPath, std::optional<size_t> segmentIndex);
    // The leaf layer the selection addresses, or nullptr for a group or nothing.
    const WaveLayer* selectedLeaf() const;
    void setDocumentDirty(bool dirty);
    void showRecalculationProgress(int percent);
    void updateWindowTitle();
    void updateRecentFilesMenu();
    void addRecentFile(const QString& filePath);
    QStringList recentFiles() const;
    void writeSettings() const;
    // Applies darkTheme_/blueAccent_ to the palette and re-tints the icons.
    // The values themselves are persisted by SettingsDialog.
    void applyTheme();

    WaveDocument document_;
    QString filePath_;
    bool dirty_                     = false;
    bool darkTheme_                 = false;
    bool blueAccent_                = false;
    QUndoStack* undoStack_          = nullptr;
    CanvasWidget* canvas_           = nullptr;
    GridBar* gridBar_               = nullptr;
    StructurePanel* structurePanel_ = nullptr;
    PropertyPanel* propertyPanel_   = nullptr;
    HelpBrowser* helpBrowser_       = nullptr;
    LicenseViewerDialog* licenses_  = nullptr;
    QLabel* cursorReadout_          = nullptr;
    QProgressBar* recalcProgress_   = nullptr;
    QSpinBox* sampleRateSpin_       = nullptr;
    LayerPath selectedLayerPath_;
    std::optional<size_t> selectedSegmentIndex_;
    QMenu* recentFilesMenu_       = nullptr;
    bool editOnly_ = false;
    QAction* newAction_    = nullptr;
    QAction* openAction_   = nullptr;
    QAction* saveAsAction_ = nullptr;
    QAction* saveAction_          = nullptr;
    QAction* addLayerAction_      = nullptr;
    QAction* addGroupAction_      = nullptr;
    QAction* addSegmentAction_    = nullptr;
    QAction* insertBeforeAction_  = nullptr;
    QAction* insertAfterAction_   = nullptr;
    QAction* moveEarlierAction_   = nullptr;
    QAction* moveLaterAction_     = nullptr;
    QAction* removeSegmentAction_ = nullptr;
};

}  // namespace zwe
