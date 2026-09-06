#include "widgets/em_calculator_widget.h"

#include "em_calculator_model.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSizePolicy>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
QDoubleSpinBox *makeNumber(QWidget *parent, double value, int decimals = 9)
{
    auto *spin = new QDoubleSpinBox(parent);
    spin->setDecimals(decimals);
    spin->setRange(0.0, 1.0e15);
    spin->setValue(value);
    spin->setKeyboardTracking(false);
    spin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return spin;
}

QComboBox *makeFrequencyUnits(QWidget *parent)
{
    auto *combo = new QComboBox(parent);
    combo->addItem(QStringLiteral("Hz"), 1.0);
    combo->addItem(QStringLiteral("kHz"), 1.0e3);
    combo->addItem(QStringLiteral("MHz"), 1.0e6);
    combo->addItem(QStringLiteral("GHz"), 1.0e9);
    combo->setCurrentIndex(2);
    return combo;
}

QComboBox *makeLengthUnits(QWidget *parent, int current = 2)
{
    auto *combo = new QComboBox(parent);
    combo->addItem(QStringLiteral("m"), 1.0);
    combo->addItem(QStringLiteral("cm"), 1.0e-2);
    combo->addItem(QStringLiteral("mm"), 1.0e-3);
    combo->addItem(QStringLiteral("um"), 1.0e-6);
    combo->setCurrentIndex(std::clamp(current, 0, combo->count() - 1));
    return combo;
}

QWidget *paired(QWidget *parent, QWidget *left, QWidget *right)
{
    auto *w = new QWidget(parent);
    auto *layout = new QHBoxLayout(w);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(left, 1);
    layout->addWidget(right, 0);
    return w;
}

QString escapedNote(const std::string &s) { return QString::fromStdString(s); }

struct FormulaTemplate
{
    const char *name;
    const char *expression;
    const char *description;
};

const FormulaTemplate kFormulaTemplates[] = {
    {"Custom expression", "", "Enter any expression in SI units. Double-click a constant below to insert it."},
    {"Exponential / decay", "exp(-2.5)", "Natural exponential e^x. Example evaluates e^-2.5; you can also write e^(-2.5)."},
    {"Coulomb force between two charges", "ke*(1e-9)*(1e-9)/(0.1^2)", "Example: q1=q2=1 nC, r=0.1 m. Result is force magnitude in N."},
    {"Electric field of a point charge", "ke*(1e-9)/(0.1^2)", "Example: q=1 nC at r=0.1 m. Result is |E| in V/m."},
    {"Potential of a point charge", "ke*(1e-9)/0.1", "Example: q=1 nC at r=0.1 m. Result is V in volts relative to infinity."},
    {"Parallel-plate capacitance", "eps0*4.2*(0.01*0.01)/1e-3", "Example: epsilon_r=4.2, A=1 cm x 1 cm, d=1 mm. Result is C in F; fringe fields neglected."},
    {"B field around a long straight wire", "mu0*1/(2*pi*0.01)", "Example: I=1 A at r=1 cm. Result is B in tesla."},
    {"B field inside an ideal solenoid", "mu0*500*1/0.1", "Example: N=500 turns, I=1 A, length=0.1 m. Result is B in tesla for an air core."},
    {"Free-space wave impedance", "sqrt(mu0/eps0)", "Returns eta0 in ohms."},
    {"Wave impedance in dielectric", "sqrt(mu0/(eps0*4.2))", "Example: lossless non-magnetic epsilon_r=4.2. Result is ohms."},
    {"LC resonance", "1/(2*pi*sqrt(10e-6*100e-9))", "Example: L=10 uH and C=100 nF. Result is resonant frequency in Hz."},
    {"Copper skin depth", "sqrt(2/(2*pi*1e9*mu0*5.8e7))", "Example: copper conductivity 5.8e7 S/m at 1 GHz. Result is skin depth in metres."},
    {"Capacitor stored energy", "0.5*100e-9*(5^2)", "Example: C=100 nF, V=5 V. Result is joules."},
    {"Inductor stored energy", "0.5*10e-6*(1^2)", "Example: L=10 uH, I=1 A. Result is joules."},
    {"Electromagnetic wave speed in dielectric", "1/sqrt(mu0*(eps0*4.2))", "Example: lossless non-magnetic epsilon_r=4.2. Result is m/s."}
};

} // namespace

