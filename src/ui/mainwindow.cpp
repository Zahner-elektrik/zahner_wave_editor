// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QResource>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUndoStack>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <tuple>
#include <type_traits>

#include "appsettings.h"
#include "canvaswidget.h"
#include "commands.h"
#include "core/binaryio.h"
#include "core/csvio.h"
#include "core/documenttransform.h"
#include "core/segmentcontinuity.h"
#include "core/version.h"
#include "core/zwjio.h"
#include "editonlyserver.h"
#include "exportdialog.h"
#include "gridbar.h"
#include "helpbrowser.h"
#include "importdialog.h"
#include "licenseviewerdialog.h"
#include "propertypanel.h"
#include "settingsdialog.h"
#include "structurepanel.h"
#include "theme.h"

static void initWaveEditorResources() {
    Q_INIT_RESOURCE(waveeditor);
}

// See the matching category in canvaswidget.cpp; enable with
// QT_LOGGING_RULES="zwe.perf.debug=true".
Q_LOGGING_CATEGORY(zwePerfWindow, "zwe.perf", QtWarningMsg)

namespace zwe {

namespace {

constexpr auto SettingsGeometryKey    = "mainWindow/geometry";
constexpr auto SettingsRecentFilesKey = "mainWindow/recentFiles";
constexpr auto SettingsWindowStateKey = "mainWindow/state";
constexpr auto SettingsOpenPathKey    = "fileDialogs/openDocumentPath";
constexpr auto SettingsSavePathKey    = "fileDialogs/saveDocumentPath";
constexpr auto DocumentFilter         = "Zahner Wave JSON (*.zwj)";

QIcon toolbarIcon(const char* name) {
    return QIcon(QStringLiteral(":/icons/toolbar/%1.svg").arg(QString::fromLatin1(name)));
}

void setActionHelp(QAction* action, const QString& text) {
    action->setToolTip(text);
    action->setStatusTip(text);
}

// A layer path as a fragment of an undo merge key: "0", "2-1". Two edits merge
// only when they touch the same field of the same node, so the key has to name
// the node as precisely as the path does.
QString layerPathKey(const LayerPath& path) {
    QStringList parts;
    parts.reserve(static_cast<qsizetype>(path.size()));
    for (const size_t index : path) {
        parts.append(QString::number(index));
    }
    return parts.join(QLatin1Char('-'));
}

QString oneChangedField(std::initializer_list<std::pair<bool, const char*>> fields) {
    const char* changedField = nullptr;
    for (const auto& [changed, field] : fields) {
        if (! changed) {
            continue;
        }
        if (changedField) {
            return {};
        }
        changedField = field;
    }
    return changedField ? QString::fromLatin1(changedField) : QString{};
}

QString changedParameterField(const Segment& before, const Segment& after) {
    if (before.params.index() != after.params.index()) {
        return {};
    }
    return std::visit(
        [](const auto& left, const auto& right) -> QString {
            using Params = std::decay_t<decltype(left)>;
            if constexpr (! std::is_same_v<Params, std::decay_t<decltype(right)>>) {
                return {};
            } else if constexpr (std::is_same_v<Params, DcParams>) {
                return oneChangedField({{left.value != right.value, "value"}});
            } else if constexpr (std::is_same_v<Params, RampParams>) {
                return oneChangedField({
                    {left.startValue != right.startValue, "start"},
                    {left.endValue != right.endValue, "end"},
                });
            } else if constexpr (std::is_same_v<Params, SineParams>) {
                return oneChangedField({
                    {left.amplitude != right.amplitude, "amplitude"},
                    {left.frequency != right.frequency, "frequency"},
                    {left.phaseDeg != right.phaseDeg, "phase"},
                    {left.offset != right.offset, "offset"},
                });
            } else if constexpr (std::is_same_v<Params, SquareParams>) {
                return oneChangedField({
                    {left.amplitude != right.amplitude, "amplitude"},
                    {left.frequency != right.frequency, "frequency"},
                    {left.phaseDeg != right.phaseDeg, "phase"},
                    {left.offset != right.offset, "offset"},
                    {left.duty != right.duty, "duty"},
                });
            } else if constexpr (std::is_same_v<Params, TriangleParams>) {
                return oneChangedField({
                    {left.amplitude != right.amplitude, "amplitude"},
                    {left.frequency != right.frequency, "frequency"},
                    {left.phaseDeg != right.phaseDeg, "phase"},
                    {left.offset != right.offset, "offset"},
                    {left.symmetry != right.symmetry, "symmetry"},
                });
            } else if constexpr (std::is_same_v<Params, PulseParams>) {
                return oneChangedField({
                    {left.baseValue != right.baseValue, "base"},
                    {left.pulseValue != right.pulseValue, "pulse"},
                    {left.delay != right.delay, "delay"},
                    {left.width != right.width, "width"},
                });
            } else if constexpr (std::is_same_v<Params, ExponentialParams>) {
                return oneChangedField({
                    {left.startValue != right.startValue, "start"},
                    {left.endValue != right.endValue, "end"},
                    {left.timeConstant != right.timeConstant, "timeConstant"},
                });
            } else if constexpr (std::is_same_v<Params, FormulaParams>) {
                return oneChangedField({
                    {left.expression != right.expression, "expression"},
                    {left.globalTime != right.globalTime, "timeReference"},
                });
            } else if constexpr (std::is_same_v<Params, ChirpParams>) {
                return oneChangedField({
                    {left.amplitude != right.amplitude, "amplitude"},
                    {left.startFrequency != right.startFrequency, "startFrequency"},
                    {left.endFrequency != right.endFrequency, "endFrequency"},
                    {left.phaseDeg != right.phaseDeg, "phase"},
                    {left.offset != right.offset, "offset"},
                    {left.sweep != right.sweep, "sweep"},
                });
            } else if constexpr (std::is_same_v<Params, RickerParams>) {
                return oneChangedField({
                    {left.amplitude != right.amplitude, "amplitude"},
                    {left.centerFrequency != right.centerFrequency, "centerFrequency"},
                    {left.offset != right.offset, "offset"},
                });
            } else if constexpr (std::is_same_v<Params, WindowParams>) {
                return oneChangedField({
                    {left.shape != right.shape, "shape"},
                    {left.amplitude != right.amplitude, "amplitude"},
                    {left.offset != right.offset, "offset"},
                    {left.alpha != right.alpha, "alpha"},
                    {left.sigma != right.sigma, "sigma"},
                    {left.beta != right.beta, "beta"},
                    {left.rise != right.rise, "rise"},
                    {left.fall != right.fall, "fall"},
                });
            } else if constexpr (std::is_same_v<Params, NpvParams>) {
                return oneChangedField({
                    {left.startValue != right.startValue, "start"},
                    {left.endValue != right.endValue, "end"},
                    {left.stepValue != right.stepValue, "stepValue"},
                    {left.stepTime != right.stepTime, "stepTime"},
                    {left.pulseTime != right.pulseTime, "pulseTime"},
                });
            } else if constexpr (std::is_same_v<Params, SwvParams>) {
                return oneChangedField({
                    {left.startValue != right.startValue, "start"},
                    {left.endValue != right.endValue, "end"},
                    {left.stepValue != right.stepValue, "stepValue"},
                    {left.amplitude != right.amplitude, "amplitude"},
                    {left.period != right.period, "period"},
                });
            } else if constexpr (std::is_same_v<Params, DpvParams>) {
                return oneChangedField({
                    {left.startValue != right.startValue, "start"},
                    {left.endValue != right.endValue, "end"},
                    {left.stepValue != right.stepValue, "stepValue"},
                    {left.pulseValue != right.pulseValue, "pulseValue"},
                    {left.stepTime != right.stepTime, "stepTime"},
                    {left.pulseTime != right.pulseTime, "pulseTime"},
                    {left.invertPulse != right.invertPulse, "invertPulse"},
                });
            } else {
                if (left.interp != right.interp && left.points == right.points) {
                    return QStringLiteral("interpolation");
                }
                if (left.points.size() != right.points.size() || left.interp != right.interp) {
                    return {};
                }
                std::optional<size_t> changedIndex;
                for (size_t index = 0; index < left.points.size(); ++index) {
                    if (left.points[index] != right.points[index]) {
                        if (changedIndex) {
                            return {};
                        }
                        changedIndex = index;
                    }
                }
                return changedIndex ? QStringLiteral("point-%1").arg(*changedIndex) : QString{};
            }
        },
        before.params,
        after.params
    );
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    initWaveEditorResources();
    setAcceptDrops(true);
    setMinimumSize(640, 420);
    undoStack_ = new QUndoStack(this);
    connect(undoStack_, &QUndoStack::cleanChanged, this, [this](bool clean) {
        setDocumentDirty(! clean);
    });

    // The grid bar sits directly above the plot, not in a menu: how fine the snap
    // grid is belongs to the editing itself and is changed while working.
    auto* canvasArea   = new QWidget(this);
    auto* canvasLayout = new QVBoxLayout(canvasArea);
    canvasLayout->setContentsMargins(0, 0, 0, 0);
    canvasLayout->setSpacing(0);
    gridBar_ = new GridBar(canvasArea);
    canvasLayout->addWidget(gridBar_);
    canvas_ = new CanvasWidget(canvasArea);
    canvasLayout->addWidget(canvas_, 1);
    setCentralWidget(canvasArea);
    canvas_->setSnapSettings(gridBar_->settings());

    auto* structureDock = new QDockWidget(tr("Structure"), this);
    structureDock->setObjectName(QStringLiteral("structureDock"));
    structurePanel_ = new StructurePanel(structureDock);
    structureDock->setWidget(structurePanel_);
    addDockWidget(Qt::LeftDockWidgetArea, structureDock);

    auto* propertyDock = new QDockWidget(tr("Properties"), this);
    propertyDock->setObjectName(QStringLiteral("propertyDock"));
    auto* propertyScroll = new QScrollArea(propertyDock);
    propertyScroll->setObjectName(QStringLiteral("propertyScrollArea"));
    propertyScroll->setWidgetResizable(true);
    propertyScroll->setFrameShape(QFrame::NoFrame);
    propertyPanel_ = new PropertyPanel(propertyScroll);
    propertyScroll->setWidget(propertyPanel_);
    propertyDock->setWidget(propertyScroll);
    addDockWidget(Qt::RightDockWidgetArea, propertyDock);

    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    auto* newAction =
        fileMenu->addAction(toolbarIcon("new"), tr("&New"), this, &MainWindow::newDocument);
    newAction->setObjectName(QStringLiteral("newAction"));
    newAction->setShortcut(QKeySequence::New);
    newAction_ = newAction;

    auto* openAction = fileMenu->addAction(
        toolbarIcon("open"), tr("&Open..."), this, &MainWindow::openDocumentDialog
    );
    openAction->setObjectName(QStringLiteral("openAction"));
    openAction->setShortcut(QKeySequence::Open);
    openAction_ = openAction;

    saveAction_ = fileMenu->addAction(tr("&Save"), this, &MainWindow::saveDocument);
    saveAction_->setIcon(toolbarIcon("save"));
    saveAction_->setObjectName(QStringLiteral("saveAction"));
    saveAction_->setShortcut(QKeySequence::Save);
    auto* saveAsAction = fileMenu->addAction(tr("Save &As..."), this, &MainWindow::saveDocumentAs);
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    saveAsAction_ = saveAsAction;

    recentFilesMenu_ = fileMenu->addMenu(tr("Recent &Files"));
    updateRecentFilesMenu();

    fileMenu->addSeparator();
    auto* importAction = fileMenu->addAction(tr("&Import CSV..."), this, &MainWindow::importCsv);
    importAction->setIcon(toolbarIcon("import-csv"));
    importAction->setObjectName(QStringLiteral("importAction"));
    importAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
    auto* exportAction =
        fileMenu->addAction(tr("Export &Waveform..."), this, &MainWindow::exportWaveform);
    exportAction->setIcon(toolbarIcon("export-csv"));
    exportAction->setObjectName(QStringLiteral("exportAction"));
    exportAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));

