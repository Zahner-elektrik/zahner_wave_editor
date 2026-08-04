// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include "propertypanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <type_traits>

#include "core/expression.h"
#include "core/sampling.h"
#include "engineeringedit.h"
#include "engineeringnotation.h"

namespace zwe {

namespace {

// Beyond this a Points segment is no longer something one edits row by row: the
// table would be slower to build than it is useful, and such a segment comes from
// an import rather than from typing.
constexpr size_t MaximumListedPoints = 512;

class FormulaEdit final : public QLineEdit {
public:
    using QLineEdit::QLineEdit;

protected:
    void keyPressEvent(QKeyEvent* event) override {
        // On keyboard layouts where circumflex is a dead key, "^" followed by
        // "2" is composed into U+00B2. muParser needs the literal ASCII "^".
        if (event->key() == Qt::Key_Dead_Circumflex || event->key() == Qt::Key_AsciiCircum) {
            insert(QStringLiteral("^"));
            event->accept();
            return;
        }
        QLineEdit::keyPressEvent(event);
    }
};

QString typeName(SegmentType type) {
    switch (type) {
        case SegmentType::Dc:
            return PropertyPanel::tr("DC");
        case SegmentType::Ramp:
            return PropertyPanel::tr("Ramp");
        case SegmentType::Sine:
            return PropertyPanel::tr("Sine");
        case SegmentType::Square:
            return PropertyPanel::tr("Square");
        case SegmentType::Triangle:
            return PropertyPanel::tr("Triangle");
        case SegmentType::Pulse:
            return PropertyPanel::tr("Pulse");
        case SegmentType::Exponential:
            return PropertyPanel::tr("Exponential");
        case SegmentType::Formula:
            return PropertyPanel::tr("Formula");
        case SegmentType::Points:
            return PropertyPanel::tr("Points");
        case SegmentType::Chirp:
            return PropertyPanel::tr("Chirp");
        case SegmentType::Ricker:
            return PropertyPanel::tr("Ricker wavelet");
        case SegmentType::Window:
            return PropertyPanel::tr("Window");
        // The method names IM7 gives its jobs, so a document reads the same way
        // in both places.
        case SegmentType::Npv:
            return PropertyPanel::tr("Normal Pulse Voltammetry");
        case SegmentType::Dpv:
            return PropertyPanel::tr("Differential Pulse Voltammetry");
        case SegmentType::Swv:
            return PropertyPanel::tr("Square Wave Voltammetry");
    }
    return {};
}

// Writes into whichever of the three voltammetry parameter sets a segment
// holds. They share most of their fields, and a property panel setter has to
// reach the concrete alternative without its caller knowing which one it is.
template <typename Write>
void editSweep(Segment& segment, Write&& write) {
    std::visit(
        [&write](auto& params) {
            using Params = std::decay_t<decltype(params)>;
            if constexpr (std::is_same_v<Params, NpvParams> ||
                          std::is_same_v<Params, SwvParams> ||
                          std::is_same_v<Params, DpvParams>) {
                write(params);
            }
        },
        segment.params
    );
}

// The same for the two methods built from a staircase with a pulse on it, which
// share their step and pulse timing.
template <typename Write>
void editPulseSweep(Segment& segment, Write&& write) {
    std::visit(
        [&write](auto& params) {
            using Params = std::decay_t<decltype(params)>;
            if constexpr (std::is_same_v<Params, NpvParams> || std::is_same_v<Params, DpvParams>) {
                write(params);
            }
        },
        segment.params
    );
}

// The shapes in the order of WindowParams::Shape, so a combo box index is the
// enumerator.
QStringList windowShapeNames() {
    return {
        PropertyPanel::tr("Rectangular"),
        PropertyPanel::tr("Hann"),
        PropertyPanel::tr("Hamming"),
        PropertyPanel::tr("Blackman"),
        PropertyPanel::tr("Blackman-Harris"),
        PropertyPanel::tr("Tukey"),
        PropertyPanel::tr("Gauss"),
        PropertyPanel::tr("Kaiser"),
        PropertyPanel::tr("Trapezoid")
    };
}

}  // namespace

PropertyPanel::PropertyPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    form_ = new QFormLayout;
    layout->addLayout(form_);
    layout->addStretch();
    rebuildForm();
}

void PropertyPanel::setDocument(const WaveDocument& document) {
    // Dragging or nudging a point sends a new document for every mouse move. A
    // full form rebuild per event would tear down and recreate a table of up to
    // 512 rows, so those edits only write the cells that changed.
    const bool refreshOnly = pointsTable_ && onlyPointsMoved(document);
    document_              = document;
    if (refreshOnly) {
        refreshPointsTable();
        return;
    }
    rebuildForm();
}

void PropertyPanel::setSelection(const LayerPath& layerPath, std::optional<size_t> segmentIndex) {
    if (layerPath_ != layerPath || segmentIndex_ != segmentIndex) {
        selectedPoint_.reset();
    }
    layerPath_    = layerPath;
    segmentIndex_ = segmentIndex;
    rebuildForm();
}

const WaveLayer* PropertyPanel::selectedLayer() const {
    return layerAtPath(document_.layers, layerPath_);
}