EmCalculatorWidget::EmCalculatorWidget(QWidget *parent) : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    auto *intro = new QLabel(QStringLiteral(
        "Calculator / Notes — wave/frequency conversion, antenna starting dimensions, an SI scientific calculator with EM constants, and a persistent plain-text notebook. "
        "Closed-form antenna dimensions are starting estimates; use Antenna designer + convergence/validation for final geometry."), this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto *tabs = new QTabWidget(this);
    tabs->setDocumentMode(true);
    auto *wavePage = new QWidget(tabs);
    auto *antennaPage = new QWidget(tabs);
    auto *sciencePage = new QWidget(tabs);
    auto *notesPage = new QWidget(tabs);
    tabs->addTab(wavePage, QStringLiteral("Wave / frequency"));
    tabs->addTab(antennaPage, QStringLiteral("Antenna sizing"));
    tabs->addTab(sciencePage, QStringLiteral("EM scientific calculator"));
    tabs->addTab(notesPage, QStringLiteral("Notes"));
    tabs->setTabToolTip(0, QStringLiteral("Bidirectional f <-> lambda conversion, including period, omega and wave number."));
    tabs->setTabToolTip(1, QStringLiteral("Starting dimensions for common antennas, plus inverse frequency estimates when the geometry has a meaningful resonant length."));
    tabs->setTabToolTip(2, QStringLiteral("Expression calculator using SI units with electromagnetic and physical constants."));
    tabs->setTabToolTip(3, QStringLiteral("Persistent plain-text notes with standard edit, copy/paste and text-file import/export."));
    root->addWidget(tabs, 1);

    buildWaveTab(wavePage);
    buildAntennaTab(antennaPage);
    buildScientificTab(sciencePage);
    buildNotesTab(notesPage);
}

void EmCalculatorWidget::setCreateAntennaHandler(CreateAntennaHandler handler)
{
    m_createAntennaHandler = std::move(handler);
    if (m_createInDesigner) m_createInDesigner->setEnabled(static_cast<bool>(m_createAntennaHandler));
}

void EmCalculatorWidget::buildWaveTab(QWidget *page)
{
    auto *root = new QVBoxLayout(page);
    auto *inputs = new QGroupBox(QStringLiteral("Propagation medium and conversion"), page);
    auto *form = new QFormLayout(inputs);

    m_waveEr = makeNumber(inputs, 1.0, 8);
    m_waveEr->setMinimum(1.0e-9);
    m_waveMur = makeNumber(inputs, 1.0, 8);
    m_waveMur->setMinimum(1.0e-9);
    m_waveEr->setToolTip(QStringLiteral("Relative permittivity. Free space / air approximation: 1."));
    m_waveMur->setToolTip(QStringLiteral("Relative permeability. Most RF dielectrics: approximately 1."));
    form->addRow(QStringLiteral("Relative permittivity epsilon_r"), m_waveEr);
    form->addRow(QStringLiteral("Relative permeability mu_r"), m_waveMur);

    m_waveFrequency = makeNumber(inputs, 144.0, 9);
    m_waveFrequencyUnit = makeFrequencyUnits(inputs);
    form->addRow(QStringLiteral("Frequency"), paired(inputs, m_waveFrequency, m_waveFrequencyUnit));

    m_waveLength = makeNumber(inputs, 1.0, 9);
    m_waveLengthUnit = makeLengthUnits(inputs, 0);
    form->addRow(QStringLiteral("Wavelength lambda"), paired(inputs, m_waveLength, m_waveLengthUnit));

    auto *buttons = new QWidget(inputs);
    auto *buttonLayout = new QHBoxLayout(buttons);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    auto *fromFrequency = new QPushButton(QStringLiteral("Frequency -> wavelength"), buttons);
    auto *fromWavelength = new QPushButton(QStringLiteral("Wavelength -> frequency"), buttons);
    auto *copyFrequency = new QPushButton(QStringLiteral("Copy f value"), buttons);
    auto *copyWavelength = new QPushButton(QStringLiteral("Copy lambda value"), buttons);
    auto *copy = new QPushButton(QStringLiteral("Copy result"), buttons);
    buttonLayout->addWidget(fromFrequency);
    buttonLayout->addWidget(fromWavelength);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(copyFrequency);
    buttonLayout->addWidget(copyWavelength);
    buttonLayout->addWidget(copy);
    form->addRow(buttons);
    root->addWidget(inputs);

    m_waveResult = new QPlainTextEdit(page);
    m_waveResult->setReadOnly(true);
    m_waveResult->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_waveResult->setPlaceholderText(QStringLiteral("Calculated wavelength/frequency, period, angular frequency and wave number will appear here."));
    root->addWidget(m_waveResult, 1);

    connect(fromFrequency, &QPushButton::clicked, this, [this] { calculateWaveFromFrequency(); });
    connect(fromWavelength, &QPushButton::clicked, this, [this] { calculateWaveFromWavelength(); });
    connect(copyFrequency, &QPushButton::clicked, this, [this] { copyText(m_waveFrequency->cleanText()); });
    connect(copyWavelength, &QPushButton::clicked, this, [this] { copyText(m_waveLength->cleanText()); });
    connect(copy, &QPushButton::clicked, this, [this] { copyText(m_waveResult->toPlainText()); });
    calculateWaveFromFrequency();
}

