// SPDX-FileCopyrightText: 2026 Zahner-Elektrik GmbH & Co. KG
// SPDX-License-Identifier: GPL-3.0-only

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QtTest>

#include "ui/engineeringedit.h"
#include "ui/propertypanel.h"

using namespace zwe;

namespace {

WaveDocument formulaDocument() {
    WaveDocument document;
    WaveLayer layer;
    layer.segments.push_back(defaultSegment(SegmentType::Formula));
    document.layers.push_back(std::move(layer));
    return document;
}

WaveDocument pointsDocument() {
    WaveDocument document;
    Segment segment;
    segment.duration = 1.0;
    segment.params   = PointsParams{
        .interp = PointsParams::Interp::Linear, .points = {{0.0, 0.0}, {0.5, 1.0}, {1.0, 0.0}}
    };
    document.layers.push_back(WaveLayer{.name = QStringLiteral("Layer"), .segments = {segment}});
    return document;
}

WaveDocument singleSegmentDocument(SegmentType type) {
    WaveDocument document;
    WaveLayer layer;
    layer.segments.push_back(defaultSegment(type));
    document.layers.push_back(std::move(layer));
    return document;
}

// The parameter fields carry no object name of their own, so they are addressed
// the way a user picks them out: by the label the form puts in front of them.
EngineeringEdit* fieldLabelled(QWidget& panel, const QString& label) {
    auto* form = panel.findChild<QFormLayout*>();
    if (! form) {
        return nullptr;
    }
    for (int row = 0; row < form->rowCount(); ++row) {
        QLayoutItem* labelItem = form->itemAt(row, QFormLayout::LabelRole);
        auto* text             = labelItem ? qobject_cast<QLabel*>(labelItem->widget()) : nullptr;
        if (! text || text->text() != label) {
            continue;
        }
        QLayoutItem* fieldItem = form->itemAt(row, QFormLayout::FieldRole);
        return fieldItem ? qobject_cast<EngineeringEdit*>(fieldItem->widget()) : nullptr;
    }
    return nullptr;
}

// Types a value into a field and commits it, which is what leaving the field
// does for a user.
void commitValue(EngineeringEdit* field, const QString& text) {
    field->selectAll();
    QTest::keyClicks(field, text);
    QTest::keyClick(field, Qt::Key_Return);
}

}  // namespace

class TestPropertyPanel : public QObject {
    Q_OBJECT

private slots:
    void formulaTypingRetainsEditorAndCommitsOnce();
    void segmentTypeIsReadOnly();
    void pointsTableEditsTheSelectedPoint();
    void pointsTableFollowsTheCanvasSelectionWithoutEchoing();
    void movingAPointKeepsTheTableInstance();
    void layerFormEditsNameModeAndEdgePolicy();
    void windowSegmentOffersOnlyTheShapesOwnParameter();
    void sweepFieldsFitTheDurationAndKeepTheTimesInStep();
};

void TestPropertyPanel::formulaTypingRetainsEditorAndCommitsOnce() {
    PropertyPanel panel;
    panel.setDocument(formulaDocument());
    panel.setSelection({0}, 0);
    panel.show();

    auto* editor = panel.findChild<QLineEdit*>(QStringLiteral("formulaExpressionEdit"));
    QVERIFY(editor);
    QVERIFY(! editor->toolTip().isEmpty());
    panel.activateWindow();
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    QSignalSpy changed(&panel, &PropertyPanel::segmentChanged);

    editor->selectAll();
    QTest::keyClicks(editor, QStringLiteral("sin("));
    QCOMPARE(panel.findChild<QLineEdit*>(QStringLiteral("formulaExpressionEdit")), editor);
    QVERIFY(editor->hasFocus());
    QCOMPARE(changed.count(), 0);
    QCOMPARE(editor->property("expressionValid").toBool(), false);

    QTest::keyClicks(editor, QStringLiteral("t)"));
    QCOMPARE(panel.findChild<QLineEdit*>(QStringLiteral("formulaExpressionEdit")), editor);
    QVERIFY(editor->hasFocus());
    QCOMPARE(changed.count(), 0);
    QCOMPARE(editor->property("expressionValid").toBool(), true);

    QTest::keyClick(editor, Qt::Key_Return);
    QTRY_COMPARE(changed.count(), 1);
    const QList<QVariant> arguments = changed.takeFirst();
    const Segment segment           = qvariant_cast<Segment>(arguments.at(2));
    QCOMPARE(std::get<FormulaParams>(segment.params).expression, QStringLiteral("sin(t)"));

    editor->selectAll();
    QKeyEvent circumflexPress(QEvent::KeyPress, Qt::Key_Dead_Circumflex, Qt::NoModifier);
    QCoreApplication::sendEvent(editor, &circumflexPress);
    QTest::keyClick(editor, Qt::Key_2);
    QCOMPARE(editor->text(), QStringLiteral("^2"));
}