const Segment* PropertyPanel::selectedSegment() const {
    const WaveLayer* layer = selectedLayer();
    if (! layer || isGroup(*layer) || ! segmentIndex_ ||
        *segmentIndex_ >= layer->segments.size()) {
        return nullptr;
    }
    return &layer->segments[*segmentIndex_];
}

void PropertyPanel::rebuildForm() {
    // Every widget of the old form is about to be dropped, this one included.
    pointsTable_ = nullptr;
    while (form_->count() > 0) {
        QLayoutItem* item = form_->takeAt(0);
        if (QWidget* widget = item->widget()) {
            // A rebuild is usually triggered from a signal of one of these
            // editors (editingFinished -> document edit -> setDocument).
            // Deleting the emitting widget while its event handler is still on
            // the stack is a use-after-free, which showed up as focus-handling
            // crashes on the next click. Detach the widget right away and
            // leave the destruction to the event loop.
            widget->hide();
            widget->setParent(nullptr);
            widget->deleteLater();
        }
        delete item;
    }
    const Segment* selected = selectedSegment();
    if (! selected) {
        if (const WaveLayer* layer = selectedLayer()) {
            addLayerForm(*layer);
            return;
        }
        auto* hint = new QLabel(tr("Select a layer or a segment to edit its properties."), this);
        hint->setWordWrap(true);
        hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        form_->addRow(hint);
        return;
    }

    const Segment& segment = *selected;
    auto* type             = new QLabel(typeName(segmentType(segment)), this);
    type->setObjectName(QStringLiteral("segmentTypeLabel"));
    form_->addRow(tr("Type"), type);

    addNumberField(tr("Duration (s)"), segment.duration, [](Segment& edited, double value) {
        if (value > 0.0) {
            edited.duration = value;
        }
    });
    auto* repeat = new QSpinBox(this);
    repeat->setRange(1, 1'000'000);
    repeat->setValue(segment.repeat);
    connect(repeat, &QSpinBox::valueChanged, this, [this](int value) {
        const Segment* current = selectedSegment();
        if (! current) {
            return;
        }
        Segment edited = *current;
        edited.repeat  = value;
        emitChanged(std::move(edited));
    });
    form_->addRow(tr("Repeat"), repeat);

    std::visit(
        [this, &segment](const auto& params) {
            using Params = std::decay_t<decltype(params)>;
            if constexpr (std::is_same_v<Params, DcParams>) {
                addNumberField(tr("Value"), params.value, [](Segment& segment, double value) {
                    std::get<DcParams>(segment.params).value = value;
                });
            } else if constexpr (std::is_same_v<Params, RampParams>) {
                addNumberField(
                    tr("Start value"),
                    params.startValue,
                    [](Segment& segment, double value) {
                        std::get<RampParams>(segment.params).startValue = value;
                    }
                );
                addNumberField(
                    tr("End value"),
                    params.endValue,
                    [](Segment& segment, double value) {
                        std::get<RampParams>(segment.params).endValue = value;
                    }
                );
            } else if constexpr (std::is_same_v<Params, SineParams> ||
                                 std::is_same_v<Params, SquareParams> ||
                                 std::is_same_v<Params, TriangleParams>) {
                addNumberField(
                    tr("Amplitude"),
                    params.amplitude,
                    [](Segment& segment, double value) {
                        std::visit(
                            [value](auto& actual) {
                                using Actual = std::decay_t<decltype(actual)>;
                                if constexpr (std::is_same_v<Actual, SineParams> ||
                                              std::is_same_v<Actual, SquareParams> ||
                                              std::is_same_v<Actual, TriangleParams>) {
                                    actual.amplitude = value;
                                }
                            },
                            segment.params
                        );
                    }
                );
                addNumberField(
                    tr("Frequency (Hz)"),
                    params.frequency,
                    [](Segment& segment, double value) {
                        if (value > 0.0) {
                            std::visit(
                                [value](auto& actual) {
                                    using Actual = std::decay_t<decltype(actual)>;
                                    if constexpr (std::is_same_v<Actual, SineParams> ||
                                                  std::is_same_v<Actual, SquareParams> ||
                                                  std::is_same_v<Actual, TriangleParams>) {
                                        actual.frequency = value;
                                    }
                                },
                                segment.params
                            );
                        }
                    }
                );
                addNumberField(
                    tr("Phase (deg)"),
                    params.phaseDeg,
                    [](Segment& segment, double value) {
                        std::visit(
                            [value](auto& actual) {
                                using Actual = std::decay_t<decltype(actual)>;
                                if constexpr (std::is_same_v<Actual, SineParams> ||
                                              std::is_same_v<Actual, SquareParams> ||
                                              std::is_same_v<Actual, TriangleParams>) {
                                    actual.phaseDeg = value;
                                }
                            },
                            segment.params
                        );
                    }
                );
                addNumberField(tr("Offset"), params.offset, [](Segment& segment, double value) {
                    std::visit(
                        [value](auto& actual) {
                            using Actual = std::decay_t<decltype(actual)>;
                            if constexpr (std::is_same_v<Actual, SineParams> ||
                                          std::is_same_v<Actual, SquareParams> ||
                                          std::is_same_v<Actual, TriangleParams>) {
                                actual.offset = value;
                            }
                        },
                        segment.params
                    );
                });
                if constexpr (std::is_same_v<Params, SquareParams>) {
                    addNumberField(tr("Duty"), params.duty, [](Segment& segment, double value) {
                        if (value > 0.0 && value < 1.0) {
                            std::get<SquareParams>(segment.params).duty = value;
                        }
                    });
                }
                if constexpr (std::is_same_v<Params, TriangleParams>) {
                    addNumberField(
                        tr("Symmetry"),
                        params.symmetry,
                        [](Segment& segment, double value) {
                            if (value >= 0.0 && value <= 1.0) {
                                std::get<TriangleParams>(segment.params).symmetry = value;
                            }
                        }
                    );
                }
            } else if constexpr (std::is_same_v<Params, PulseParams>) {
                addNumberField(
                    tr("Base value"),
                    params.baseValue,
                    [](Segment& segment, double value) {
                        std::get<PulseParams>(segment.params).baseValue = value;
                    }
                );
                addNumberField(
                    tr("Pulse value"),
                    params.pulseValue,
                    [](Segment& segment, double value) {
                        std::get<PulseParams>(segment.params).pulseValue = value;
                    }
                );
                addNumberField(tr("Delay (s)"), params.delay, [](Segment& segment, double value) {
                    if (value >= 0.0) {
                        std::get<PulseParams>(segment.params).delay = value;
                    }
                });
                addNumberField(tr("Width (s)"), params.width, [](Segment& segment, double value) {
                    if (value > 0.0) {
                        std::get<PulseParams>(segment.params).width = value;
                    }
                });
            } else if constexpr (std::is_same_v<Params, ExponentialParams>) {
                addNumberField(
                    tr("Start value"),
                    params.startValue,
                    [](Segment& segment, double value) {
                        std::get<ExponentialParams>(segment.params).startValue = value;
                    }
                );
                addNumberField(
                    tr("End value"),
                    params.endValue,
                    [](Segment& segment, double value) {
                        std::get<ExponentialParams>(segment.params).endValue = value;
                    }
                );
                addNumberField(
                    tr("Time constant (s)"),
                    params.timeConstant,
                    [](Segment& segment, double value) {
                        if (value > 0.0) {
                            std::get<ExponentialParams>(segment.params).timeConstant = value;
                        }
                    }
                );
            } else if constexpr (std::is_same_v<Params, FormulaParams>) {
                auto* expression = new FormulaEdit(params.expression, this);
                expression->setObjectName(QStringLiteral("formulaExpressionEdit"));
                expression->setToolTip(
                    tr("Use t, constants, operators, and functions. See Help > Formula Reference.")
                );
                const auto validate = [expression](const QString& value) {
                    Expression candidate;
                    const auto error = candidate.compile(value);
                    expression->setProperty("expressionValid", ! error.has_value());
                    if (error) {
                        expression->setStyleSheet(
                            QStringLiteral("QLineEdit { border: 1px solid #a02832; }")
                        );
                        expression->setStatusTip(error->message);
                    } else {
                        expression->setStyleSheet({});
                        expression->setStatusTip({});
                    }
                };
                validate(params.expression);
                connect(expression, &QLineEdit::textChanged, this, validate);
                connect(expression, &QLineEdit::editingFinished, this, [this, expression] {
                    Expression candidate;
                    const QString value = expression->text();
                    if (candidate.compile(value)) {
                        return;
                    }
                    const Segment* current = selectedSegment();
                    if (! current || ! std::holds_alternative<FormulaParams>(current->params)) {
                        return;
                    }
                    Segment edited = *current;
                    auto& formula  = std::get<FormulaParams>(edited.params);
                    if (formula.expression == value) {
                        return;
                    }
                    formula.expression        = value;
                    const LayerPath layerPath = layerPath_;
                    const size_t segmentIndex = *segmentIndex_;
                    QTimer::singleShot(0, this, [this, layerPath, segmentIndex, edited] {
                        emit segmentChanged(layerPath, segmentIndex, edited);
                    });
                });
                form_->addRow(tr("Expression"), expression);
                auto* globalTime = new QCheckBox(tr("Use global time"), this);
                globalTime->setChecked(params.globalTime);
                connect(globalTime, &QCheckBox::toggled, this, [this](bool enabled) {
                    const Segment* current = selectedSegment();
                    if (! current || ! std::holds_alternative<FormulaParams>(current->params)) {
                        return;
                    }
                    Segment edited                                    = *current;
                    std::get<FormulaParams>(edited.params).globalTime = enabled;
                    emitChanged(std::move(edited));
                });
                form_->addRow(globalTime);
            } else if constexpr (std::is_same_v<Params, PointsParams>) {
                auto* interpolation = new QComboBox(this);
                interpolation->addItems({tr("Linear"), tr("Step"), tr("PCHIP")});
                interpolation->setCurrentIndex(static_cast<int>(params.interp));
                connect(interpolation, &QComboBox::activated, this, [this, interpolation] {
                    const Segment* current = selectedSegment();
                    if (! current || ! std::holds_alternative<PointsParams>(current->params)) {
                        return;
                    }
                    Segment edited = *current;
                    std::get<PointsParams>(edited.params).interp =
                        static_cast<PointsParams::Interp>(interpolation->currentIndex());
                    emitChanged(std::move(edited));
                });
                form_->addRow(tr("Interpolation"), interpolation);
                addPointsTable(params);
            } else if constexpr (std::is_same_v<Params, ChirpParams>) {
                addNumberField(
                    tr("Amplitude"),
                    params.amplitude,
                    [](Segment& segment, double value) {
                        std::get<ChirpParams>(segment.params).amplitude = value;
                    }
                );
                addNumberField(
                    tr("Start frequency (Hz)"),
                    params.startFrequency,
                    [](Segment& segment, double value) {
                        auto& chirp = std::get<ChirpParams>(segment.params);
                        if (value >= 0.0 &&
                            (chirp.sweep == ChirpParams::Sweep::Linear || value > 0.0)) {
                            chirp.startFrequency = value;
                        }
                    }
                );
                addNumberField(
                    tr("End frequency (Hz)"),
                    params.endFrequency,
                    [](Segment& segment, double value) {
                        auto& chirp = std::get<ChirpParams>(segment.params);
                        if (value >= 0.0 &&
                            (chirp.sweep == ChirpParams::Sweep::Linear || value > 0.0)) {
                            chirp.endFrequency = value;
                        }
                    }
                );
                addNumberField(
                    tr("Phase (deg)"),
                    params.phaseDeg,
                    [](Segment& segment, double value) {
                        std::get<ChirpParams>(segment.params).phaseDeg = value;
                    }
                );
                addNumberField(tr("Offset"), params.offset, [](Segment& segment, double value) {
                    std::get<ChirpParams>(segment.params).offset = value;
                });
                auto* sweep = new QComboBox(this);
                sweep->setObjectName(QStringLiteral("chirpSweepCombo"));
                sweep->addItem(tr("Linear"), static_cast<int>(ChirpParams::Sweep::Linear));
                sweep->addItem(
                    tr("Exponential"), static_cast<int>(ChirpParams::Sweep::Exponential)
                );
                sweep->setCurrentIndex(sweep->findData(static_cast<int>(params.sweep)));
                connect(sweep, &QComboBox::activated, this, [this, sweep] {
                    const Segment* current = selectedSegment();
                    if (! current || ! std::holds_alternative<ChirpParams>(current->params)) {
                        return;
                    }
                    Segment edited = *current;
                    auto& chirp    = std::get<ChirpParams>(edited.params);
                    const auto requested =
                        static_cast<ChirpParams::Sweep>(sweep->currentData().toInt());
                    if (requested == ChirpParams::Sweep::Exponential &&
                        (chirp.startFrequency <= 0.0 || chirp.endFrequency <= 0.0)) {
                        rebuildForm();
                        return;
                    }
                    chirp.sweep = requested;
                    emitChanged(std::move(edited));
                });
                form_->addRow(tr("Sweep"), sweep);
            } else if constexpr (std::is_same_v<Params, RickerParams>) {
                addNumberField(
                    tr("Amplitude"),
                    params.amplitude,
                    [](Segment& segment, double value) {
                        std::get<RickerParams>(segment.params).amplitude = value;
                    }
                );
                addNumberField(
                    tr("Center frequency (Hz)"),
                    params.centerFrequency,
                    [](Segment& segment, double value) {
                        if (value > 0.0) {
                            std::get<RickerParams>(segment.params).centerFrequency = value;
                        }
                    }
                );
                addNumberField(tr("Offset"), params.offset, [](Segment& segment, double value) {
                    std::get<RickerParams>(segment.params).offset = value;
                });
            } else if constexpr (std::is_same_v<Params, WindowParams>) {
                auto* shape = new QComboBox(this);
                shape->setObjectName(QStringLiteral("windowShapeCombo"));
                shape->addItems(windowShapeNames());
                shape->setCurrentIndex(static_cast<int>(params.shape));
                connect(shape, &QComboBox::activated, this, [this, shape] {
                    const Segment* current = selectedSegment();
                    if (! current || ! std::holds_alternative<WindowParams>(current->params)) {
                        return;
                    }
                    Segment edited = *current;
                    std::get<WindowParams>(edited.params).shape =
                        static_cast<WindowParams::Shape>(shape->currentIndex());
                    emitChanged(std::move(edited));
                });
                form_->addRow(tr("Shape"), shape);
                addNumberField(
                    tr("Amplitude"),
                    params.amplitude,
                    [](Segment& segment, double value) {
                        std::get<WindowParams>(segment.params).amplitude = value;
                    }
                );
                addNumberField(tr("Offset"), params.offset, [](Segment& segment, double value) {
                    std::get<WindowParams>(segment.params).offset = value;
                });
                // Only the parameter of the selected shape is offered: the other
                // three have no effect on it, and a field that does nothing is
                // worse than no field.
                switch (params.shape) {
                    case WindowParams::Shape::Tukey:
                        addNumberField(
                            tr("Taper (0..1)"),
                            params.alpha,
                            [](Segment& segment, double value) {
                                if (value >= 0.0 && value <= 1.0) {
                                    std::get<WindowParams>(segment.params).alpha = value;
                                }
                            }
                        );
                        break;
                    case WindowParams::Shape::Gauss:
                        addNumberField(
                            tr("Sigma (of half width)"),
                            params.sigma,
                            [](Segment& segment, double value) {
                                if (value > 0.0) {
                                    std::get<WindowParams>(segment.params).sigma = value;
                                }
                            }
                        );
                        break;
                    case WindowParams::Shape::Kaiser:
                        addNumberField(
                            tr("Beta"),
                            params.beta,
                            [](Segment& segment, double value) {
                                if (value >= 0.0) {
                                    std::get<WindowParams>(segment.params).beta = value;
                                }
                            }
                        );
                        break;
                    case WindowParams::Shape::Trapezoid:
                        addNumberField(
                            tr("Rise (s)"),
                            params.rise,
                            [](Segment& segment, double value) {
                                if (value >= 0.0) {
                                    std::get<WindowParams>(segment.params).rise = value;
                                }
                            }
                        );
                        addNumberField(
                            tr("Fall (s)"),
                            params.fall,
                            [](Segment& segment, double value) {
                                if (value >= 0.0) {
                                    std::get<WindowParams>(segment.params).fall = value;
                                }
                            }
                        );
                        break;
                    case WindowParams::Shape::Rectangular:
                    case WindowParams::Shape::Hann:
                    case WindowParams::Shape::Hamming:
                    case WindowParams::Shape::Blackman:
                    case WindowParams::Shape::BlackmanHarris:
                        break;
                }
            } else if constexpr (std::is_same_v<Params, NpvParams> ||
                                 std::is_same_v<Params, SwvParams> ||
                                 std::is_same_v<Params, DpvParams>) {
                // Start and end mean the first and the last pulse for NPV and
                // the ends of the staircase for the other two, so only NPV has
                // something to explain here.
                QString startTooltip;
                QString endTooltip;
                if constexpr (std::is_same_v<Params, NpvParams>) {
                    // The baseline is a value of its own, which is what lets a
                    // pulse train run towards it as well as away from it. It
                    // leaves the sweep's length alone, so unlike its neighbours
                    // it is not a field that refits the duration.
                    addNumberField(
                        tr("Base value"),
                        params.baseValue,
                        [](Segment& segment, double value) {
                            std::get<NpvParams>(segment.params).baseValue = value;
                        },
                        tr("The level the waveform holds between the pulses")
                    );
                    startTooltip =
                        tr("The first pulse of the sweep, the one every following pulse steps on "
                           "from");
                    endTooltip = tr("The last pulse of the sweep");
                }
                addSweepNumberField(
                    tr("Start value"),
                    params.startValue,
                    [](Segment& segment, double value) {
                        editSweep(segment, [value](auto& sweep) { sweep.startValue = value; });
                    },
                    startTooltip
                );
                addSweepNumberField(
                    tr("End value"),
                    params.endValue,
                    [](Segment& segment, double value) {
                        editSweep(segment, [value](auto& sweep) { sweep.endValue = value; });
                    },
                    endTooltip
                );
                // The scan direction comes from start and end, so the step is a
                // magnitude; a sweep down is written with an end below the start.
                addSweepNumberField(
                    tr("Step value"),
                    params.stepValue,
                    [](Segment& segment, double value) {
                        if (value > 0.0) {
                            editSweep(segment, [value](auto& sweep) { sweep.stepValue = value; });
                        }
                    }
                );
                if constexpr (std::is_same_v<Params, DpvParams>) {
                    addSweepNumberField(
                        tr("Pulse value"),
                        params.pulseValue,
                        [](Segment& segment, double value) {
                            if (value > 0.0) {
                                std::get<DpvParams>(segment.params).pulseValue = value;
                            }
                        }
                    );
                }
                if constexpr (std::is_same_v<Params, SwvParams>) {
                    addSweepNumberField(
                        tr("Amplitude"),
                        params.amplitude,
                        [](Segment& segment, double value) {
                            if (value > 0.0) {
                                std::get<SwvParams>(segment.params).amplitude = value;
                            }
                        }
                    );
                    addSweepNumberField(
                        tr("Period (s)"),
                        params.period,
                        [](Segment& segment, double value) {
                            if (value > 0.0) {
                                std::get<SwvParams>(segment.params).period = value;
                            }
                        }
                    );
                } else {
                    // The two times are kept in the order the waveform needs:
                    // a pulse may fill its step but never outlast it, so each
                    // field pulls the other along rather than being rejected.
                    addSweepNumberField(
                        tr("Step time (s)"),
                        params.stepTime,
                        [](Segment& segment, double value) {
                            if (value > 0.0) {
                                editPulseSweep(segment, [value](auto& sweep) {
                                    sweep.stepTime  = value;
                                    sweep.pulseTime = std::min(sweep.pulseTime, value);
                                });
                            }
                        }
                    );
                    addSweepNumberField(
                        tr("Pulse time (s)"),
                        params.pulseTime,
                        [](Segment& segment, double value) {
                            if (value > 0.0) {
                                editPulseSweep(segment, [value](auto& sweep) {
                                    sweep.pulseTime = std::min(value, sweep.stepTime);
                                });
                            }
                        }
                    );
                }
                if constexpr (std::is_same_v<Params, DpvParams>) {
                    auto* invert = new QCheckBox(tr("Invert pulse"), this);
                    invert->setObjectName(QStringLiteral("dpvInvertPulseCheck"));
                    invert->setChecked(params.invertPulse);
                    invert->setToolTip(
                        tr("Send the pulse against the scan direction instead of with it")
                    );
                    connect(invert, &QCheckBox::toggled, this, [this](bool enabled) {
                        const Segment* current = selectedSegment();
                        if (! current || ! std::holds_alternative<DpvParams>(current->params)) {
                            return;
                        }
                        Segment edited                                  = *current;
                        std::get<DpvParams>(edited.params).invertPulse = enabled;
                        emitChanged(std::move(edited));
                    });
                    form_->addRow(invert);
                }
                // What the parameters add up to. The duration above follows this
                // number, so seeing it explains why editing a value moves it.
                auto* steps = new QLabel(
                    QString::number(static_cast<qulonglong>(sweepStepCount(segment))), this
                );
                steps->setObjectName(QStringLiteral("sweepStepCountLabel"));
                if constexpr (std::is_same_v<Params, NpvParams>) {
                    // The pulses are the steps plus one, because start and end
                    // are both pulses, and it is the pulses the duration counts.
                    steps->setToolTip(
                        tr("|End value - start value| divided by the step value, rounded down. "
                           "One pulse more than that is sent - the first and the last one are "
                           "both counted - and the duration above is kept at that many steps.")
                    );
                } else {
                    steps->setToolTip(
                        tr("|End value - start value| divided by the step value, rounded down. The "
                           "duration above is kept at this many steps.")
                    );
                }
                form_->addRow(tr("Steps"), steps);
            }
        },
        segment.params
    );
}