    fileMenu->addSeparator();
    auto* quitAction = fileMenu->addAction(tr("&Quit"), this, &QWidget::close);
    quitAction->setShortcut(QKeySequence::Quit);

    auto* editMenu   = menuBar()->addMenu(tr("&Edit"));
    auto* undoAction = undoStack_->createUndoAction(this, tr("&Undo"));
    undoAction->setIcon(toolbarIcon("undo"));
    undoAction->setShortcut(QKeySequence::Undo);
    editMenu->addAction(undoAction);
    auto* redoAction = undoStack_->createRedoAction(this, tr("&Redo"));
    redoAction->setIcon(toolbarIcon("redo"));
    redoAction->setShortcut(QKeySequence::Redo);
    editMenu->addAction(redoAction);
    editMenu->addSeparator();
    auto* scaleAction = editMenu->addAction(tr("Scale Entire Waveform..."));
    scaleAction->setObjectName(QStringLiteral("scaleWaveformAction"));
    connect(scaleAction, &QAction::triggered, this, &MainWindow::scaleWaveform);
    editMenu->addSeparator();
    auto* settingsAction = editMenu->addAction(tr("&Settings..."), this, &MainWindow::showSettings);
    settingsAction->setObjectName(QStringLiteral("settingsAction"));
    settingsAction->setShortcut(QKeySequence::Preferences);
    // macOS keeps preferences in the application menu, not in Edit.
    settingsAction->setMenuRole(QAction::PreferencesRole);

    auto* structureMenu = menuBar()->addMenu(tr("&Structure"));
    addLayerAction_     = structureMenu->addAction(toolbarIcon("add-layer"), tr("Add &Layer"));
    addLayerAction_->setObjectName(QStringLiteral("addLayerAction"));
    addGroupAction_ = structureMenu->addAction(toolbarIcon("add-group"), tr("Add &Group"));
    addGroupAction_->setObjectName(QStringLiteral("addGroupAction"));
    addSegmentAction_ = structureMenu->addAction(toolbarIcon("add-segment"), tr("Add &Segment"));
    addSegmentAction_->setObjectName(QStringLiteral("addSegmentAction"));
    const auto populateWaveformMenu =
        [this](QMenu* menu, const std::function<void(SegmentType)>& callback) {
            const auto addCategory =
                [this, menu, &callback](const QString& title, const auto& entries) {
                    auto* category = menu->addMenu(title);
                    for (const auto& [label, icon, type] : entries) {
                        auto* action = category->addAction(toolbarIcon(icon), label);
                        action->setData(static_cast<int>(type));
                        connect(action, &QAction::triggered, this, [callback, type] {
                            callback(type);
                        });
                    }
                };
            using Entry = std::tuple<QString, const char*, SegmentType>;
            addCategory(
                tr("Basic"),
                std::initializer_list<Entry>{
                    {tr("Hold / DC"), "wave-dc", SegmentType::Dc},
                    {tr("Ramp"), "wave-ramp", SegmentType::Ramp}
                }
            );
            addCategory(
                tr("Periodic"),
                std::initializer_list<Entry>{
                    {tr("Sine"), "wave-sine", SegmentType::Sine},
                    {tr("Square"), "wave-square", SegmentType::Square},
                    {tr("Triangle"), "wave-triangle", SegmentType::Triangle}
                }
            );
            addCategory(
                tr("Special"),
                std::initializer_list<Entry>{
                    {tr("Pulse"), "wave-pulse", SegmentType::Pulse},
                    {tr("Exponential"), "wave-exponential", SegmentType::Exponential},
                    {tr("Formula"), "wave-formula", SegmentType::Formula},
                    {tr("Points"), "wave-points", SegmentType::Points},
                    {tr("Chirp"), "wave-chirp", SegmentType::Chirp},
                    {tr("Ricker wavelet"), "wave-ricker", SegmentType::Ricker},
                    {tr("Window"), "wave-window", SegmentType::Window}
                }
            );
            // The pulse voltammetry methods, under the names their IM7 jobs
            // carry. Unlike the categories above, each of these is a complete
            // measurement waveform rather than a shape to build one from.
            addCategory(
                tr("Electrochemistry"),
                std::initializer_list<Entry>{
                    {tr("Normal Pulse Voltammetry"), "wave-npv", SegmentType::Npv},
                    {tr("Differential Pulse Voltammetry"), "wave-dpv", SegmentType::Dpv},
                    {tr("Square Wave Voltammetry"), "wave-swv", SegmentType::Swv}
                }
            );
        };
    auto* waveformMenu = new QMenu(tr("Add Segment"), this);
    addSegmentAction_->setMenu(waveformMenu);
    populateWaveformMenu(waveformMenu, [this](SegmentType type) { addSegment(type); });