void EmCalculatorWidget::buildAntennaTab(QWidget *page)
{
    auto *root = new QVBoxLayout(page);
    auto *box = new QGroupBox(QStringLiteral("Common antenna starting dimensions"), page);
    auto *form = new QFormLayout(box);

    m_antennaType = new QComboBox(box);
    for (int i = 0; i <= static_cast<int>(EmCalculator::AntennaKind::ParabolicDish); ++i)
    {
        const auto kind = static_cast<EmCalculator::AntennaKind>(i);
        m_antennaType->addItem(QString::fromLatin1(EmCalculator::antennaName(kind)), i);
    }
    form->addRow(QStringLiteral("Antenna"), m_antennaType);

    m_antennaSolveMode = new QComboBox(box);
    m_antennaSolveMode->addItem(QStringLiteral("Frequency -> dimensions"), 0);
    m_antennaSolveMode->addItem(QStringLiteral("Primary dimension -> frequency"), 1);
    form->addRow(QStringLiteral("Solve from"), m_antennaSolveMode);

    m_antennaFrequency = makeNumber(box, 144.0, 9);
    m_antennaFrequencyUnit = makeFrequencyUnits(box);
    form->addRow(QStringLiteral("Frequency"), paired(box, m_antennaFrequency, m_antennaFrequencyUnit));

    m_primaryDimensionLabel = new QLabel(QStringLiteral("Primary dimension"), box);
    m_primaryDimension = makeNumber(box, 0.5, 9);
    m_primaryDimensionUnit = makeLengthUnits(box, 0);
    form->addRow(m_primaryDimensionLabel, paired(box, m_primaryDimension, m_primaryDimensionUnit));

    m_velocityFactorLabel = new QLabel(QStringLiteral("Wire velocity / shortening factor"), box);
    m_velocityFactor = makeNumber(box, 1.0, 6);
    m_velocityFactor->setRange(0.01, 1.0);
    m_velocityFactor->setSingleStep(0.01);
    m_velocityFactor->setToolTip(QStringLiteral("Multiplies free-space wavelength for simple wire-length estimates. Use 1.0 for bare-wire geometric starts unless you have a justified shortening factor."));
    form->addRow(m_velocityFactorLabel, m_velocityFactor);

    m_patchErLabel = new QLabel(QStringLiteral("Patch substrate epsilon_r"), box);
    m_patchEr = makeNumber(box, 4.2, 6);
    m_patchEr->setRange(1.000001, 1000.0);
    form->addRow(m_patchErLabel, m_patchEr);

    m_patchHeightLabel = new QLabel(QStringLiteral("Patch substrate height h"), box);
    m_patchHeight = makeNumber(box, 1.6, 6);
    m_patchHeightUnit = makeLengthUnits(box, 2);
    form->addRow(m_patchHeightLabel, paired(box, m_patchHeight, m_patchHeightUnit));

    m_dishDiameterLabel = new QLabel(QStringLiteral("Dish diameter D"), box);
    m_dishDiameter = makeNumber(box, 1.0, 6);
    m_dishDiameterUnit = makeLengthUnits(box, 0);
    form->addRow(m_dishDiameterLabel, paired(box, m_dishDiameter, m_dishDiameterUnit));

    m_dishEfficiencyLabel = new QLabel(QStringLiteral("Aperture efficiency eta"), box);
    m_dishEfficiency = makeNumber(box, 0.60, 4);
    m_dishEfficiency->setRange(0.01, 1.0);
    form->addRow(m_dishEfficiencyLabel, m_dishEfficiency);

    m_focalRatioLabel = new QLabel(QStringLiteral("Focal ratio f/D"), box);
    m_focalRatio = makeNumber(box, 0.40, 4);
    m_focalRatio->setRange(0.01, 10.0);
    form->addRow(m_focalRatioLabel, m_focalRatio);

    m_desiredGainLabel = new QLabel(QStringLiteral("Requested gain for diameter estimate"), box);
    m_desiredGain = new QDoubleSpinBox(box);
    m_desiredGain->setRange(-40.0, 100.0);
    m_desiredGain->setDecimals(3);
    m_desiredGain->setValue(20.0);
    m_desiredGain->setSuffix(QStringLiteral(" dBi"));
    form->addRow(m_desiredGainLabel, m_desiredGain);

    auto *buttons = new QWidget(box);
    auto *buttonLayout = new QHBoxLayout(buttons);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    auto *calculate = new QPushButton(QStringLiteral("Calculate antenna dimensions"), buttons);
    auto *copyFrequency = new QPushButton(QStringLiteral("Copy f value"), buttons);
    auto *copyPrimary = new QPushButton(QStringLiteral("Copy primary value"), buttons);
    auto *copy = new QPushButton(QStringLiteral("Copy result"), buttons);
    m_createInDesigner = new QPushButton(QStringLiteral("Create in Antenna designer"), buttons);
    m_createInDesigner->setEnabled(false);
    m_createInDesigner->setToolTip(QStringLiteral("Build the calculated starting geometry in Antenna designer and switch to that workspace. Existing antenna geometry is replaced, with undo history preserved."));
    buttonLayout->addWidget(calculate);
    buttonLayout->addWidget(m_createInDesigner);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(copyFrequency);
    buttonLayout->addWidget(copyPrimary);
    buttonLayout->addWidget(copy);
    form->addRow(buttons);
    root->addWidget(box);

    m_antennaHint = new QLabel(page);
    m_antennaHint->setWordWrap(true);
    root->addWidget(m_antennaHint);

    m_antennaResult = new QPlainTextEdit(page);
    m_antennaResult->setReadOnly(true);
    m_antennaResult->setLineWrapMode(QPlainTextEdit::NoWrap);
    root->addWidget(m_antennaResult, 1);

    connect(m_antennaType, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updateAntennaUi(); });
    connect(m_antennaSolveMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updateAntennaUi(); });
    connect(calculate, &QPushButton::clicked, this, [this] { calculateAntenna(); });
    connect(m_createInDesigner, &QPushButton::clicked, this, [this] { createAntennaInDesigner(); });
    connect(copyFrequency, &QPushButton::clicked, this, [this] { copyText(m_antennaFrequency->cleanText()); });
    connect(copyPrimary, &QPushButton::clicked, this, [this] {
        const auto kind = static_cast<EmCalculator::AntennaKind>(m_antennaType->currentData().toInt());
        copyText(kind == EmCalculator::AntennaKind::ParabolicDish ? m_dishDiameter->cleanText() : m_primaryDimension->cleanText());
    });
    connect(copy, &QPushButton::clicked, this, [this] { copyText(m_antennaResult->toPlainText()); });

    updateAntennaUi();
    calculateAntenna();
}