void PropertyPanel::addLayerForm(const WaveLayer& layer) {
    auto* type = new QLabel(isGroup(layer) ? tr("Group") : tr("Layer"), this);
    type->setObjectName(QStringLiteral("layerTypeLabel"));
    form_->addRow(tr("Type"), type);

    const auto editLayer = [this](const std::function<void(WaveLayer&)>& change) {
        const WaveLayer* current = selectedLayer();
        if (! current) {
            return;
        }
        WaveLayer edited = *current;
        change(edited);
        if (! (edited == *current)) {
            emit layerChanged(layerPath_, edited);
        }
    };

    auto* name = new QLineEdit(layer.name, this);
    name->setObjectName(QStringLiteral("layerNameEdit"));
    // Committing per keystroke would push an undo step per letter; the tree
    // follows as soon as the field is left.
    connect(name, &QLineEdit::editingFinished, this, [this, name, editLayer] {
        const QString value = name->text();
        editLayer([&value](WaveLayer& edited) { edited.name = value; });
    });
    form_->addRow(tr("Name"), name);

    auto* enabled = new QCheckBox(tr("Active"), this);
    enabled->setObjectName(QStringLiteral("layerEnabledCheck"));
    enabled->setChecked(layer.enabled);
    enabled->setToolTip(tr("The same switch as the check box in the structure tree"));
    connect(enabled, &QCheckBox::toggled, this, [editLayer](bool checked) {
        editLayer([checked](WaveLayer& edited) { edited.enabled = checked; });
    });
    form_->addRow(enabled);

    auto* mode = new QComboBox(this);
    mode->setObjectName(QStringLiteral("layerModeCombo"));
    mode->addItem(tr("Add"), static_cast<int>(LayerMode::Add));
    mode->addItem(tr("Multiply"), static_cast<int>(LayerMode::Multiply));
    mode->setCurrentIndex(mode->findData(static_cast<int>(layer.mode)));
    mode->setToolTip(
        tr("How this layer is combined with the layers above it on the same level")
    );
    connect(mode, &QComboBox::activated, this, [mode, editLayer] {
        const auto requested = static_cast<LayerMode>(mode->currentData().toInt());
        editLayer([requested](WaveLayer& edited) { edited.mode = requested; });
    });
    form_->addRow(tr("Mode"), mode);

    auto* edge = new QComboBox(this);
    edge->setObjectName(QStringLiteral("layerEdgeCombo"));
    edge->addItem(tr("Neutral"), static_cast<int>(LayerEdge::Neutral));
    edge->addItem(tr("Zero"), static_cast<int>(LayerEdge::Zero));
    edge->addItem(tr("Hold last"), static_cast<int>(LayerEdge::HoldLast));
    edge->addItem(tr("Loop"), static_cast<int>(LayerEdge::Loop));
    edge->setCurrentIndex(edge->findData(static_cast<int>(layer.edge)));
    edge->setToolTip(
        tr("What this layer contributes outside its own duration. Neutral is 0 when adding "
           "and 1 when multiplying, so a short window leaves the rest of the signal alone.")
    );
    connect(edge, &QComboBox::activated, this, [edge, editLayer] {
        const auto requested = static_cast<LayerEdge>(edge->currentData().toInt());
        editLayer([requested](WaveLayer& edited) { edited.edge = requested; });
    });
    form_->addRow(tr("Outside"), edge);

    auto* duration = new QLabel(engineering::format(layerDuration(layer)), this);
    duration->setObjectName(QStringLiteral("layerDurationLabel"));
    form_->addRow(tr("Duration (s)"), duration);

    // The first enabled node of a level starts the combination, so nothing is
    // there yet for it to add to or multiply with.
    const std::vector<WaveLayer>* siblings = layerSiblings(document_.layers, layerPath_);
    if (siblings && ! layerPath_.empty()) {
        const auto first = std::find_if(
            siblings->begin(),
            siblings->end(),
            [](const WaveLayer& sibling) { return sibling.enabled; }
        );
        if (first != siblings->end() &&
            static_cast<size_t>(std::distance(siblings->begin(), first)) == layerPath_.back()) {
            auto* hint = new QLabel(
                tr("First active layer of this level - it starts the combination, so its "
                   "mode has no effect."),
                this
            );
            hint->setObjectName(QStringLiteral("layerModeHint"));
            hint->setWordWrap(true);
            hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            form_->addRow(hint);
        }
    }
}