    insertBeforeAction_ = structureMenu->addAction(tr("Insert &Before"));
    insertBeforeAction_->setObjectName(QStringLiteral("insertBeforeAction"));
    auto* beforeMenu = new QMenu(tr("Insert Before"), this);
    insertBeforeAction_->setMenu(beforeMenu);
    populateWaveformMenu(beforeMenu, [this](SegmentType type) { insertSegment(type, false); });
    insertAfterAction_ = structureMenu->addAction(tr("Insert &After"));
    insertAfterAction_->setObjectName(QStringLiteral("insertAfterAction"));
    auto* afterMenu = new QMenu(tr("Insert After"), this);
    insertAfterAction_->setMenu(afterMenu);
    populateWaveformMenu(afterMenu, [this](SegmentType type) { insertSegment(type, true); });

    moveEarlierAction_ = structureMenu->addAction(tr("Move Earlier"));
    moveEarlierAction_->setIcon(toolbarIcon("move-up"));
    moveEarlierAction_->setObjectName(QStringLiteral("moveEarlierAction"));
    moveEarlierAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Left));
    connect(moveEarlierAction_, &QAction::triggered, this, [this] { moveSelectedSegment(-1); });
    moveLaterAction_ = structureMenu->addAction(tr("Move Later"));
    moveLaterAction_->setIcon(toolbarIcon("move-down"));
    moveLaterAction_->setObjectName(QStringLiteral("moveLaterAction"));
    moveLaterAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Right));
    connect(moveLaterAction_, &QAction::triggered, this, [this] { moveSelectedSegment(1); });
    removeSegmentAction_ = structureMenu->addAction(
        toolbarIcon("remove-selected"),
        tr("&Remove Selected"),
        this,
        &MainWindow::removeSelectedSegment
    );
    removeSegmentAction_->setObjectName(QStringLiteral("removeSegmentAction"));

    connect(addLayerAction_, &QAction::triggered, this, [this] {
        addLayerNode(LayerKind::Leaf);
    });
    connect(addGroupAction_, &QAction::triggered, this, [this] {
        addLayerNode(LayerKind::Group);
    });

    auto* viewMenu            = menuBar()->addMenu(tr("&View"));
    auto* structureDockAction = structureDock->toggleViewAction();
    structureDockAction->setText(tr("Show Structure Dock"));
    viewMenu->addAction(structureDockAction);
    auto* propertyDockAction = propertyDock->toggleViewAction();
    propertyDockAction->setText(tr("Show Properties Dock"));
    viewMenu->addAction(propertyDockAction);
    viewMenu->addSeparator();
    auto* legendAction = viewMenu->addAction(tr("Show &Legend"));
    legendAction->setObjectName(QStringLiteral("legendAction"));
    legendAction->setCheckable(true);
    legendAction->setChecked(appsettings::showLegend());
    canvas_->setLegendVisible(legendAction->isChecked());
    connect(legendAction, &QAction::toggled, this, [this](bool visible) {
        appsettings::setShowLegend(visible);
        canvas_->setLegendVisible(visible);
    });
    auto* fitAction = viewMenu->addAction(
        toolbarIcon("fit-waveform"), tr("&Fit Waveform"), canvas_, &CanvasWidget::fitToDocument
    );
    fitAction->setObjectName(QStringLiteral("fitAction"));

    // Theme and accent color are application settings, not per-window view
    // state: they live in Edit > Settings.
    darkTheme_  = appsettings::darkTheme();
    blueAccent_ = appsettings::accent() == theme::Accent::Blue;
    applyTheme();

    auto* helpMenu   = menuBar()->addMenu(tr("&Help"));
    auto* helpAction = helpMenu->addAction(tr("&Help Contents"), this, &MainWindow::showHelp);
    helpAction->setObjectName(QStringLiteral("helpContentsAction"));
    helpAction->setShortcut(QKeySequence::HelpContents);
    auto* formulaHelpAction =
        helpMenu->addAction(tr("&Formula Reference"), this, &MainWindow::showFormulaHelp);
    formulaHelpAction->setObjectName(QStringLiteral("formulaHelpAction"));
    helpMenu->addSeparator();
    auto* licensesAction =
        helpMenu->addAction(tr("Open Source &Licenses"), this, &MainWindow::showLicenses);
    licensesAction->setObjectName(QStringLiteral("licensesAction"));
    helpMenu->addSeparator();
    helpMenu->addAction(tr("&About"), this, &MainWindow::showAbout);

    auto* toolbar = addToolBar(tr("Waveform tools"));
    toolbar->setObjectName(QStringLiteral("fileToolbar"));
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setIconSize(QSize(22, 22));
    toolbar->addAction(newAction);
    toolbar->addAction(openAction);
    toolbar->addAction(saveAction_);
    toolbar->addAction(importAction);
    toolbar->addAction(exportAction);
    toolbar->addSeparator();
    toolbar->addAction(undoAction);
    toolbar->addAction(redoAction);
    toolbar->addSeparator();
    toolbar->addAction(addLayerAction_);
    toolbar->addAction(addGroupAction_);
    toolbar->addAction(addSegmentAction_);
    toolbar->addAction(removeSegmentAction_);
    toolbar->addSeparator();
    toolbar->addAction(fitAction);
    auto* toolbarSpacer = new QWidget(toolbar);
    toolbarSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(toolbarSpacer);
    auto* sampleRateLabel = new QLabel(tr("Value rate"), toolbar);
    sampleRateLabel->setContentsMargins(6, 0, 4, 0);
    toolbar->addWidget(sampleRateLabel);
    sampleRateSpin_ = new QSpinBox(toolbar);
    sampleRateSpin_->setObjectName(QStringLiteral("toolbarSampleRateSpin"));
    sampleRateSpin_->setRange(1, 1000000000);
    sampleRateSpin_->setSuffix(tr(" 1/s"));
    sampleRateSpin_->setKeyboardTracking(false);
    sampleRateSpin_->setToolTip(tr("Value rate used for preview and waveform export"));
    sampleRateSpin_->setValue(document_.sampleRate);
    toolbar->addWidget(sampleRateSpin_);
    connect(sampleRateSpin_, &QSpinBox::editingFinished, this, [this] {
        const double value = sampleRateSpin_->value();
        if (value == document_.sampleRate) {
            return;
        }
        WaveDocument edited = document_;
        edited.sampleRate   = value;
        applyDocumentMutation(
            std::move(edited), selectedLayerPath_, selectedSegmentIndex_, tr("Change Value Rate")
        );
    });
    if (auto* button = qobject_cast<QToolButton*>(toolbar->widgetForAction(addSegmentAction_))) {
        button->setPopupMode(QToolButton::InstantPopup);
    }
    structurePanel_->setStructureActions(
        addLayerAction_,
        addGroupAction_,
        addSegmentAction_,
        insertBeforeAction_,
        insertAfterAction_,
        moveEarlierAction_,
        moveLaterAction_,
        removeSegmentAction_
    );
    setActionHelp(newAction, tr("Create a new waveform document"));
    setActionHelp(openAction, tr("Open a waveform document"));
    setActionHelp(saveAction_, tr("Save the waveform document"));
    setActionHelp(importAction, tr("Import waveform values from CSV"));
    setActionHelp(exportAction, tr("Export waveform values as CSV or binary doubles"));
    setActionHelp(undoAction, tr("Undo the last edit"));
    setActionHelp(redoAction, tr("Redo the last undone edit"));
    setActionHelp(addLayerAction_, tr("Add a new waveform layer"));
    setActionHelp(
        addGroupAction_,
        tr("Add a group whose layers are combined among themselves before the rest")
    );
    setActionHelp(addSegmentAction_, tr("Add a waveform segment to the active layer"));
    setActionHelp(insertBeforeAction_, tr("Insert a waveform segment before the selection"));
    setActionHelp(insertAfterAction_, tr("Insert a waveform segment after the selection"));
    setActionHelp(moveEarlierAction_, tr("Move the selected segment earlier"));
    setActionHelp(moveLaterAction_, tr("Move the selected segment later"));
    setActionHelp(removeSegmentAction_, tr("Remove the selected waveform segment"));
    setActionHelp(fitAction, tr("Fit the complete waveform in the canvas"));
    setActionHelp(settingsAction, tr("Change the application settings"));
    updateStructureActionState();

    recalcProgress_ = new QProgressBar(this);
    recalcProgress_->setRange(0, 100);
    recalcProgress_->setMaximumWidth(180);
    recalcProgress_->setFormat(tr("Recalculating… %p%"));
    recalcProgress_->hide();
    statusBar()->addPermanentWidget(recalcProgress_);
    connect(
        canvas_, &CanvasWidget::recalculationProgress, this, &MainWindow::showRecalculationProgress
    );

    cursorReadout_ = new QLabel(this);
    cursorReadout_->setMinimumWidth(180);
    statusBar()->addPermanentWidget(cursorReadout_);
    connect(canvas_, &CanvasWidget::cursorReadout, this, [this](double time, double value) {
        if (! std::isfinite(time)) {
            cursorReadout_->clear();
            return;
        }
        cursorReadout_->setText(
            tr("t = %1 s, value = %2").arg(time, 0, 'g', 5).arg(value, 0, 'g', 5)
        );
    });
    connect(
        canvas_,
        &CanvasWidget::segmentSelected,
        this,
        [this](const LayerPath& layerPath, size_t segmentIndex) {
            setSelection(layerPath, segmentIndex);
        }
    );
    connect(canvas_, &CanvasWidget::selectionCleared, this, [this] {
        setSelection(LayerPath{}, std::nullopt);
    });
    connect(
        canvas_,
        &CanvasWidget::segmentChanged,
        this,
        [this](
            const LayerPath& layerPath,
            size_t segmentIndex,
            const Segment& segment,
            bool continuous
        ) { replaceSegment(layerPath, segmentIndex, segment, continuous); }
    );
    connect(gridBar_, &GridBar::settingsChanged, this, [this] {
        canvas_->setSnapSettings(gridBar_->settings());
    });
    connect(canvas_, &CanvasWidget::snapStepChanged, gridBar_, &GridBar::showAutomaticStep);
    // The canvas and the points table show the same point, whichever of the two
    // it was picked in.
    connect(
        canvas_,
        &CanvasWidget::selectedPointChanged,
        propertyPanel_,
        &PropertyPanel::setSelectedPoint
    );
    connect(propertyPanel_, &PropertyPanel::pointSelected, this, [this](int pointIndex) {
        canvas_->setSelectedPointIndex(
            pointIndex >= 0 ? std::optional<size_t>(static_cast<size_t>(pointIndex)) : std::nullopt
        );
    });
    connect(structurePanel_, &StructurePanel::selectionChanged, this, &MainWindow::setSelection);
    connect(
        structurePanel_,
        &StructurePanel::removeLayerRequested,
        this,
        [this](const LayerPath& layerPath) {
            WaveDocument edited              = document_;
            std::vector<WaveLayer>* siblings = layerSiblings(edited.layers, layerPath);
            if (! siblings || layerPath.back() >= siblings->size()) {
                return;
            }
            const bool group = isGroup((*siblings)[layerPath.back()]);
            siblings->erase(siblings->begin() + static_cast<std::ptrdiff_t>(layerPath.back()));
            applyDocumentMutation(
                std::move(edited),
                LayerPath{},
                std::nullopt,
                group ? tr("Remove Group") : tr("Remove Layer")
            );
        }
    );
    connect(
        structurePanel_,
        &StructurePanel::duplicateLayerRequested,
        this,
        [this](const LayerPath& layerPath) {
            WaveDocument edited              = document_;
            std::vector<WaveLayer>* siblings = layerSiblings(edited.layers, layerPath);
            if (! siblings || layerPath.back() >= siblings->size()) {
                return;
            }
            WaveLayer copy = (*siblings)[layerPath.back()];
            copy.name += tr(" Copy");
            const bool group = isGroup(copy);
            siblings->insert(
                siblings->begin() + static_cast<std::ptrdiff_t>(layerPath.back() + 1),
                std::move(copy)
            );
            LayerPath inserted = layerPath;
            inserted.back() += 1;
            applyDocumentMutation(
                std::move(edited),
                inserted,
                std::nullopt,
                group ? tr("Duplicate Group") : tr("Duplicate Layer")
            );
        }
    );
    connect(
        structurePanel_, &StructurePanel::groupLayerRequested, this, &MainWindow::groupLayer
    );
    connect(structurePanel_, &StructurePanel::ungroupRequested, this, &MainWindow::ungroupLayer);
    connect(
        structurePanel_,
        &StructurePanel::addSegmentRequested,
        this,
        [this](const LayerPath& layerPath) {
            setSelection(layerPath, std::nullopt);
            addSegment(SegmentType::Dc);
        }
    );
    connect(
        structurePanel_,
        &StructurePanel::removeSegmentRequested,
        this,
        [this](const LayerPath& layerPath, size_t segmentIndex) {
            WaveDocument edited = document_;
            WaveLayer* layer    = layerAtPath(edited.layers, layerPath);
            if (! layer || isGroup(*layer) || segmentIndex >= layer->segments.size()) {
                return;
            }
            layer->segments.erase(
                layer->segments.begin() + static_cast<std::ptrdiff_t>(segmentIndex)
            );
            applyDocumentMutation(
                std::move(edited), layerPath, std::nullopt, tr("Remove Segment")
            );
        }
    );
    connect(
        structurePanel_,
        &StructurePanel::duplicateSegmentRequested,
        this,
        [this](const LayerPath& layerPath, size_t segmentIndex) {
            WaveDocument edited = document_;
            WaveLayer* layer    = layerAtPath(edited.layers, layerPath);
            if (! layer || isGroup(*layer) || segmentIndex >= layer->segments.size()) {
                return;
            }
            layer->segments.insert(
                layer->segments.begin() + static_cast<std::ptrdiff_t>(segmentIndex + 1),
                layer->segments[segmentIndex]
            );
            applyDocumentMutation(
                std::move(edited), layerPath, segmentIndex + 1, tr("Duplicate Segment")
            );
        }
    );
    connect(
        structurePanel_,
        &StructurePanel::layerEnabledChanged,
        this,
        [this](const LayerPath& layerPath, bool enabled) {
            WaveDocument edited = document_;
            WaveLayer* layer    = layerAtPath(edited.layers, layerPath);
            if (! layer) {
                return;
            }
            layer->enabled = enabled;
            applyDocumentMutation(std::move(edited), layerPath, std::nullopt, tr("Toggle Layer"));
        }
    );
    connect(
        structurePanel_,
        &StructurePanel::documentReordered,
        this,
        [this](const WaveDocument& edited) {
            applyDocumentMutation(edited, LayerPath{}, std::nullopt, tr("Reorder Structure"));
        }
    );
    connect(
        propertyPanel_,
        &PropertyPanel::segmentChanged,
        this,
        [this](const LayerPath& layerPath, size_t segmentIndex, const Segment& segment) {
            // Property editors commonly commit from focusOutEvent. Rebuilding the
            // panel synchronously would delete the focused widget while Qt is
            // still dispatching that event.
            QTimer::singleShot(0, this, [this, layerPath, segmentIndex, segment] {
                replaceSegment(layerPath, segmentIndex, segment);
            });
        }
    );
    connect(
        propertyPanel_,
        &PropertyPanel::layerChanged,
        this,
        [this](const LayerPath& layerPath, const WaveLayer& layer) {
            // Deferred for the same reason: the name field commits when it loses
            // focus, and the rebuild that follows would delete it mid-event.
            QTimer::singleShot(0, this, [this, layerPath, layer] {
                replaceLayer(layerPath, layer);
            });
        }
    );
    connect(
        propertyPanel_,
        &PropertyPanel::documentChanged,
        this,
        [this](const WaveDocument& document) {
            QTimer::singleShot(0, this, [this, document] {
                applyDocumentMutation(
                    document,
                    selectedLayerPath_,
                    selectedSegmentIndex_,
                    tr("Edit Document Settings"),
                    QStringLiteral("document/rates")
                );
            });
        }
    );

    statusBar()->showMessage(tr("Ready"));
    if (! restoreGeometry(QSettings().value(QLatin1String(SettingsGeometryKey)).toByteArray())) {
        resize(1200, 800);
    }
    restoreState(QSettings().value(QLatin1String(SettingsWindowStateKey)).toByteArray());
    WaveDocument initialDocument;
    initialDocument.name = tr("Untitled");
    WaveLayer initialLayer;
    initialLayer.name = tr("Layer 1");
    initialDocument.layers.push_back(std::move(initialLayer));
    setDocument(std::move(initialDocument), {});
    updateWindowTitle();
}