void EmCalculatorWidget::buildScientificTab(QWidget *page)
{
    auto *root = new QVBoxLayout(page);

    auto *formulaBox = new QGroupBox(QStringLiteral("Expression and formula library"), page);
    auto *formulaLayout = new QFormLayout(formulaBox);
    m_formulaTemplate = new QComboBox(formulaBox);
    for (const auto &entry : kFormulaTemplates)
        m_formulaTemplate->addItem(QString::fromLatin1(entry.name));
    formulaLayout->addRow(QStringLiteral("Template"), m_formulaTemplate);

    m_formulaDescription = new QLabel(formulaBox);
    m_formulaDescription->setWordWrap(true);
    formulaLayout->addRow(QStringLiteral("Meaning"), m_formulaDescription);

    m_expression = new QLineEdit(formulaBox);
    m_expression->setPlaceholderText(QStringLiteral("Example: ke*(2e-9)*(-1e-9)/(0.05^2)"));
    m_expression->setClearButtonEnabled(true);
    formulaLayout->addRow(QStringLiteral("Expression (SI)"), m_expression);

    auto *functionRow = new QWidget(formulaBox);
    auto *functionLayout = new QHBoxLayout(functionRow);
    functionLayout->setContentsMargins(0, 0, 0, 0);
    for (const QString token : {QStringLiteral("sin("), QStringLiteral("cos("), QStringLiteral("tan("), QStringLiteral("sqrt("),
                                QStringLiteral("abs("), QStringLiteral("exp("), QStringLiteral("e^("), QStringLiteral("log10("), QStringLiteral("ln("),
                                QStringLiteral("sind("), QStringLiteral("pow(")})
    {
        auto *button = new QPushButton(token, functionRow);
        button->setMaximumWidth(78);
        connect(button, &QPushButton::clicked, this, [this, token] { insertExpressionToken(token); });
        functionLayout->addWidget(button);
    }
    functionLayout->addStretch(1);
    formulaLayout->addRow(QStringLiteral("Functions"), functionRow);

    auto *actionRow = new QWidget(formulaBox);
    auto *actionLayout = new QHBoxLayout(actionRow);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    auto *evaluate = new QPushButton(QStringLiteral("Evaluate"), actionRow);
    auto *copyResult = new QPushButton(QStringLiteral("Copy result"), actionRow);
    auto *copyExpression = new QPushButton(QStringLiteral("Copy expression"), actionRow);
    actionLayout->addWidget(evaluate);
    actionLayout->addWidget(copyResult);
    actionLayout->addWidget(copyExpression);
    actionLayout->addStretch(1);
    formulaLayout->addRow(actionRow);

    m_expressionResult = new QLineEdit(formulaBox);
    m_expressionResult->setReadOnly(true);
    m_expressionResult->setPlaceholderText(QStringLiteral("Result"));
    formulaLayout->addRow(QStringLiteral("Result"), m_expressionResult);
    m_expressionStatus = new QLabel(formulaBox);
    m_expressionStatus->setWordWrap(true);
    formulaLayout->addRow(QStringLiteral("Status"), m_expressionStatus);
    root->addWidget(formulaBox);

    auto *constantsBox = new QGroupBox(QStringLiteral("Built-in constants — double-click a symbol to insert it"), page);
    auto *constantsLayout = new QVBoxLayout(constantsBox);
    m_constantsTable = new QTableWidget(constantsBox);
    m_constantsTable->setColumnCount(4);
    m_constantsTable->setHorizontalHeaderLabels({QStringLiteral("Symbol"), QStringLiteral("Value"), QStringLiteral("Unit"), QStringLiteral("Meaning")});
    const struct ConstantInfo { const char *symbol; double value; const char *unit; const char *meaning; } constants[] = {
        {"pi", EmCalculator::pi, "", "Pi"},
        {"e", EmCalculator::euler, "", "Euler number"},
        {"eps0", EmCalculator::epsilon0, "F/m", "Vacuum permittivity"},
        {"mu0", EmCalculator::mu0, "H/m", "Vacuum permeability"},
        {"c0", EmCalculator::c0, "m/s", "Speed of light in vacuum"},
        {"ke", EmCalculator::coulombK, "N m^2/C^2", "Coulomb constant (alias: k)"},
        {"eta0", EmCalculator::eta0, "ohm", "Free-space wave impedance (alias: z0)"},
        {"h", EmCalculator::planckH, "J s", "Planck constant"},
        {"hbar", EmCalculator::hbar, "J s", "Reduced Planck constant"},
        {"qe", EmCalculator::elementaryCharge, "C", "Elementary charge magnitude"},
        {"me", EmCalculator::electronMass, "kg", "Electron mass"}
    };
    m_constantsTable->setRowCount(static_cast<int>(sizeof(constants) / sizeof(constants[0])));
    for (int row = 0; row < m_constantsTable->rowCount(); ++row)
    {
        m_constantsTable->setItem(row, 0, new QTableWidgetItem(QString::fromLatin1(constants[row].symbol)));
        m_constantsTable->setItem(row, 1, new QTableWidgetItem(precise(constants[row].value)));
        m_constantsTable->setItem(row, 2, new QTableWidgetItem(QString::fromLatin1(constants[row].unit)));
        m_constantsTable->setItem(row, 3, new QTableWidgetItem(QString::fromLatin1(constants[row].meaning)));
    }
    m_constantsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_constantsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_constantsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_constantsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_constantsTable->verticalHeader()->setVisible(false);
    m_constantsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    constantsLayout->addWidget(m_constantsTable);
    root->addWidget(constantsBox, 1);

    auto *syntax = new QLabel(QStringLiteral(
        "Syntax: + - * / ^, parentheses; sin/cos/tan use radians; sind/cosd/tand use degrees. "
        "Functions: asin, acos, atan, sqrt, abs, exp, ln/log, log10, floor, ceil, pow(a,b), min(a,b), max(a,b), deg2rad, rad2deg. "
        "All numerical values are interpreted in coherent SI units."), page);
    syntax->setWordWrap(true);
    root->addWidget(syntax);

    connect(m_formulaTemplate, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) { loadFormulaTemplate(index); });
    connect(evaluate, &QPushButton::clicked, this, [this] { evaluateScientificExpression(); });
    connect(m_expression, &QLineEdit::returnPressed, this, [this] { evaluateScientificExpression(); });
    connect(copyResult, &QPushButton::clicked, this, [this] { copyText(m_expressionResult->text()); });
    connect(copyExpression, &QPushButton::clicked, this, [this] { copyText(m_expression->text()); });
    connect(m_constantsTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (auto *item = m_constantsTable->item(row, 0)) insertExpressionToken(item->text());
    });

    loadFormulaTemplate(2);
    evaluateScientificExpression();
}