void PropertyPanel::addPointsTable(const PointsParams& params) {
    // An imported waveform can hold tens of thousands of points. Listing them all
    // would neither be usable nor fast, and typing exact values one row at a time
    // is not how such a segment is edited anyway.
    if (params.points.size() > MaximumListedPoints) {
        auto* hint = new QLabel(
            tr("%1 points - too many to list. Edit them on the canvas, or import "
               "the values from CSV.")
                .arg(params.points.size()),
            this
        );
        hint->setWordWrap(true);
        hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        form_->addRow(hint);
        return;
    }

    pointsTable_ = new QTableWidget(static_cast<int>(params.points.size()), 2, this);
    pointsTable_->setObjectName(QStringLiteral("pointsTable"));
    pointsTable_->setHorizontalHeaderLabels({tr("Time (s)"), tr("Value")});
    pointsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    pointsTable_->verticalHeader()->setDefaultSectionSize(pointsTable_->fontMetrics().height() + 8);
    pointsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    pointsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    pointsTable_->setEditTriggers(
        QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed |
        QAbstractItemView::AnyKeyPressed
    );
    // Enough rows to see the shape of a hand-built segment; longer lists scroll
    // rather than pushing the rest of the panel out of view.
    pointsTable_->setMinimumHeight(150);
    pointsTable_->setMaximumHeight(260);
    refreshPointsTable();

    connect(pointsTable_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        const auto parsed = engineering::parse(item->text());
        if (! parsed) {
            // Restoring the stored value rejects the input without a dialog; the
            // segment keeps the number it had.
            refreshPointsTable();
            return;
        }
        editPoint(item->row(), item->column(), *parsed);
    });
    connect(pointsTable_, &QTableWidget::currentCellChanged, this, [this](int row) {
        selectedPoint_ = row >= 0 ? std::optional<size_t>(static_cast<size_t>(row)) : std::nullopt;
        emit pointSelected(row);
    });
    form_->addRow(pointsTable_);

    auto* buttons   = new QWidget(this);
    auto* buttonRow = new QHBoxLayout(buttons);
    buttonRow->setContentsMargins(0, 0, 0, 0);
    auto* insert = new QPushButton(tr("Insert"), buttons);
    insert->setObjectName(QStringLiteral("insertPointButton"));
    insert->setToolTip(tr("Insert a point after the selected one, on the current curve"));
    connect(insert, &QPushButton::clicked, this, &PropertyPanel::insertPointAfterSelection);
    buttonRow->addWidget(insert);
    auto* remove = new QPushButton(tr("Remove"), buttons);
    remove->setObjectName(QStringLiteral("removePointButton"));
    remove->setToolTip(tr("Remove the selected point"));
    remove->setEnabled(params.points.size() > 2);
    connect(remove, &QPushButton::clicked, this, &PropertyPanel::removeSelectedPoint);
    buttonRow->addWidget(remove);
    buttonRow->addStretch(1);
    form_->addRow(buttons);
}

