#pragma once

#include "em_calculator_model.h"
#include "em_units_calculator_model.h"

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
class QTabWidget;
class QTimer;
class QCheckBox;
class QSpinBox;
class QToolButton;
class NotesInkCanvas;

class EmCalculatorWidget final : public QWidget
{
public:
    using CreateAntennaHandler = std::function<bool(EmCalculator::AntennaKind, const EmCalculator::AntennaInputs &, QString &)>;

    explicit EmCalculatorWidget(QWidget *parent = nullptr);
    ~EmCalculatorWidget() override;
    void setCreateAntennaHandler(CreateAntennaHandler handler);
    void setActiveAntennaProjectFile(const QString &path);

private:
    void buildWaveTab(QWidget *page);
    void buildAntennaTab(QWidget *page);
    void buildScientificTab(QWidget *page);
    void buildVariablesTab(QWidget *page);
    void evaluateVariableWorksheet();
    void buildConstantsTab(QWidget *page);
    void updateConstantsFilter();
    void updateConstantDetails(int row);
    void buildLaplaceRailTab(QWidget *page);
    void buildNotesTab(QWidget *page);

    void calculateWaveFromFrequency();
    void calculateWaveFromWavelength();
    void updateAntennaUi();
    bool calculateAntenna();
    void invalidateAntennaCalculation();
    void invalidateWaveCalculation();
    void displayWaveResult(const EmCalculator::WaveResult &result);
    void createAntennaInDesigner();
    EmCalculator::AntennaInputs antennaInputsFromUi() const;
    void evaluateScientificExpression();
    void refreshScientificDisplay();
    void invalidateScientificDisplay();
    QString scientificDisplayText() const;
    QString scientificExactText() const;
    void loadFormulaTemplate(int index);
    void insertExpressionToken(const QString &token);
    void copyText(const QString &text, const QString &status = QString());
    void refreshInkPageControls();
    bool loadInkNotes();
    bool saveInkNotes();
    QString inkNotesPath() const;
    void exportInkPng();
    void copyInkImage();
    bool loadNotes();
    bool saveNotes();
    bool saveWorksheet();
    bool loadWorksheet();
    void changeNotebookScope(int scope);
    void updateScopeDescription();
    QString notesBackupKey() const;
    QString projectNotesSidecarPath() const;
    QString projectWorksheetSidecarPath() const;
    void openNotesFile();
    void restorePreviousNotes();
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
    QPushButton *m_waveCopyFrequency = nullptr;
    QPushButton *m_waveCopyWavelength = nullptr;
    QPushButton *m_waveCopyResult = nullptr;
    EmCalculator::WaveResult m_validatedWaveResult;
    bool m_waveValid = false;

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
    QPushButton *m_antennaCopyFrequency = nullptr;
    QPushButton *m_antennaCopyPrimary = nullptr;
    QPushButton *m_antennaCopyResult = nullptr;
    EmCalculator::AntennaInputs m_validatedAntennaInputs;
    EmCalculator::AntennaResult m_validatedAntennaResult;
    EmCalculator::AntennaKind m_validatedAntennaKind = EmCalculator::AntennaKind::ExactHalfWaveDipole;
    bool m_antennaValid = false;
    CreateAntennaHandler m_createAntennaHandler;

    QTabWidget *m_calculatorTabs = nullptr;
    int m_scientificTabIndex = -1;
    int m_constantsTabIndex = -1;
    int m_variablesTabIndex = -1;
    int m_notesTabIndex = -1;
    double m_lastAnswer = 0.0;
    bool m_lastAnswerValid = false;
    EmCalculator::Quantity m_lastQuantity;
    EmCalculator::VariableTable m_userVariables;
    QComboBox *m_formulaTemplate = nullptr;
    QLabel *m_formulaDescription = nullptr;
    QLineEdit *m_expression = nullptr;
    QLineEdit *m_expressionResult = nullptr;
    QCheckBox *m_autoPrefix = nullptr;
    QSpinBox *m_significantDigits = nullptr;
    QPushButton *m_scienceCopyValue = nullptr;
    QPushButton *m_scienceCopyUnit = nullptr;
    QPushButton *m_scienceCopyExpression = nullptr;
    QPushButton *m_scienceSendNotes = nullptr;
    bool m_scientificValid = false;
    QString m_validatedScientificExpression;
    EmCalculator::Quantity m_validatedScientificQuantity;
    QLabel *m_expressionStatus = nullptr;
    QTableWidget *m_historyTable = nullptr;
    QLabel *m_expressionUnit = nullptr;
    QPlainTextEdit *m_variableWorksheet = nullptr;
    QTableWidget *m_variableResults = nullptr;
    QLabel *m_variableStatus = nullptr;
    QLabel *m_variableScopeLabel = nullptr;
    QTimer *m_variableSaveTimer = nullptr;
    QLineEdit *m_constantSearch = nullptr;
    QComboBox *m_constantCategory = nullptr;
    QLabel *m_constantDetails = nullptr;
    QLabel *m_constantsCount = nullptr;
    QTableWidget *m_constantsTable = nullptr;

    NotesInkCanvas *m_inkCanvas = nullptr;
    QSpinBox *m_inkPageSelector = nullptr;
    QLabel *m_inkPageInfo = nullptr;
    QLabel *m_inkStatus = nullptr;
    QTimer *m_inkSaveTimer = nullptr;
    QPlainTextEdit *m_notesEditor = nullptr;
    QLabel *m_notesStatus = nullptr;
    QComboBox *m_notebookScope = nullptr;
    QLabel *m_projectNotesLabel = nullptr;
    QString m_activeAntennaProjectFile;
    int m_currentNotebookScope = 0; // 0 = existing global QSettings; 1 = sidecars next to .qta
    bool m_switchingNotebook = false;
    QTimer *m_notesSaveTimer = nullptr;
};
