#pragma once

#include "laplace_rail_model.h"

#include <QString>
#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSlider;

class LaplaceRailGraphWidget;
class LaplaceRailSketchWidget;

class LaplaceRailWidget final : public QWidget
{
public:
    explicit LaplaceRailWidget(QWidget *parent = nullptr);

private:
    LaplaceRail::Inputs inputsFromUi() const;
    void updateModeUi();
    void recalculate();
    void selectTime(double timeS);
    void updateStateReadout(const LaplaceRail::State &state);
    void copySummary();
    void createExercise();
    void checkExercise();

    QComboBox *m_mode = nullptr;
    QDoubleSpinBox *m_b = nullptr;
    QDoubleSpinBox *m_length = nullptr;
    QDoubleSpinBox *m_angle = nullptr;
    QDoubleSpinBox *m_mass = nullptr;
    QDoubleSpinBox *m_current = nullptr;
    QDoubleSpinBox *m_voltage = nullptr;
    QDoubleSpinBox *m_resistance = nullptr;
    QDoubleSpinBox *m_v0 = nullptr;
    QDoubleSpinBox *m_duration = nullptr;
    QComboBox *m_graphQuantity = nullptr;
    QSlider *m_timeSlider = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_stateLabel = nullptr;
    QLabel *m_formulaLabel = nullptr;
    QPlainTextEdit *m_summary = nullptr;
    LaplaceRailGraphWidget *m_graph = nullptr;
    LaplaceRailSketchWidget *m_sketch = nullptr;

    QComboBox *m_exerciseTarget = nullptr;
    QLabel *m_exercisePrompt = nullptr;
    QDoubleSpinBox *m_exerciseAnswer = nullptr;
    QLabel *m_exerciseStatus = nullptr;
    double m_expectedExerciseAnswer = 0.0;
    QString m_expectedExerciseUnit;

    LaplaceRail::Result m_result;
};