void PropertyPanel::refreshPointsTable() {
    const PointsParams* params = selectedPoints();
    if (! pointsTable_ || ! params ||
        pointsTable_->rowCount() != static_cast<int>(params->points.size())) {
        return;
    }
    // Setting an item's text emits itemChanged, which is the handler that writes
    // back to the document. Only the cells that really differ are touched, so a
    // drag stays cheap even with a few hundred rows.
    const QSignalBlocker blocker(pointsTable_);
    for (int row = 0; row < pointsTable_->rowCount(); ++row) {
        const auto& point                      = params->points[static_cast<size_t>(row)];
        const std::array<QString, 2> formatted = {
            engineering::format(point.first), engineering::format(point.second)
        };
        for (int column = 0; column < 2; ++column) {
            QTableWidgetItem* item = pointsTable_->item(row, column);
            if (! item) {
                item = new QTableWidgetItem;
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                pointsTable_->setItem(row, column, item);
            }
            if (item->text() != formatted[static_cast<size_t>(column)]) {
                item->setText(formatted[static_cast<size_t>(column)]);
            }
        }
    }
    // Still inside the signal blocker: following the canvas must not echo the
    // selection straight back to it.
    if (selectedPoint_ && *selectedPoint_ < params->points.size() &&
        pointsTable_->currentRow() != static_cast<int>(*selectedPoint_)) {
        pointsTable_->selectRow(static_cast<int>(*selectedPoint_));
    }
}