void TestPropertyPanel::segmentTypeIsReadOnly() {
    PropertyPanel panel;
    panel.setDocument(formulaDocument());
    panel.setSelection({0}, 0);

    const auto* type = panel.findChild<QLabel*>(QStringLiteral("segmentTypeLabel"));
    QVERIFY(type);
    QCOMPARE(type->text(), QStringLiteral("Formula"));
    QVERIFY(! panel.findChild<QComboBox*>());
}

void TestPropertyPanel::pointsTableEditsTheSelectedPoint() {
    PropertyPanel panel;
    panel.setDocument(pointsDocument());
    panel.setSelection({0}, 0);
    panel.show();

    auto* table = panel.findChild<QTableWidget*>(QStringLiteral("pointsTable"));
    QVERIFY(table);
    QCOMPARE(table->rowCount(), 3);
    QCOMPARE(table->item(1, 0)->text(), QStringLiteral("500m"));
    QCOMPARE(table->item(1, 1)->text(), QStringLiteral("1"));

    QSignalSpy changed(&panel, &PropertyPanel::segmentChanged);
    table->item(1, 1)->setText(QStringLiteral("2.5"));
    QCOMPARE(changed.count(), 1);
    Segment segment = qvariant_cast<Segment>(changed.takeFirst().at(2));
    QCOMPARE(std::get<PointsParams>(segment.params).points[1].second, 2.5);

    // A typed time is kept between its neighbours: the points of a segment are
    // sorted, and the whole editor relies on that.
    table->item(1, 0)->setText(QStringLiteral("4"));
    QCOMPARE(changed.count(), 1);
    segment = qvariant_cast<Segment>(changed.takeFirst().at(2));
    QVERIFY(std::get<PointsParams>(segment.params).points[1].first <= 1.0);

    // Unparsable input changes nothing and puts the stored value back.
    table->item(1, 0)->setText(QStringLiteral("not a number"));
    QCOMPARE(changed.count(), 0);
    QCOMPARE(table->item(1, 0)->text(), QStringLiteral("500m"));
}

void TestPropertyPanel::pointsTableFollowsTheCanvasSelectionWithoutEchoing() {
    PropertyPanel panel;
    panel.setDocument(pointsDocument());
    panel.setSelection({0}, 0);
    panel.show();

    auto* table = panel.findChild<QTableWidget*>(QStringLiteral("pointsTable"));
    QVERIFY(table);
    QSignalSpy selected(&panel, &PropertyPanel::pointSelected);

    // Coming from the canvas: the row follows, but the panel must not send the
    // selection straight back.
    panel.setSelectedPoint(2);
    QCOMPARE(table->currentRow(), 2);
    QCOMPARE(selected.count(), 0);

    // Coming from the table: the canvas is told which point to act on.
    table->setCurrentCell(1, 0);
    QCOMPARE(selected.count(), 1);
    QCOMPARE(selected.takeFirst().at(0).toInt(), 1);

    // Insert takes the value off the current curve, so the shape is unchanged.
    auto* insert = panel.findChild<QPushButton*>(QStringLiteral("insertPointButton"));
    QVERIFY(insert);
    QSignalSpy changed(&panel, &PropertyPanel::segmentChanged);
    insert->click();
    QCOMPARE(changed.count(), 1);
    const Segment segment = qvariant_cast<Segment>(changed.takeFirst().at(2));
    const auto& points    = std::get<PointsParams>(segment.params).points;
    QCOMPARE(points.size(), size_t{4});
    QCOMPARE(points[2].first, 0.75);
    QCOMPARE(points[2].second, 0.5);
}