MainWindow::~MainWindow() {
    // ~QUndoStack emits cleanChanged/indexChanged while the child objects are
    // deleted in ~QObject, i.e. after this class's members (filePath_,
    // document_, ...) have already been destroyed. Sever all connections into
    // this object first so those emissions cannot touch destroyed members.
    undoStack_->disconnect(this);
}

bool MainWindow::openDocument(const QString& filePath) {
    if (filePath.isEmpty()) {
        return false;
    }

    const QFileInfo fileInfo(filePath);
    if (! fileInfo.exists() || ! fileInfo.isFile()) {
        QMessageBox::warning(this, tr("Open Document"), tr("The selected file does not exist."));
        return false;
    }
    if (fileInfo.suffix().compare(QLatin1String("zwj"), Qt::CaseInsensitive) != 0) {
        QMessageBox::warning(this, tr("Open Document"), tr("Only .zwj documents can be opened."));
        return false;
    }
    if (! confirmDiscardChanges()) {
        return false;
    }

    const auto result = zwjio::load(fileInfo.absoluteFilePath());
    if (result.error) {
        QMessageBox::critical(this, tr("Open Document"), result.error->message);
        return false;
    }
    if (! result.document) {
        QMessageBox::critical(this, tr("Open Document"), tr("The document could not be loaded."));
        return false;
    }

    setDocument(*result.document, fileInfo.absoluteFilePath());
    addRecentFile(fileInfo.absoluteFilePath());
    statusBar()->showMessage(tr("Opened %1").arg(fileInfo.fileName()), 4000);
    return true;
}