void EmCalculatorWidget::buildNotesTab(QWidget *page)
{
    auto *root = new QVBoxLayout(page);

    auto *toolbar = new QWidget(page);
    auto *buttons = new QHBoxLayout(toolbar);
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(6);

    auto addButton = [&](const QString &text, const QString &tip, auto action) {
        auto *button = new QPushButton(text, toolbar);
        button->setToolTip(tip);
        connect(button, &QPushButton::clicked, this, action);
        buttons->addWidget(button);
        return button;
    };

    addButton(QStringLiteral("Open .txt"), QStringLiteral("Replace the notebook contents with a plain-text file."), [this] { openNotesFile(); });
    addButton(QStringLiteral("Save as .txt"), QStringLiteral("Export the current notebook as a plain-text file."), [this] { saveNotesFile(); });
    buttons->addSpacing(12);
    addButton(QStringLiteral("Undo"), QStringLiteral("Undo (Ctrl+Z)."), [this] { if (m_notesEditor) m_notesEditor->undo(); });
    addButton(QStringLiteral("Redo"), QStringLiteral("Redo (Ctrl+Y / Ctrl+Shift+Z)."), [this] { if (m_notesEditor) m_notesEditor->redo(); });
    addButton(QStringLiteral("Cut"), QStringLiteral("Cut selection (Ctrl+X)."), [this] { if (m_notesEditor) m_notesEditor->cut(); });
    addButton(QStringLiteral("Copy"), QStringLiteral("Copy selection (Ctrl+C)."), [this] { if (m_notesEditor) m_notesEditor->copy(); });
    addButton(QStringLiteral("Paste"), QStringLiteral("Paste clipboard text (Ctrl+V)."), [this] { if (m_notesEditor) m_notesEditor->paste(); });
    addButton(QStringLiteral("Select all"), QStringLiteral("Select all text (Ctrl+A)."), [this] { if (m_notesEditor) m_notesEditor->selectAll(); });
    buttons->addStretch(1);
    addButton(QStringLiteral("Clear"), QStringLiteral("Clear the notebook. Undo remains available immediately afterwards."), [this] { if (m_notesEditor) m_notesEditor->clear(); });
    root->addWidget(toolbar);

    auto *hint = new QLabel(QStringLiteral(
        "Plain-text engineering notebook. Standard keyboard shortcuts, selection and copy/paste work normally. "
        "The text is autosaved locally for this QTsignalApp user profile; Open/Save as .txt can be used for portable notes."), page);
    hint->setWordWrap(true);
    root->addWidget(hint);

    m_notesEditor = new QPlainTextEdit(page);
    m_notesEditor->setPlaceholderText(QStringLiteral("Write calculations, reminders, dimensions, observations, test notes, equations, etc. here..."));
    m_notesEditor->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_notesEditor->setTabChangesFocus(false);
    root->addWidget(m_notesEditor, 1);

    m_notesStatus = new QLabel(page);
    root->addWidget(m_notesStatus);

    m_notesSaveTimer = new QTimer(this);
    m_notesSaveTimer->setSingleShot(true);
    m_notesSaveTimer->setInterval(600);
    connect(m_notesSaveTimer, &QTimer::timeout, this, [this] { saveNotes(); });
    connect(m_notesEditor, &QPlainTextEdit::textChanged, this, [this] {
        if (m_notesSaveTimer) m_notesSaveTimer->start();
        updateNotesStatus(QStringLiteral("Modified — autosave pending"));
    });
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        if (m_notesSaveTimer) m_notesSaveTimer->stop();
        saveNotes();
    });

    loadNotes();
}