void PropertyPanel::editPoint(int row, int column, double value) {
    const PointsParams* params = selectedPoints();
    if (! params || row < 0 || static_cast<size_t>(row) >= params->points.size()) {
        return;
    }
    Segment edited = *selectedSegment();
    auto& points   = std::get<PointsParams>(edited.params).points;
    auto& point    = points[static_cast<size_t>(row)];
    if (column == 0) {
        // Points are kept sorted by time and the editors rely on that, so a typed
        // time stays between its neighbours instead of reordering the segment.
        const double spacing = std::max(1e-9, edited.duration * 1e-9);
        const double lower = row == 0 ? 0.0 : points[static_cast<size_t>(row) - 1].first + spacing;
        const double upper = static_cast<size_t>(row) + 1 == points.size()
                                 ? edited.duration
                                 : points[static_cast<size_t>(row) + 1].first - spacing;
        point.first        = std::clamp(value, lower, std::max(lower, upper));
    } else {
        point.second = value;
    }
    emitChanged(std::move(edited));
}

void PropertyPanel::insertPointAfterSelection() {
    const PointsParams* params = selectedPoints();
    if (! params || params->points.size() < 2) {
        return;
    }
    Segment edited = *selectedSegment();
    auto& points   = std::get<PointsParams>(edited.params).points;
    // Into the gap after the selected point, or into the last one when the
    // selection is the final point and has no gap of its own.
    const size_t after =
        selectedPoint_ && *selectedPoint_ + 1 < points.size() ? *selectedPoint_ : points.size() - 2;
    // Halfway to the next point, taking the value off the current curve: an
    // inserted point never changes the shape, it only gives you a handle.
    const double time  = (points[after].first + points[after + 1].first) / 2.0;
    const double value = sampling::segmentValueAt(edited, time, time);
    points.insert(points.begin() + static_cast<std::ptrdiff_t>(after) + 1, {time, value});
    selectedPoint_ = after + 1;
    emitChanged(std::move(edited));
    emit pointSelected(static_cast<int>(after + 1));
}

