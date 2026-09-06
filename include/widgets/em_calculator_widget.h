#pragma once

#include "em_calculator_model.h"

#include <QWidget>
#include <QString>

#include <functional>

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTimer;

class EmCalculatorWidget final : public QWidget
{
public:
    using CreateAntennaHandler = std::function<bool(EmCalculator::AntennaKind, const EmCalculator::AntennaInputs &, QString &)>;

    explicit EmCalculatorWidget(QWidget *parent = nullptr);
    void setCreateAntennaHandler(CreateAntennaHandler handler);

private:
    void buildWaveTab(QWidget *page);
    void buildAntennaTab(QWidget *page);
    void buildScientificTab(QWidget *page);
    void buildNotesTab(QWidget *page);

    void calculateWaveFromFrequency();
    void calculateWaveFromWavelength();
    void updateAntennaUi();
    void calculateAntenna();
    void createAntennaInDesigner();
    EmCalculator::AntennaInputs antennaInputsFromUi() const;
    void evaluateScientificExpression();
    void loadFormulaTemplate(int index);
    void insertExpressionToken(const QString &token);
    void copyText(const QString &text, const QString &status = QString());
    void loadNotes();
    void saveNotes();
    void openNotesFile();
    void saveNotesFile();
    void updateNotesStatus(const QString &prefix = QString());

    static double scaledValue(double value, const QComboBox *unitCombo);
    static QString engineering(double value, const QString &unit = QString());
    static QString precise(double value);

    QDoubleSpinBox *m_waveFrequency = nullptr;
    QComboBox *m_waveFrequencyUnit = nullptr;
    QDoubleSpinBox *m_waveLength = nullptr;
    QComboBox *m_waveLengthUnit = nullptr;
    QDoubleSpinBox *m_waveEr = nullptr;
    QDoubleSpinBox *m_waveMur = nullptr;
    QPlainTextEdit *m_waveResult = nullptr;

    QComboBox *m_antennaType = nullptr;
    QComboBox *m_antennaSolveMode = nullptr;
    QDoubleSpinBox *m_antennaFrequency = nullptr;
    QComboBox *m_antennaFrequencyUnit = nullptr;
    QLabel *m_primaryDimensionLabel = nullptr;
    QDoubleSpinBox *m_primaryDimension = nullptr;
    QComboBox *m_primaryDimensionUnit = nullptr;
    QDoubleSpinBox *m_velocityFactor = nullptr;
    QLabel *m_velocityFactorLabel = nullptr;
    QDoubleSpinBox *m_patchEr = nullptr;
    QLabel *m_patchErLabel = nullptr;
    QDoubleSpinBox *m_patchHeight = nullptr;
    QComboBox *m_patchHeightUnit = nullptr;
    QLabel *m_patchHeightLabel = nullptr;
    QDoubleSpinBox *m_dishDiameter = nullptr;
    QComboBox *m_dishDiameterUnit = nullptr;
    QLabel *m_dishDiameterLabel = nullptr;
    QDoubleSpinBox *m_dishEfficiency = nullptr;
    QLabel *m_dishEfficiencyLabel = nullptr;
    QDoubleSpinBox *m_focalRatio = nullptr;
    QLabel *m_focalRatioLabel = nullptr;
    QDoubleSpinBox *m_desiredGain = nullptr;
    QLabel *m_desiredGainLabel = nullptr;
    QPlainTextEdit *m_antennaResult = nullptr;
    QLabel *m_antennaHint = nullptr;
    QPushButton *m_createInDesigner = nullptr;
    CreateAntennaHandler m_createAntennaHandler;

    QComboBox *m_formulaTemplate = nullptr;
    QLabel *m_formulaDescription = nullptr;
    QLineEdit *m_expression = nullptr;
    QLineEdit *m_expressionResult = nullptr;
    QLabel *m_expressionStatus = nullptr;
    QTableWidget *m_constantsTable = nullptr;

    QPlainTextEdit *m_notesEditor = nullptr;
    QLabel *m_notesStatus = nullptr;
    QTimer *m_notesSaveTimer = nullptr;
};