void MainWindow::setEditOnly(bool editOnly) {
    editOnly_ = editOnly;
    if (! editOnly_) {
        return;
    }

    // Listen for a request to show this window. The application that started
    // the editor has no portable way of raising another process's window, so it
    // asks the window to raise itself, which is something Qt can do.
    if (! filePath_.isEmpty()) {
        const QString name = zwe::editonly::socketName(filePath_);
        // A crashed predecessor leaves its socket behind and would block the
        // listen; nothing else uses this name.
        QLocalServer::removeServer(name);
        editOnlyServer_ = new QLocalServer(this);
        connect(editOnlyServer_, &QLocalServer::newConnection, this, [this]() {
            while (QLocalSocket* connection = editOnlyServer_->nextPendingConnection()) {
                connection->close();
                connection->deleteLater();
            }
            // A minimized window is not raised by raise() alone, so the
            // minimized flag has to go -- but only that one. showNormal() would
            // also clear Qt::WindowMaximized, which drops a maximized editor
            // back to its restored size on every further Edit click.
            if (isMinimized()) {
                setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
            }
            // Cheap for a window that is already visible, and the one thing that
            // brings back a window hidden by something other than minimizing.
            show();
            raise();
            // On Windows the caller has to have granted this process the right to
            // come forward (AllowSetForegroundWindow), otherwise the task bar
            // entry only flashes -- which is still the documented behaviour, not
            // a failure.
            activateWindow();
        });
        if (! editOnlyServer_->listen(name)) {
            qWarning(
                "Cannot listen for window requests on %s: %s",
                qPrintable(name),
                qPrintable(editOnlyServer_->errorString())
            );
        }
    }

    // Hidden rather than disabled: a greyed out "Open..." invites the question
    // why it is greyed out, while an editor that simply has no way to switch
    // documents reads as intended.
    for (QAction* action : {newAction_, openAction_, saveAsAction_}) {
        if (action) {
            action->setVisible(false);
            action->setEnabled(false);
        }
    }
    if (recentFilesMenu_) {
        recentFilesMenu_->menuAction()->setVisible(false);
        recentFilesMenu_->setEnabled(false);
    }
    setAcceptDrops(false);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (! confirmDiscardChanges()) {
        event->ignore();
        return;
    }
    writeSettings();
    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (editOnly_) {
        return;
    }
    const auto urls = event->mimeData()->urls();
    if (urls.size() == 1 && urls.first().isLocalFile() &&
        urls.first().toLocalFile().endsWith(QLatin1String(".zwj"), Qt::CaseInsensitive)) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    if (editOnly_) {
        return;
    }
    const auto urls = event->mimeData()->urls();
    if (urls.size() == 1 && openDocument(urls.first().toLocalFile())) {
        event->acceptProposedAction();
    }
}

void MainWindow::newDocument() {
    if (! confirmDiscardChanges()) {
        return;
    }

    WaveDocument document;
    document.name = tr("Untitled");
    WaveLayer firstLayer;
    firstLayer.name = tr("Layer 1");
    document.layers.push_back(std::move(firstLayer));
    setDocument(std::move(document), {});
    statusBar()->showMessage(tr("New document"), 4000);
}

void MainWindow::openDocumentDialog() {
    const QString startPath = QSettings().value(QLatin1String(SettingsOpenPathKey)).toString();
    const QString filePath =
        QFileDialog::getOpenFileName(this, tr("Open Document"), startPath, tr(DocumentFilter));
    if (! filePath.isEmpty() && openDocument(filePath)) {
        QSettings().setValue(
            QLatin1String(SettingsOpenPathKey), QFileInfo(filePath).absolutePath()
        );
    }
}

bool MainWindow::saveDocument() {
    if (filePath_.isEmpty()) {
        return saveDocumentAs();
    }

    if (const auto error = zwjio::save(document_, filePath_)) {
        QMessageBox::critical(this, tr("Save Document"), error->message);
        return false;
    }

    undoStack_->setClean();
    addRecentFile(filePath_);
    statusBar()->showMessage(tr("Saved %1").arg(QFileInfo(filePath_).fileName()), 4000);
    return true;
}

bool MainWindow::saveDocumentAs() {
    QString suggestedPath = filePath_;
    if (suggestedPath.isEmpty()) {
        const QString fileName =
            document_.name.isEmpty() ? tr("untitled.zwj") : document_.name + ".zwj";
        const QString directory = QSettings().value(QLatin1String(SettingsSavePathKey)).toString();
        suggestedPath = directory.isEmpty() ? fileName : QDir(directory).filePath(fileName);
    }

    QString filePath = QFileDialog::getSaveFileName(
        this, tr("Save Document As"), suggestedPath, tr(DocumentFilter)
    );
    if (filePath.isEmpty()) {
        return false;
    }
    if (! filePath.endsWith(QLatin1String(".zwj"), Qt::CaseInsensitive)) {
        filePath += QLatin1String(".zwj");
    }

    const QString newFilePath = QFileInfo(filePath).absoluteFilePath();
    if (document_.name.isEmpty() || document_.name == tr("Untitled")) {
        WaveDocument renamed = document_;
        renamed.name         = QFileInfo(newFilePath).completeBaseName();
        applyDocumentMutation(
            std::move(renamed), selectedLayerPath_, selectedSegmentIndex_, tr("Rename Document")
        );
    }
    if (const auto error = zwjio::save(document_, newFilePath)) {
        QMessageBox::critical(this, tr("Save Document"), error->message);
        return false;
    }

    filePath_ = newFilePath;
    QSettings().setValue(QLatin1String(SettingsSavePathKey), QFileInfo(filePath_).absolutePath());
    undoStack_->setClean();
    addRecentFile(filePath_);
    statusBar()->showMessage(tr("Saved %1").arg(QFileInfo(filePath_).fileName()), 4000);
    return true;
}