void TestPropertyPanel::movingAPointKeepsTheTableInstance() {
    PropertyPanel panel;
    WaveDocument document = pointsDocument();
    panel.setDocument(document);
    panel.setSelection({0}, 0);
    panel.show();

    auto* table = panel.findChild<QTableWidget*>(QStringLiteral("pointsTable"));
    QVERIFY(table);

    // A drag sends a document per mouse move. Rebuilding the form every time
    // would tear the table down, which is both slow and loses the selection.
    std::get<PointsParams>(document.layers[0].segments[0].params).points[1] = {0.4, 0.25};
    panel.setDocument(document);
    QCOMPARE(panel.findChild<QTableWidget*>(QStringLiteral("pointsTable")), table);
    QCOMPARE(table->item(1, 0)->text(), QStringLiteral("400m"));
    QCOMPARE(table->item(1, 1)->text(), QStringLiteral("250m"));

    // A different number of points is a structural change and does rebuild.
    std::get<PointsParams>(document.layers[0].segments[0].params).points.push_back({1.5, 0.0});
    document.layers[0].segments[0].duration = 2.0;
    panel.setDocument(document);
    auto* rebuilt = panel.findChild<QTableWidget*>(QStringLiteral("pointsTable"));
    QVERIFY(rebuilt);
    QCOMPARE(rebuilt->rowCount(), 4);
}

void TestPropertyPanel::layerFormEditsNameModeAndEdgePolicy() {
    PropertyPanel panel;
    WaveDocument document;
    WaveLayer first;
    first.name = QStringLiteral("Carrier");
    first.segments.push_back(defaultSegment(SegmentType::Dc));
    WaveLayer second;
    second.name = QStringLiteral("Envelope");
    second.segments.push_back(defaultSegment(SegmentType::Window));
    WaveLayer group;
    group.name     = QStringLiteral("Group");
    group.kind     = LayerKind::Group;
    group.children = {second};
    document.layers = {first, group};
    panel.setDocument(document);
    panel.show();

    // Selecting a layer - no segment - shows its own properties instead of the
    // "select something" hint.
    panel.setSelection({0}, std::nullopt);
    auto* type = panel.findChild<QLabel*>(QStringLiteral("layerTypeLabel"));
    auto* name = panel.findChild<QLineEdit*>(QStringLiteral("layerNameEdit"));
    auto* mode = panel.findChild<QComboBox*>(QStringLiteral("layerModeCombo"));
    auto* edge = panel.findChild<QComboBox*>(QStringLiteral("layerEdgeCombo"));
    QVERIFY(type);
    QVERIFY(name);
    QVERIFY(mode);
    QVERIFY(edge);
    QCOMPARE(type->text(), QStringLiteral("Layer"));
    QCOMPARE(name->text(), QStringLiteral("Carrier"));
    QCOMPARE(mode->currentData().toInt(), static_cast<int>(LayerMode::Add));
    QCOMPARE(edge->currentData().toInt(), static_cast<int>(LayerEdge::Neutral));
    // It is the first enabled node of its level, so its mode does not matter.
    QVERIFY(panel.findChild<QLabel*>(QStringLiteral("layerModeHint")));

    // Driven through real key events rather than by emitting the signals: what
    // is worth testing here is that the widgets are reachable and wired, not
    // that a hand-sent signal arrives.
    QSignalSpy changed(&panel, &PropertyPanel::layerChanged);
    mode->setFocus();
    QTest::keyClick(mode, Qt::Key_Down);  // Add -> Multiply
    QCOMPARE(changed.count(), 1);
    QCOMPARE(qvariant_cast<LayerPath>(changed.first().at(0)), LayerPath{0});
    QCOMPARE(qvariant_cast<WaveLayer>(changed.takeFirst().at(1)).mode, LayerMode::Multiply);

    edge->setFocus();
    QTest::keyClick(edge, Qt::Key_End);  // Neutral -> Loop, the last entry
    QCOMPARE(changed.count(), 1);
    QCOMPARE(qvariant_cast<WaveLayer>(changed.takeFirst().at(1)).edge, LayerEdge::Loop);

    name->setFocus();
    name->selectAll();
    QTest::keyClicks(name, QStringLiteral("Renamed"));
    QCOMPARE(changed.count(), 0);  // not per keystroke
    QTest::keyClick(name, Qt::Key_Return);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(qvariant_cast<WaveLayer>(changed.takeFirst().at(1)).name, QStringLiteral("Renamed"));

    // With the edit applied, the way MainWindow does it, committing the same
    // text again changes nothing and must not cost another undo step.
    document.layers[0].name = QStringLiteral("Renamed");
    panel.setDocument(document);
    auto* renamed = panel.findChild<QLineEdit*>(QStringLiteral("layerNameEdit"));
    QVERIFY(renamed);
    renamed->setFocus();
    QTest::keyClick(renamed, Qt::Key_Return);
    QCOMPARE(changed.count(), 0);

    // A group says so, and a nested layer is reached by its path.
    panel.setSelection({1}, std::nullopt);
    QCOMPARE(
        panel.findChild<QLabel*>(QStringLiteral("layerTypeLabel"))->text(), QStringLiteral("Group")
    );
    panel.setSelection({1, 0}, std::nullopt);
    QCOMPARE(
        panel.findChild<QLineEdit*>(QStringLiteral("layerNameEdit"))->text(),
        QStringLiteral("Envelope")
    );

    // A path that addresses nothing falls back to the hint.
    panel.setSelection({5}, std::nullopt);
    QVERIFY(! panel.findChild<QLineEdit*>(QStringLiteral("layerNameEdit")));
}