void EmCalculatorWidget::loadNotes()
{
    if (!m_notesEditor) return;
    QSettings settings;
    m_notesEditor->setPlainText(settings.value(QStringLiteral("calculatorNotes/plainText")).toString());
    if (m_notesEditor->document()) m_notesEditor->document()->setModified(false);
    updateNotesStatus(QStringLiteral("Loaded / autosaved locally"));
}

void EmCalculatorWidget::saveNotes()
{
    if (!m_notesEditor) return;
    QSettings settings;
    settings.setValue(QStringLiteral("calculatorNotes/plainText"), m_notesEditor->toPlainText());
    if (m_notesEditor->document()) m_notesEditor->document()->setModified(false);
    updateNotesStatus(QStringLiteral("Autosaved locally"));
}

void EmCalculatorWidget::openNotesFile()
{
    if (!m_notesEditor) return;
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open notes"), QString(), QStringLiteral("Text files (*.txt *.md);;All files (*.*)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        updateNotesStatus(QStringLiteral("ERROR: could not open %1").arg(path));
        return;
    }
    QTextStream stream(&file);
    m_notesEditor->setPlainText(stream.readAll());
    saveNotes();
    updateNotesStatus(QStringLiteral("Opened %1 — autosaved locally").arg(path));
}

void EmCalculatorWidget::saveNotesFile()
{
    if (!m_notesEditor) return;
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save notes"), QStringLiteral("QTsignalApp_notes.txt"), QStringLiteral("Text files (*.txt);;Markdown (*.md);;All files (*.*)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        updateNotesStatus(QStringLiteral("ERROR: could not write %1").arg(path));
        return;
    }
    QTextStream stream(&file);
    stream << m_notesEditor->toPlainText();
    file.close();
    saveNotes();
    updateNotesStatus(QStringLiteral("Saved %1").arg(path));
}

void EmCalculatorWidget::updateNotesStatus(const QString &prefix)
{
    if (!m_notesEditor || !m_notesStatus) return;
    const QString text = m_notesEditor->toPlainText();
    const int chars = text.size();
    const int lines = m_notesEditor->document() ? m_notesEditor->document()->blockCount() : 0;
    const QString lead = prefix.isEmpty() ? QStringLiteral("Notes") : prefix;
    m_notesStatus->setText(QStringLiteral("%1 — %2 line(s), %3 character(s). Copy/paste shortcuts: Ctrl+C / Ctrl+V.")
                               .arg(lead).arg(lines).arg(chars));
}

void EmCalculatorWidget::calculateWaveFromFrequency()
{
    EmCalculator::WaveResult r;
    std::string error;
    if (!EmCalculator::waveFromFrequency(scaledValue(m_waveFrequency->value(), m_waveFrequencyUnit),
                                         m_waveEr->value(), m_waveMur->value(), r, error))
    {
        m_waveResult->setPlainText(QStringLiteral("ERROR: %1").arg(QString::fromStdString(error)));
        return;
    }
    const double selectedScale = m_waveLengthUnit->currentData().toDouble();
    m_waveLength->setValue(r.wavelengthM / selectedScale);
    const QString text = QStringLiteral(
        "Frequency      = %1  [%2 Hz]\n"
        "Wavelength     = %3  [%4 m]\n"
        "Phase velocity = %5\n"
        "Period T       = %6  [%7 s]\n"
        "Angular freq.  = %8\n"
        "Wave number k  = %9\n"
        "Medium         = epsilon_r=%10, mu_r=%11")
        .arg(engineering(r.frequencyHz, QStringLiteral("Hz")))
        .arg(precise(r.frequencyHz))
        .arg(engineering(r.wavelengthM, QStringLiteral("m")))
        .arg(precise(r.wavelengthM))
        .arg(engineering(r.phaseVelocityMS, QStringLiteral("m/s")))
        .arg(engineering(r.periodS, QStringLiteral("s")))
        .arg(precise(r.periodS))
        .arg(engineering(r.angularFrequencyRadS, QStringLiteral("rad/s")))
        .arg(engineering(r.waveNumberRadM, QStringLiteral("rad/m")))
        .arg(m_waveEr->value(), 0, 'g', 10)
        .arg(m_waveMur->value(), 0, 'g', 10);
    m_waveResult->setPlainText(text);
}