void MainWindow::importCsv() {
    // Only a leaf layer can take a segment; with a group selected the import
    // offers a new layer of its own.
    const bool hasSelectedLayer     = selectedLeaf() != nullptr;
    const QString selectedLayerName = hasSelectedLayer ? selectedLeaf()->name : QString{};
    ImportDialog dialog(document_.sampleRate, selectedLayerName, hasSelectedLayer, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const csvio::ImportResult result =
        csvio::importCsv(dialog.filePath(), dialog.sampleRate(), dialog.interpolation());
    if (result.error) {
        QMessageBox::critical(this, tr("Import CSV"), result.error->message);
        return;
    }
    if (! result.segment) {
        QMessageBox::critical(this, tr("Import CSV"), tr("The CSV file could not be imported."));
        return;
    }

    WaveDocument edited = document_;
    LayerPath targetLayer;
    if (dialog.target() == ImportDialog::Target::SelectedLayer && hasSelectedLayer) {
        targetLayer = selectedLayerPath_;
        layerAtPath(edited.layers, targetLayer)->segments.push_back(*result.segment);
    } else {
        WaveLayer layer;
        layer.name = tr("Imported %1").arg(edited.layers.size() + 1);
        layer.segments.push_back(*result.segment);
        edited.layers.push_back(std::move(layer));
        targetLayer = {edited.layers.size() - 1};
    }
    const size_t targetSegment = layerAtPath(edited.layers, targetLayer)->segments.size() - 1;
    applyDocumentMutation(std::move(edited), targetLayer, targetSegment, tr("Import CSV"));
    statusBar()->showMessage(tr("Imported %1").arg(QFileInfo(dialog.filePath()).fileName()), 4000);
}

void MainWindow::exportWaveform() {
    ExportDialog dialog(document_, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const ExportFormat format = dialog.format();
    const QString suffix      = ExportDialog::suffixFor(format);
    QString path              = dialog.filePath();
    if (! path.endsWith(suffix, Qt::CaseInsensitive)) {
        path += suffix;
    }
    WaveDocument edited                     = document_;
    edited.exportSettings.significantDigits = dialog.significantDigits();
    edited.exportSettings.format            = format;
    edited.exportSettings.lastExportPath    = QFileInfo(path).absoluteFilePath();

    const sampling::Progress progress = [this](size_t done, size_t total) {
        showRecalculationProgress(total > 0 ? static_cast<int>(done * 100 / total) : 0);
    };
    std::optional<QString> errorMessage;
    if (format == ExportFormat::Binary) {
        if (const auto error = binaryio::exportBinary(edited, path, dialog.sampleRate(), progress)) {
            errorMessage = error->message;
        }
    } else {
        if (const auto error = csvio::exportCsv(edited, path, dialog.sampleRate(), progress)) {
            errorMessage = error->message;
        }
    }
    showRecalculationProgress(100);
    if (errorMessage) {
        QMessageBox::critical(this, tr("Export Waveform"), *errorMessage);
        return;
    }

    applyDocumentMutation(
        std::move(edited), selectedLayerPath_, selectedSegmentIndex_, tr("Update Export Settings")
    );
    statusBar()->showMessage(tr("Exported %1").arg(QFileInfo(path).fileName()), 4000);
}

void MainWindow::scaleWaveform() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Scale Entire Waveform"));
    dialog.setModal(true);
    dialog.setMinimumWidth(420);

    auto* layout = new QVBoxLayout(&dialog);
    auto* intro  = new QLabel(
        tr("Scale all layers and segments at once. The document Value Rate is not changed."),
        &dialog
    );
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* form            = new QFormLayout;
    auto* amplitudeFactor = new QDoubleSpinBox(&dialog);
    amplitudeFactor->setObjectName(QStringLiteral("amplitudeScaleSpin"));
    amplitudeFactor->setDecimals(6);
    amplitudeFactor->setRange(-1.0e9, 1.0e9);
    amplitudeFactor->setValue(1.0);
    amplitudeFactor->setToolTip(tr("Multiplies all signal values and offsets."));
    form->addRow(tr("Amplitude factor"), amplitudeFactor);

    auto* timeFactor = new QDoubleSpinBox(&dialog);
    timeFactor->setObjectName(QStringLiteral("timeScaleSpin"));
    timeFactor->setDecimals(6);
    timeFactor->setRange(0.000001, 1.0e9);
    timeFactor->setValue(1.0);
    timeFactor->setToolTip(
        tr("Multiplies durations and time constants; frequencies are divided by this factor.")
    );
    form->addRow(tr("Time factor"), timeFactor);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    if (qFuzzyCompare(amplitudeFactor->value(), 1.0) && qFuzzyCompare(timeFactor->value(), 1.0)) {
        return;
    }

    WaveDocument edited = scaleDocument(document_, amplitudeFactor->value(), timeFactor->value());
    applyDocumentMutation(
        std::move(edited), selectedLayerPath_, selectedSegmentIndex_, tr("Scale Entire Waveform")
    );
    canvas_->fitToDocument();
}

void MainWindow::showAbout() {
    QMessageBox::about(
        this,
        tr("About Zahner Wave Editor"),
        // The full build identity, not just the release number: this is the
        // string a user is asked for in a bug report.
        tr("Zahner Wave Editor %1\n\nA waveform editor for Zahner Lab wave jobs.")
            .arg(zwe::applicationVersionFull())
    );
}

void MainWindow::showHelp() {
    if (! helpBrowser_) {
        helpBrowser_ = new HelpBrowser(this);
    }
    helpBrowser_->openOverview();
    helpBrowser_->show();
    helpBrowser_->raise();
    helpBrowser_->activateWindow();
}

void MainWindow::showFormulaHelp() {
    if (! helpBrowser_) {
        helpBrowser_ = new HelpBrowser(this);
    }
    helpBrowser_->openFormulaReference();
    helpBrowser_->show();
    helpBrowser_->raise();
    helpBrowser_->activateWindow();
}

void MainWindow::showLicenses() {
    if (! licenses_) {
        licenses_ = new LicenseViewerDialog(this);
    }
    licenses_->show();
    licenses_->raise();
    licenses_->activateWindow();
}

void MainWindow::showSettings() {
    SettingsDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    // Read back rather than asking the dialog: the settings are the single
    // source of truth, and the appearance is applied to the running instance.
    darkTheme_  = appsettings::darkTheme();
    blueAccent_ = appsettings::accent() == theme::Accent::Blue;
    applyTheme();

    if (dialog.restartRequired()) {
        QMessageBox::information(
            this,
            tr("Settings"),
            tr("The selected language will be used the next time the Zahner Wave Editor starts.")
        );
    }
}

void MainWindow::applyTheme() {
    const auto accent = blueAccent_ ? theme::Accent::Blue : theme::Accent::Red;
    theme::apply(darkTheme_, accent);
    const QColor accentColor = theme::accentColor(accent);
    for (QAction* action : findChildren<QAction*>()) {
        if (! action->icon().isNull()) {
            action->setIcon(theme::accentIcon(action->icon(), accentColor));
        }
    }
    structurePanel_->refreshIcons();
    QApplication::setWindowIcon(theme::applicationIcon(darkTheme_));
    setWindowIcon(theme::applicationIcon(darkTheme_));
}

bool MainWindow::confirmDiscardChanges() {
    if (! dirty_) {
        return true;
    }

    const auto choice = QMessageBox::warning(
        this,
        tr("Unsaved Changes"),
        tr("The current document has unsaved changes. Do you want to save them?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save
    );
    if (choice == QMessageBox::Save) {
        return saveDocument();
    }
    return choice == QMessageBox::Discard;
}

void MainWindow::setDocument(WaveDocument document, const QString& filePath) {
    if (document.id.isEmpty()) {
        document.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    filePath_ = filePath;
    undoStack_->clear();
    undoStack_->setClean();
    const LayerPath initialLayer = document.layers.empty() ? LayerPath{} : LayerPath{0};
    const std::optional<size_t> initialSegment =
        ! initialLayer.empty() && ! isGroup(document.layers.front()) &&
                ! document.layers.front().segments.empty()
            ? std::optional<size_t>{0}
            : std::nullopt;
    applyDocumentState(std::move(document), initialLayer, initialSegment, true);
    saveAction_->setEnabled(true);
}

void MainWindow::applyDocumentState(
    WaveDocument document,
    const LayerPath& layerPath,
    std::optional<size_t> segmentIndex,
    bool resetView
) {
    QElapsedTimer timer;
    timer.start();
    document_             = std::move(document);
    selectedLayerPath_    = layerPath;
    selectedSegmentIndex_ = segmentIndex;
    if (sampleRateSpin_) {
        const QSignalBlocker blocker(sampleRateSpin_);
        sampleRateSpin_->setValue(document_.sampleRate);
    }
    const WaveLayer* layer = layerAtPath(document_.layers, selectedLayerPath_);
    if (! layer) {
        selectedLayerPath_.clear();
        selectedSegmentIndex_.reset();
    } else if (isGroup(*layer) || ! selectedSegmentIndex_ ||
               *selectedSegmentIndex_ >= layer->segments.size()) {
        selectedSegmentIndex_.reset();
    }
    canvas_->setDocument(document_, resetView);
    canvas_->setSelectedLayerPath(selectedLayerPath_);
    canvas_->setSelectedSegmentIndex(selectedSegmentIndex_);
    structurePanel_->setDocument(document_);
    structurePanel_->setSelection(selectedLayerPath_, selectedSegmentIndex_);
    propertyPanel_->setDocument(document_);
    propertyPanel_->setSelection(selectedLayerPath_, selectedSegmentIndex_);
    updateStructureActionState();
    updateWindowTitle();
    qCDebug(zwePerfWindow) << "applyDocumentState:" << timer.elapsed() << "ms";
}

void MainWindow::applyDocumentMutation(
    WaveDocument document,
    const LayerPath& layerPath,
    std::optional<size_t> segmentIndex,
    const QString& text,
    QString mergeKey
) {
    if (document == document_) {
        return;
    }

    const DocumentSelection beforeSelection{selectedLayerPath_, selectedSegmentIndex_};
    const DocumentSelection afterSelection{layerPath, segmentIndex};
    undoStack_->push(new DocumentEditCommand(
        document_,
        std::move(document),
        beforeSelection,
        afterSelection,
        [this](const WaveDocument& state, const DocumentSelection& selection) {
            applyDocumentState(state, selection.layerPath, selection.segmentIndex, false);
        },
        text,
        std::move(mergeKey)
    ));
}

const WaveLayer* MainWindow::selectedLeaf() const {
    const WaveLayer* layer = layerAtPath(document_.layers, selectedLayerPath_);
    return layer && ! isGroup(*layer) ? layer : nullptr;
}

void MainWindow::replaceSegment(
    const LayerPath& layerPath, size_t segmentIndex, const Segment& segment, bool continuous
) {
    WaveDocument edited = document_;
    WaveLayer* layer    = layerAtPath(edited.layers, layerPath);
    if (! layer || isGroup(*layer) || segmentIndex >= layer->segments.size()) {
        return;
    }
    layer->segments[segmentIndex] = segment;
    QString mergeKey              = segmentMergeKey(layerPath, segmentIndex, segment);
    if (continuous && mergeKey.isEmpty()) {
        mergeKey = QStringLiteral("canvas/%1/%2")
                       .arg(layerPathKey(layerPath))
                       .arg(segmentIndex);
    }
    applyDocumentMutation(
        std::move(edited), layerPath, segmentIndex, tr("Edit Segment"), std::move(mergeKey)
    );
}

void MainWindow::replaceLayer(const LayerPath& layerPath, const WaveLayer& layer) {
    WaveDocument edited = document_;
    WaveLayer* target   = layerAtPath(edited.layers, layerPath);
    if (! target || isGroup(*target) != isGroup(layer)) {
        return;
    }
    // Name, enabled state, mode and edge policy come from the property panel;
    // the content stays whatever the document holds.
    const std::vector<Segment> segments   = target->segments;
    const std::vector<WaveLayer> children = target->children;
    *target                               = layer;
    target->segments                      = segments;
    target->children                      = children;
    // Renaming merges per layer, so typing a name is one undo step rather than
    // one per commit of the same field.
    applyDocumentMutation(
        std::move(edited),
        layerPath,
        selectedSegmentIndex_,
        isGroup(layer) ? tr("Edit Group") : tr("Edit Layer"),
        QStringLiteral("layer/%1").arg(layerPathKey(layerPath))
    );
}

QString MainWindow::segmentMergeKey(
    const LayerPath& layerPath, size_t segmentIndex, const Segment& segment
) const {
    const WaveLayer* layer = layerAtPath(document_.layers, layerPath);
    if (! layer || isGroup(*layer) || segmentIndex >= layer->segments.size()) {
        return {};
    }
    const Segment& current = layer->segments[segmentIndex];
    const QString prefix =
        QStringLiteral("segment/%1/%2").arg(layerPathKey(layerPath)).arg(segmentIndex);
    if (current.duration != segment.duration && current.repeat == segment.repeat &&
        current.params == segment.params) {
        return prefix + QStringLiteral("/duration");
    }
    if (current.repeat != segment.repeat && current.duration == segment.duration &&
        current.params == segment.params) {
        return prefix + QStringLiteral("/repeat");
    }
    // A voltammetry parameter edit brings the sweep's own duration with it, so a
    // duration that matches the edited parameters belongs to that single edit
    // instead of counting as a second change that would block the merge.
    const bool fittedDuration =
        current.duration != segment.duration && segment.duration == sweepDuration(segment);
    if ((current.duration != segment.duration && ! fittedDuration) ||
        current.repeat != segment.repeat) {
        return {};
    }
    const QString field = changedParameterField(current, segment);
    return field.isEmpty() ? QString{} : prefix + QLatin1Char('/') + field;
}

void MainWindow::setSelection(const LayerPath& layerPath, std::optional<size_t> segmentIndex) {
    const WaveLayer* layer = layerAtPath(document_.layers, layerPath);
    if (! layer) {
        selectedLayerPath_.clear();
        selectedSegmentIndex_.reset();
    } else {
        selectedLayerPath_    = layerPath;
        selectedSegmentIndex_ = ! isGroup(*layer) && segmentIndex &&
                                        *segmentIndex < layer->segments.size()
                                    ? segmentIndex
                                    : std::nullopt;
    }
    canvas_->setSelectedLayerPath(selectedLayerPath_);
    canvas_->setSelectedSegmentIndex(selectedSegmentIndex_);
    structurePanel_->setSelection(selectedLayerPath_, selectedSegmentIndex_);
    propertyPanel_->setSelection(selectedLayerPath_, selectedSegmentIndex_);
    updateStructureActionState();
}

void MainWindow::addLayerNode(LayerKind kind) {
    WaveDocument edited              = document_;
    const WaveLayer* selected       = layerAtPath(document_.layers, selectedLayerPath_);
    std::vector<WaveLayer>* level    = &edited.layers;
    LayerPath levelPath;
    if (selected && isGroup(*selected)) {
        // A selected group is the container one is working in.
        level     = &layerAtPath(edited.layers, selectedLayerPath_)->children;
        levelPath = selectedLayerPath_;
    } else if (selectedLayerPath_.size() > 1) {
        // Otherwise the new node joins the selection's own level.
        levelPath.assign(selectedLayerPath_.begin(), selectedLayerPath_.end() - 1);
        level = &layerAtPath(edited.layers, levelPath)->children;
    }

    WaveLayer node;
    node.kind = kind;
    node.name = kind == LayerKind::Group ? tr("Group %1").arg(level->size() + 1)
                                         : tr("Layer %1").arg(level->size() + 1);
    level->push_back(std::move(node));
    LayerPath inserted = levelPath;
    inserted.push_back(level->size() - 1);
    applyDocumentMutation(
        std::move(edited),
        inserted,
        std::nullopt,
        kind == LayerKind::Group ? tr("Add Group") : tr("Add Layer")
    );
}

void MainWindow::groupLayer(const LayerPath& layerPath) {
    WaveDocument edited              = document_;
    std::vector<WaveLayer>* siblings = layerSiblings(edited.layers, layerPath);
    if (! siblings || layerPath.empty() || layerPath.back() >= siblings->size()) {
        return;
    }
    WaveLayer group;
    group.kind = LayerKind::Group;
    group.name = tr("Group of %1").arg((*siblings)[layerPath.back()].name);
    group.children.push_back(std::move((*siblings)[layerPath.back()]));
    (*siblings)[layerPath.back()] = std::move(group);
    applyDocumentMutation(std::move(edited), layerPath, std::nullopt, tr("Group Layer"));
}

void MainWindow::ungroupLayer(const LayerPath& layerPath) {
    WaveDocument edited              = document_;
    std::vector<WaveLayer>* siblings = layerSiblings(edited.layers, layerPath);
    if (! siblings || layerPath.empty() || layerPath.back() >= siblings->size()) {
        return;
    }
    WaveLayer group = std::move((*siblings)[layerPath.back()]);
    if (! isGroup(group)) {
        return;
    }
    const auto position = siblings->begin() + static_cast<std::ptrdiff_t>(layerPath.back());
    siblings->erase(position);
    // The children take the group's place, in their own order.
    siblings->insert(
        siblings->begin() + static_cast<std::ptrdiff_t>(layerPath.back()),
        std::make_move_iterator(group.children.begin()),
        std::make_move_iterator(group.children.end())
    );
    LayerPath remaining;
    if (! group.children.empty()) {
        remaining = layerPath;
    }
    applyDocumentMutation(std::move(edited), remaining, std::nullopt, tr("Ungroup"));
}

void MainWindow::addSegment(SegmentType type) {
    const WaveLayer* selected = selectedLeaf();
    if (! selected) {
        return;
    }
    WaveDocument edited       = document_;
    auto& segments            = layerAtPath(edited.layers, selectedLayerPath_)->segments;
    const size_t segmentIndex = selectedSegmentIndex_ && *selectedSegmentIndex_ < segments.size()
                                    ? *selectedSegmentIndex_ + 1
                                    : segments.size();
    segments.insert(
        segments.begin() + static_cast<std::ptrdiff_t>(segmentIndex),
        initializeSegmentContinuity(*selected, segmentIndex, defaultSegment(type))
    );
    applyDocumentMutation(std::move(edited), selectedLayerPath_, segmentIndex, tr("Add Segment"));
    canvas_->ensureDocumentVisible();
}

void MainWindow::insertSegment(SegmentType type, bool afterSelection) {
    const WaveLayer* selected = selectedLeaf();
    if (! selected || ! selectedSegmentIndex_ ||
        *selectedSegmentIndex_ >= selected->segments.size()) {
        return;
    }
    const size_t insertionIndex = *selectedSegmentIndex_ + (afterSelection ? 1 : 0);
    WaveDocument edited         = document_;
    auto& segments              = layerAtPath(edited.layers, selectedLayerPath_)->segments;
    segments.insert(
        segments.begin() + static_cast<std::ptrdiff_t>(insertionIndex),
        initializeSegmentContinuity(*selected, insertionIndex, defaultSegment(type))
    );
    applyDocumentMutation(
        std::move(edited),
        selectedLayerPath_,
        insertionIndex,
        afterSelection ? tr("Insert Segment After") : tr("Insert Segment Before")
    );
    canvas_->ensureDocumentVisible();
}

void MainWindow::moveSelectedSegment(int offset) {
    const WaveLayer* selected = selectedLeaf();
    if (! selected || ! selectedSegmentIndex_ ||
        *selectedSegmentIndex_ >= selected->segments.size()) {
        return;
    }
    const size_t segmentIndex = *selectedSegmentIndex_;
    const auto destination    = static_cast<std::ptrdiff_t>(segmentIndex) + offset;
    if (destination < 0 ||
        destination >= static_cast<std::ptrdiff_t>(selected->segments.size())) {
        return;
    }
    WaveDocument edited = document_;
    auto& segments      = layerAtPath(edited.layers, selectedLayerPath_)->segments;
    std::swap(segments[segmentIndex], segments[static_cast<size_t>(destination)]);
    applyDocumentMutation(
        std::move(edited),
        selectedLayerPath_,
        static_cast<size_t>(destination),
        tr("Move Segment")
    );
}

void MainWindow::removeSelectedSegment() {
    const WaveLayer* selected = selectedLeaf();
    if (! selected || ! selectedSegmentIndex_ ||
        *selectedSegmentIndex_ >= selected->segments.size()) {
        return;
    }
    WaveDocument edited = document_;
    auto& segments      = layerAtPath(edited.layers, selectedLayerPath_)->segments;
    segments.erase(segments.begin() + static_cast<std::ptrdiff_t>(*selectedSegmentIndex_));
    applyDocumentMutation(
        std::move(edited), selectedLayerPath_, std::nullopt, tr("Remove Segment")
    );
}

void MainWindow::updateStructureActionState() {
    const WaveLayer* leaf = selectedLeaf();
    // A group holds layers, not segments, so segment actions stay off for it.
    if (addSegmentAction_) {
        addSegmentAction_->setEnabled(leaf != nullptr);
    }
    const bool hasSegment =
        leaf && selectedSegmentIndex_ && *selectedSegmentIndex_ < leaf->segments.size();
    if (removeSegmentAction_) {
        removeSegmentAction_->setEnabled(hasSegment);
    }
    if (insertBeforeAction_) {
        insertBeforeAction_->setEnabled(hasSegment);
    }
    if (insertAfterAction_) {
        insertAfterAction_->setEnabled(hasSegment);
    }
    if (moveEarlierAction_) {
        moveEarlierAction_->setEnabled(hasSegment && *selectedSegmentIndex_ > 0);
    }
    if (moveLaterAction_) {
        moveLaterAction_->setEnabled(
            hasSegment && *selectedSegmentIndex_ + 1 < leaf->segments.size()
        );
    }
}

void MainWindow::showRecalculationProgress(int percent) {
    if (percent >= 100) {
        recalcProgress_->hide();
        return;
    }
    recalcProgress_->setValue(percent);
    if (! recalcProgress_->isVisible()) {
        recalcProgress_->show();
    }
    // Recalculations run synchronously on the GUI thread, so the event loop
    // cannot repaint the bar; paint it (and the status bar around it)
    // directly to keep the progress visible.
    statusBar()->repaint();
}

void MainWindow::setDocumentDirty(bool dirty) {
    if (dirty_ == dirty) {
        return;
    }
    dirty_ = dirty;
    updateWindowTitle();
}

void MainWindow::updateWindowTitle() {
    QString documentTitle = QFileInfo(filePath_).completeBaseName();
    if (documentTitle.isEmpty()) {
        documentTitle = document_.name.isEmpty() ? tr("Untitled") : document_.name;
    }
    // The full build identity, as Zahner Lab shows in its own title bar: a build
    // off a release tag reads "v26.32.0", anything else carries the commit
    // distance and hash and is recognizable as an intermediate build.
    setWindowTitle(tr("%1%2 - Zahner Wave Editor %3")
                       .arg(
                           documentTitle,
                           dirty_ ? QLatin1String(" *") : QString{},
                           zwe::applicationVersionFull()
                       ));
}

void MainWindow::updateRecentFilesMenu() {
    recentFilesMenu_->clear();
    const QStringList files = recentFiles();
    if (files.isEmpty()) {
        auto* emptyAction = recentFilesMenu_->addAction(tr("No recent files"));
        emptyAction->setEnabled(false);
        return;
    }

    for (const QString& filePath : files) {
        auto* action = recentFilesMenu_->addAction(QFileInfo(filePath).fileName());
        action->setToolTip(filePath);
        connect(action, &QAction::triggered, this, [this, filePath] { openDocument(filePath); });
    }
}

void MainWindow::addRecentFile(const QString& filePath) {
    if (editOnly_) {
        // The list is not reachable in this mode and is shared with the
        // standalone editor, so a document opened on behalf of another
        // application has no business appearing in the user's own history.
        return;
    }
    QStringList files = recentFiles();
    files.removeAll(filePath);
    files.prepend(filePath);
    while (files.size() > MaxRecentFiles) {
        files.removeLast();
    }
    QSettings().setValue(QLatin1String(SettingsRecentFilesKey), files);
    updateRecentFilesMenu();
}

QStringList MainWindow::recentFiles() const {
    QStringList files = QSettings().value(QLatin1String(SettingsRecentFilesKey)).toStringList();
    files.erase(
        std::remove_if(
            files.begin(),
            files.end(),
            [](const QString& filePath) { return ! QFileInfo::exists(filePath); }
        ),
        files.end()
    );
    return files;
}

void MainWindow::writeSettings() const {
    QSettings().setValue(QLatin1String(SettingsGeometryKey), saveGeometry());
    QSettings().setValue(QLatin1String(SettingsWindowStateKey), saveState());
}

}  // namespace zwe