void PropertyPanel::removeSelectedPoint() {
    const PointsParams* params = selectedPoints();
    // Two points are the minimum a segment can be described with; the canvas
    // applies the same rule to the Delete key.
    if (! params || ! selectedPoint_ || params->points.size() <= 2 ||
        *selectedPoint_ >= params->points.size()) {
        return;
    }
    Segment edited = *selectedSegment();
    auto& points   = std::get<PointsParams>(edited.params).points;
    points.erase(points.begin() + static_cast<std::ptrdiff_t>(*selectedPoint_));
    selectedPoint_.reset();
    emitChanged(std::move(edited));
    emit pointSelected(-1);
}

const PointsParams* PropertyPanel::selectedPoints() const {
    const Segment* segment = selectedSegment();
    return segment ? std::get_if<PointsParams>(&segment->params) : nullptr;
}

void PropertyPanel::setSelectedPoint(int pointIndex) {
    const auto index =
        pointIndex >= 0 ? std::optional<size_t>(static_cast<size_t>(pointIndex)) : std::nullopt;
    if (selectedPoint_ == index) {
        return;
    }
    selectedPoint_ = index;
    // The canvas can report a point of a segment whose table has not been built
    // yet; the next refresh applies it.
    refreshPointsTable();
}

bool PropertyPanel::onlyPointsMoved(const WaveDocument& document) const {
    const Segment* previous = selectedSegment();
    if (! previous || document_.sampleRate != document.sampleRate) {
        return false;
    }
    const WaveLayer* layer = layerAtPath(document.layers, layerPath_);
    if (! layer || isGroup(*layer) || *segmentIndex_ >= layer->segments.size()) {
        return false;
    }
    const Segment& current     = layer->segments[*segmentIndex_];
    const auto* previousPoints = std::get_if<PointsParams>(&previous->params);
    const auto* currentPoints  = std::get_if<PointsParams>(&current.params);
    if (! previousPoints || ! currentPoints || previous->duration != current.duration ||
        previous->repeat != current.repeat || previousPoints->interp != currentPoints->interp ||
        previousPoints->points.size() != currentPoints->points.size()) {
        return false;
    }
    // Everything but those coordinates has to be identical, which is cheapest
    // to establish by putting the old segment back and comparing the documents.
    WaveDocument candidate                                      = document;
    layerAtPath(candidate.layers, layerPath_)->segments[*segmentIndex_] = *previous;
    return candidate == document_;
}