void EmCalculatorWidget::calculateWaveFromWavelength()
{
    EmCalculator::WaveResult r;
    std::string error;
    if (!EmCalculator::waveFromWavelength(scaledValue(m_waveLength->value(), m_waveLengthUnit),
                                          m_waveEr->value(), m_waveMur->value(), r, error))
    {
        m_waveResult->setPlainText(QStringLiteral("ERROR: %1").arg(QString::fromStdString(error)));
        return;
    }
    const double selectedScale = m_waveFrequencyUnit->currentData().toDouble();
    m_waveFrequency->setValue(r.frequencyHz / selectedScale);
    calculateWaveFromFrequency();
}

void EmCalculatorWidget::updateAntennaUi()
{
    const auto kind = static_cast<EmCalculator::AntennaKind>(m_antennaType->currentData().toInt());
    const bool patch = kind == EmCalculator::AntennaKind::RectangularPatch;
    const bool dish = kind == EmCalculator::AntennaKind::ParabolicDish;
    const bool inverse = m_antennaSolveMode->currentData().toInt() == 1;

    m_primaryDimensionLabel->setText(QString::fromLatin1(EmCalculator::primaryDimensionName(kind)));
    m_primaryDimensionLabel->setVisible(!dish);
    m_primaryDimension->setVisible(!dish);
    m_primaryDimensionUnit->setVisible(!dish);
    m_antennaFrequency->setEnabled(!inverse || dish);
    m_antennaFrequencyUnit->setEnabled(!inverse || dish);
    m_primaryDimension->setEnabled(inverse && !dish);
    m_primaryDimensionUnit->setEnabled(inverse && !dish);

    m_antennaSolveMode->setItemData(1, dish ? 0 : 1, Qt::UserRole - 1);
    if (dish && inverse) m_antennaSolveMode->setCurrentIndex(0);

    const bool wire = !patch && !dish;
    m_velocityFactorLabel->setVisible(wire);
    m_velocityFactor->setVisible(wire);
    m_patchErLabel->setVisible(patch);
    m_patchEr->setVisible(patch);
    m_patchHeightLabel->setVisible(patch);
    m_patchHeight->setVisible(patch);
    m_patchHeightUnit->setVisible(patch);
    m_dishDiameterLabel->setVisible(dish);
    m_dishDiameter->setVisible(dish);
    m_dishDiameterUnit->setVisible(dish);
    m_dishEfficiencyLabel->setVisible(dish);
    m_dishEfficiency->setVisible(dish);
    m_focalRatioLabel->setVisible(dish);
    m_focalRatio->setVisible(dish);
    m_desiredGainLabel->setVisible(dish);
    m_desiredGain->setVisible(dish);

    if (dish)
        m_antennaHint->setText(QStringLiteral("A parabolic reflector has no unique resonant frequency determined by its diameter. Enter operating frequency and dish diameter; the calculator estimates gain, beamwidth, focal distance and the diameter required for the requested gain."));
    else if (patch)
        m_antennaHint->setText(QStringLiteral("Rectangular patch uses a Hammerstad/cavity starting estimate. epsilon_r and substrate height are required; the inverse L -> f calculation is solved numerically with the same model."));
    else
        m_antennaHint->setText(QStringLiteral("Wire dimensions use free-space wavelength multiplied by the selected velocity/shortening factor. Values are engineering starting points, not guaranteed final resonance or 50-ohm matching."));
}

EmCalculator::AntennaInputs EmCalculatorWidget::antennaInputsFromUi() const
{
    EmCalculator::AntennaInputs in;
    in.frequencyHz = scaledValue(m_antennaFrequency->value(), m_antennaFrequencyUnit);
    in.primaryDimensionM = scaledValue(m_primaryDimension->value(), m_primaryDimensionUnit);
    in.velocityFactor = m_velocityFactor->value();
    in.relativePermittivity = m_patchEr->value();
    in.substrateHeightM = scaledValue(m_patchHeight->value(), m_patchHeightUnit);
    in.dishDiameterM = scaledValue(m_dishDiameter->value(), m_dishDiameterUnit);
    in.dishEfficiency = m_dishEfficiency->value();
    in.focalRatio = m_focalRatio->value();
    in.desiredGainDbi = m_desiredGain->value();
    return in;
}

void EmCalculatorWidget::createAntennaInDesigner()
{
    calculateAntenna(); // In inverse mode this resolves and writes the resulting frequency first.
    if (!m_createAntennaHandler)
    {
        m_antennaHint->setText(QStringLiteral("Antenna designer is not available in this workspace."));
        return;
    }
    const auto kind = static_cast<EmCalculator::AntennaKind>(m_antennaType->currentData().toInt());
    EmCalculator::AntennaInputs in = antennaInputsFromUi();
    QString error;
    if (!m_createAntennaHandler(kind, in, error))
    {
        m_antennaHint->setText(QStringLiteral("ERROR creating geometry: %1").arg(error));
        return;
    }
    m_antennaHint->setText(QStringLiteral("Created %1 in Antenna designer at %2 MHz. The generated geometry is an engineering starting point; tune and validate it with the appropriate solver.")
                               .arg(QString::fromLatin1(EmCalculator::antennaName(kind)))
                               .arg(in.frequencyHz / 1e6, 0, 'g', 9));
}