void TestPropertyPanel::windowSegmentOffersOnlyTheShapesOwnParameter() {
    PropertyPanel panel;
    WaveDocument document;
    WaveLayer layer;
    layer.segments.push_back(defaultSegment(SegmentType::Window));
    document.layers.push_back(std::move(layer));
    panel.setDocument(document);
    panel.setSelection({0}, 0);
    panel.show();

    auto* shape = panel.findChild<QComboBox*>(QStringLiteral("windowShapeCombo"));
    QVERIFY(shape);
    QCOMPARE(shape->count(), 9);
    QCOMPARE(shape->currentIndex(), static_cast<int>(WindowParams::Shape::Hann));
    // Duration, amplitude and offset; a Hann has no shape parameter of its own.
    const int plainFields = panel.findChildren<EngineeringEdit*>().size();
    QCOMPARE(plainFields, 3);

    QSignalSpy changed(&panel, &PropertyPanel::segmentChanged);
    shape->setFocus();
    // Rectangular, Hann, Hamming, Blackman, Blackman-Harris, Tukey: four steps
    // down from Hann.
    for (int step = 0; step < 4; ++step) {
        QTest::keyClick(shape, Qt::Key_Down);
    }
    QCOMPARE(shape->currentIndex(), static_cast<int>(WindowParams::Shape::Tukey));
    QCOMPARE(changed.count(), 4);
    while (changed.count() > 1) {
        changed.removeFirst();
    }
    const Segment edited = qvariant_cast<Segment>(changed.takeFirst().at(2));
    QCOMPARE(std::get<WindowParams>(edited.params).shape, WindowParams::Shape::Tukey);

    // With the edited segment applied, the taper field appears - and only it.
    document.layers[0].segments[0] = edited;
    panel.setDocument(document);
    QCOMPARE(panel.findChildren<EngineeringEdit*>().size(), plainFields + 1);

    std::get<WindowParams>(document.layers[0].segments[0].params).shape =
        WindowParams::Shape::Trapezoid;
    panel.setDocument(document);
    QCOMPARE(panel.findChildren<EngineeringEdit*>().size(), plainFields + 2);  // rise and fall
}