void PropertyPanel::addNumberField(
    const QString& label,
    double value,
    const std::function<void(Segment&, double)>& setter,
    const QString& tooltip
) {
    auto* edit = new EngineeringEdit(this);
    edit->setValue(value);
    connect(edit, &EngineeringEdit::valueCommitted, this, [this, setter](double editedValue) {
        const Segment* current = selectedSegment();
        if (! current) {
            return;
        }
        Segment edited = *current;
        setter(edited, editedValue);
        emitChanged(std::move(edited));
    });
    form_->addRow(label, edit);
    if (! tooltip.isEmpty()) {
        edit->setToolTip(tooltip);
        if (QWidget* labelWidget = form_->labelForField(edit)) {
            labelWidget->setToolTip(tooltip);
        }
    }
}

void PropertyPanel::addSweepNumberField(
    const QString& label,
    double value,
    const std::function<void(Segment&, double)>& setter,
    const QString& tooltip
) {
    addNumberField(
        label,
        value,
        [setter](Segment& segment, double editedValue) {
            setter(segment, editedValue);
            if (const double sweep = sweepDuration(segment); sweep > 0.0) {
                segment.duration = sweep;
            }
        },
        tooltip
    );
}

void PropertyPanel::emitChanged(Segment segment) {
    if (selectedSegment()) {
        emit segmentChanged(layerPath_, *segmentIndex_, segment);
    }
}

}  // namespace zwe