void EmCalculatorWidget::calculateAntenna()
{
    const auto kind = static_cast<EmCalculator::AntennaKind>(m_antennaType->currentData().toInt());
    EmCalculator::AntennaInputs in = antennaInputsFromUi();

    EmCalculator::AntennaResult r;
    std::string error;
    const bool inverse = m_antennaSolveMode->currentData().toInt() == 1;
    const bool ok = inverse ? EmCalculator::antennaFromPrimaryDimension(kind, in, r, error)
                            : EmCalculator::antennaFromFrequency(kind, in, r, error);
    if (!ok)
    {
        m_antennaResult->setPlainText(QStringLiteral("ERROR: %1").arg(QString::fromStdString(error)));
        return;
    }

    if (inverse)
    {
        const double scale = m_antennaFrequencyUnit->currentData().toDouble();
        m_antennaFrequency->setValue(r.frequencyHz / scale);
    }
    else if (kind != EmCalculator::AntennaKind::ParabolicDish)
    {
        const double scale = m_primaryDimensionUnit->currentData().toDouble();
        m_primaryDimension->setValue(r.primaryDimensionM / scale);
    }

    QStringList lines;
    lines << QStringLiteral("%1").arg(QString::fromLatin1(EmCalculator::antennaName(kind)));
    lines << QStringLiteral("Frequency  = %1  [%2 Hz]").arg(engineering(r.frequencyHz, QStringLiteral("Hz")), precise(r.frequencyHz));
    lines << QStringLiteral("Lambda0    = %1  [%2 m]").arg(engineering(r.wavelengthM, QStringLiteral("m")), precise(r.wavelengthM));
    lines << QString();
    for (const auto &item : r.items)
    {
        QString value;
        if (item.unit == "m") value = engineering(item.value, QStringLiteral("m"));
        else if (item.unit.empty()) value = precise(item.value);
        else value = QStringLiteral("%1 %2").arg(precise(item.value), QString::fromStdString(item.unit));
        QString line = QString::fromStdString(item.label).leftJustified(36, QLatin1Char(' ')) + QStringLiteral(" = ") + value;
        if (!item.note.empty()) line += QStringLiteral("   # ") + QString::fromStdString(item.note);
        lines << line;
    }
    lines << QString();
    lines << QStringLiteral("Assumption: %1").arg(escapedNote(r.assumption));
    lines << QStringLiteral("Use these values as a starting geometry; validate/tune the final design in Antenna designer or with an independent solver/measurement when required.");
    m_antennaResult->setPlainText(lines.join(QLatin1Char('\n')));
}

void EmCalculatorWidget::evaluateScientificExpression()
{
    const EmCalculator::ExpressionResult r = EmCalculator::evaluateExpression(m_expression->text().toStdString());
    if (!r.ok)
    {
        m_expressionResult->clear();
        m_expressionStatus->setText(QStringLiteral("ERROR at position %1: %2").arg(r.errorPosition).arg(QString::fromStdString(r.error)));
        return;
    }
    m_expressionResult->setText(precise(r.value));
    m_expressionStatus->setText(QStringLiteral("OK — %1").arg(engineering(r.value)));
}

void EmCalculatorWidget::loadFormulaTemplate(int index)
{
    index = std::clamp(index, 0, static_cast<int>(sizeof(kFormulaTemplates) / sizeof(kFormulaTemplates[0])) - 1);
    const auto &entry = kFormulaTemplates[index];
    m_formulaDescription->setText(QString::fromLatin1(entry.description));
    if (index != 0)
        m_expression->setText(QString::fromLatin1(entry.expression));
}

void EmCalculatorWidget::insertExpressionToken(const QString &token)
{
    const int position = m_expression->cursorPosition();
    QString text = m_expression->text();
    text.insert(position, token);
    m_expression->setText(text);
    m_expression->setCursorPosition(position + token.size());
    m_expression->setFocus();
}

void EmCalculatorWidget::copyText(const QString &text, const QString &)
{
    if (auto *clipboard = QApplication::clipboard()) clipboard->setText(text);
}

double EmCalculatorWidget::scaledValue(double value, const QComboBox *unitCombo)
{
    return value * (unitCombo ? unitCombo->currentData().toDouble() : 1.0);
}

QString EmCalculatorWidget::engineering(double value, const QString &unit)
{
    if (!std::isfinite(value)) return QStringLiteral("n/a");
    if (value == 0.0) return unit.isEmpty() ? QStringLiteral("0") : QStringLiteral("0 %1").arg(unit);
    const double a = std::fabs(value);
    const struct Prefix { double scale; const char *text; } prefixes[] = {
        {1e12, "T"}, {1e9, "G"}, {1e6, "M"}, {1e3, "k"}, {1.0, ""},
        {1e-3, "m"}, {1e-6, "u"}, {1e-9, "n"}, {1e-12, "p"}, {1e-15, "f"}
    };
    for (const auto &p : prefixes)
    {
        const double scaled = a / p.scale;
        if (scaled >= 1.0 && scaled < 1000.0)
            return QStringLiteral("%1 %2%3").arg(value / p.scale, 0, 'g', 8).arg(QString::fromLatin1(p.text), unit);
    }
    return unit.isEmpty() ? precise(value) : QStringLiteral("%1 %2").arg(precise(value), unit);
}

QString EmCalculatorWidget::precise(double value)
{
    if (!std::isfinite(value)) return QStringLiteral("n/a");
    return QString::number(value, 'g', 15);
}