void TestPropertyPanel::sweepFieldsFitTheDurationAndKeepTheTimesInStep() {
    PropertyPanel panel;
    WaveDocument document = singleSegmentDocument(SegmentType::Dpv);
    panel.setDocument(document);
    panel.setSelection({0}, 0);
    panel.show();
    panel.activateWindow();

    // A DPV offers the staircase, the pulse height, the two times and the
    // direction switch.
    for (const QString& label :
         {QStringLiteral("Start value"),
          QStringLiteral("End value"),
          QStringLiteral("Step value"),
          QStringLiteral("Pulse value"),
          QStringLiteral("Step time (s)"),
          QStringLiteral("Pulse time (s)")}) {
        QVERIFY2(fieldLabelled(panel, label), qPrintable(label));
    }
    QVERIFY(panel.findChild<QCheckBox*>(QStringLiteral("dpvInvertPulseCheck")));

    const Segment initial = document.layers[0].segments[0];
    auto* steps           = panel.findChild<QLabel*>(QStringLiteral("sweepStepCountLabel"));
    QVERIFY(steps);
    QCOMPARE(steps->text(), QString::number(static_cast<qulonglong>(sweepStepCount(initial))));

    // Halving the step value doubles the number of steps, and the duration
    // follows the sweep without the duration field having been touched.
    QSignalSpy changed(&panel, &PropertyPanel::segmentChanged);
    commitValue(fieldLabelled(panel, QStringLiteral("Step value")), QStringLiteral("5m"));
    QTRY_COMPARE(changed.count(), 1);
    Segment edited = qvariant_cast<Segment>(changed.takeFirst().at(2));
    QCOMPARE(std::get<DpvParams>(edited.params).stepValue, 0.005);
    QCOMPARE(sweepStepCount(edited), 2 * sweepStepCount(initial));
    QCOMPARE(edited.duration, sweepDuration(edited));

    document.layers[0].segments[0] = edited;
    panel.setDocument(document);
    QCOMPARE(
        panel.findChild<QLabel*>(QStringLiteral("sweepStepCountLabel"))->text(),
        QString::number(static_cast<qulonglong>(sweepStepCount(edited)))
    );

    // The duration itself stays the user's: setting it by hand is not refitted,
    // so a sweep can deliberately be cut short.
    commitValue(fieldLabelled(panel, QStringLiteral("Duration (s)")), QStringLiteral("1"));
    QTRY_COMPARE(changed.count(), 1);
    edited = qvariant_cast<Segment>(changed.takeFirst().at(2));
    QCOMPARE(edited.duration, 1.0);
    QVERIFY(edited.duration != sweepDuration(edited));

    // A pulse cannot outlast the step it opens, so the two times pull each other
    // along - and the next parameter edit fits the duration back to the sweep.
    document.layers[0].segments[0] = edited;
    panel.setDocument(document);
    commitValue(fieldLabelled(panel, QStringLiteral("Pulse time (s)")), QStringLiteral("500m"));
    QTRY_COMPARE(changed.count(), 1);
    edited                   = qvariant_cast<Segment>(changed.takeFirst().at(2));
    const auto& pulseTimings = std::get<DpvParams>(edited.params);
    QCOMPARE(pulseTimings.pulseTime, pulseTimings.stepTime);
    QCOMPARE(edited.duration, sweepDuration(edited));

    document.layers[0].segments[0] = edited;
    panel.setDocument(document);
    commitValue(fieldLabelled(panel, QStringLiteral("Step time (s)")), QStringLiteral("20m"));
    QTRY_COMPARE(changed.count(), 1);
    edited                  = qvariant_cast<Segment>(changed.takeFirst().at(2));
    const auto& stepTimings = std::get<DpvParams>(edited.params);
    QCOMPARE(stepTimings.stepTime, 0.02);
    QCOMPARE(stepTimings.pulseTime, 0.02);

    // A square wave sweep has a period and an amplitude in place of those, and
    // no pulse of its own.
    PropertyPanel swvPanel;
    swvPanel.setDocument(singleSegmentDocument(SegmentType::Swv));
    swvPanel.setSelection({0}, 0);
    swvPanel.show();
    QVERIFY(fieldLabelled(swvPanel, QStringLiteral("Amplitude")));
    QVERIFY(fieldLabelled(swvPanel, QStringLiteral("Period (s)")));
    QVERIFY(! fieldLabelled(swvPanel, QStringLiteral("Pulse time (s)")));
    QVERIFY(! swvPanel.findChild<QCheckBox*>(QStringLiteral("dpvInvertPulseCheck")));

    // Normal pulse voltammetry has the times but no pulse height: its pulses are
    // sized by the step value. What it does have, and the other two do not, is a
    // baseline of its own between the pulses.
    PropertyPanel npvPanel;
    WaveDocument npvDocument = singleSegmentDocument(SegmentType::Npv);
    npvPanel.setDocument(npvDocument);
    npvPanel.setSelection({0}, 0);
    npvPanel.show();
    QVERIFY(fieldLabelled(npvPanel, QStringLiteral("Pulse time (s)")));
    QVERIFY(! fieldLabelled(npvPanel, QStringLiteral("Pulse value")));
    QVERIFY(! npvPanel.findChild<QCheckBox*>(QStringLiteral("dpvInvertPulseCheck")));
    QVERIFY(! fieldLabelled(panel, QStringLiteral("Base value")));
    QVERIFY(! fieldLabelled(swvPanel, QStringLiteral("Base value")));

    // Moving the baseline leaves the pulse train, and with it the length of the
    // sweep, exactly where it was.
    const Segment npvInitial = npvDocument.layers[0].segments[0];
    QSignalSpy npvChanged(&npvPanel, &PropertyPanel::segmentChanged);
    commitValue(fieldLabelled(npvPanel, QStringLiteral("Base value")), QStringLiteral("-100m"));
    QTRY_COMPARE(npvChanged.count(), 1);
    const Segment npvEdited = qvariant_cast<Segment>(npvChanged.takeFirst().at(2));
    const auto& npvParams   = std::get<NpvParams>(npvEdited.params);
    QCOMPARE(npvParams.baseValue, -0.1);
    QCOMPARE(npvParams.startValue, std::get<NpvParams>(npvInitial.params).startValue);
    QCOMPARE(npvEdited.duration, npvInitial.duration);
}

QTEST_MAIN(TestPropertyPanel)
#include "tst_propertypanel.moc"
