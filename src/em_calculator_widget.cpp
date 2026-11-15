#include "widgets/em_calculator_widget.h"

#include "em_calculator_model.h"
#include "em_display_format.h"
#include "widgets/laplace_rail_widget.h"
#include "widgets/notes_ink_canvas.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QImageReader>
#include <QInputDialog>
#include <QSaveFile>
#include <QDir>
#include <QStandardPaths>
#include <QShortcut>
#include <QSpinBox>
#include <QTabBar>
#include <QToolButton>
#include <QDialog>
#include <QJsonDocument>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QAbstractItemView>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QDateTime>
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
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

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
    {"Coulomb force between two charges", "ke*(1 nC)*(1 nC)/(10 cm)^2", "Example: q1=q2=1 nC, r=0.1 m. Result is force magnitude in N."},
    {"Electric field of a point charge", "ke*(1 nC)/(10 cm)^2", "Example: q=1 nC at r=0.1 m. Result is |E| in V/m."},
    {"Potential of a point charge", "ke*(1 nC)/(10 cm)", "Example: q=1 nC at r=0.1 m. Result is V in volts relative to infinity."},
    {"Parallel-plate capacitance", "eps0*4.2*(1 cm)*(1 cm)/(1 mm)", "Example: epsilon_r=4.2, A=1 cm x 1 cm, d=1 mm. Result is C in F; fringe fields neglected."},
    {"Vacuum permittivity epsilon0", "eps0", "CODATA 2022 vacuum permittivity in F/m. Use the Physical constants tab for definition and source."},
    {"Capacitance of isolated sphere", "4*pi*eps0*(10 cm)", "Isolated conducting sphere of radius 0.1 m in vacuum. Result C=4*pi*eps0*r in farads."},
    {"Electric-field energy density", "0.5*eps0*(1000 V/m)^2", "Vacuum electric energy density in J/m^3 for E=1000 V/m."},
    {"Magnetic-field energy density", "(0.01 T)^2/(2*mu0)", "Vacuum magnetic energy density in J/m^3 for B=0.01 T."},
    {"Plane-wave average Poynting flux", "(10 V/m)^2/(2*eta0)", "Time-average power density in W/m^2 for a free-space sinusoidal plane wave with peak |E|=10 V/m."},
    {"B field around a long straight wire", "mu0*(1 A)/(2*pi*(1 cm))", "Example: I=1 A at r=1 cm. Result is B in tesla."},
    {"Laplace force on straight conductor", "(0.5 T)*(2 A)*(0.1 m)*sind(90)", "Example: B=0.5 T, I=2 A, active length L=0.1 m, theta=90 deg. Result is F=B I L sin(theta) in N."},
    {"Motional EMF on moving rod", "(0.5 T)*(0.1 m)*(3 m/s)*sind(90)", "Example: B=0.5 T, L=0.1 m, v=3 m/s, theta=90 deg. Result is e=B L v sin(theta) in V."},
    {"B field inside an ideal solenoid", "mu0*500*(1 A)/(0.1 m)", "Example: N=500 turns, I=1 A, length=0.1 m. Result is B in tesla for an air core."},
    {"Free-space wave impedance", "sqrt(mu0/eps0)", "Returns eta0 in ohms."},
    {"Wave impedance in dielectric", "sqrt(mu0/(eps0*4.2))", "Example: lossless non-magnetic epsilon_r=4.2. Result is ohms."},
    {"LC resonance", "1/(2*pi*sqrt(10 uH*100 nF))", "Example: L=10 uH and C=100 nF. Result is resonant frequency in Hz."},
    {"Copper skin depth", "sqrt(2/(2*pi*(1 GHz)*mu0*sigma_cu))", "Example: copper conductivity 5.8e7 S/m at 1 GHz. Result is skin depth in metres."},
    {"Capacitor stored energy", "0.5*(100 nF)*(5 V)^2", "Example: C=100 nF, V=5 V. Result is joules."},
    {"Inductor stored energy", "0.5*(10 uH)*(1 A)^2", "Example: L=10 uH, I=1 A. Result is joules."},
    {"Electromagnetic wave speed in dielectric", "1/sqrt(mu0*(eps0*4.2))", "Example: lossless non-magnetic epsilon_r=4.2. Result is m/s."}
};

} // namespace

EmCalculatorWidget::EmCalculatorWidget(QWidget *parent) : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    auto *intro = new QLabel(QStringLiteral(
        "Calculate, explore physical constants and keep notes. Values are expressed in SI unless a unit is selected. "
        "Antenna dimensions are initial estimates to verify in the Designer."), this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto *tabs = new QTabWidget(this);
    m_calculatorTabs = tabs;
    tabs->setObjectName(QStringLiteral("emCalculatorTabs"));
    tabs->setDocumentMode(true);
    auto *wavePage = new QWidget(tabs);
    auto *antennaPage = new QWidget(tabs);
    auto *sciencePage = new QWidget(tabs);
    auto *constantsPage = new QWidget(tabs);
    auto *variablesPage = new QWidget(tabs);
    auto *laplacePage = new QWidget(tabs);
    auto *notesPage = new QWidget(tabs);
    m_scientificTabIndex = tabs->addTab(sciencePage, QStringLiteral("Scientific"));
    m_variablesTabIndex = tabs->addTab(variablesPage, QStringLiteral("Variables & units"));
    m_constantsTabIndex = tabs->addTab(constantsPage, QStringLiteral("Constants"));
    tabs->addTab(wavePage, QStringLiteral("Wave / frequency"));
    tabs->addTab(antennaPage, QStringLiteral("Antenna sizing"));
    tabs->addTab(laplacePage, QStringLiteral("Laplace rail"));
    m_notesTabIndex = tabs->addTab(notesPage, QStringLiteral("Notes"));
    tabs->setUsesScrollButtons(true);
    tabs->tabBar()->setElideMode(Qt::ElideRight);
    tabs->setTabToolTip(m_scientificTabIndex, QStringLiteral("Expressions, physical units, adjustable display precision and copy actions."));
    tabs->setTabToolTip(m_variablesTabIndex, QStringLiteral("Reusable variables, unit conversions and dimensional validation."));
    tabs->setTabToolTip(m_constantsTabIndex, QStringLiteral("Searchable CODATA 2022 physical constants."));
    tabs->setTabToolTip(3, QStringLiteral("Frequency and wavelength conversion in a chosen medium."));
    tabs->setTabToolTip(4, QStringLiteral("Common antenna dimensions and verified Designer handoff."));
    tabs->setTabToolTip(5, QStringLiteral("Interactive Laplace-rail physics and practice."));
    tabs->setTabToolTip(m_notesTabIndex, QStringLiteral("Global notebook or portable project-associated notes and worksheet."));
    // Notes are a working canvas: suppress the general calculator introduction
    // while the notebook tab is active to recover vertical drawing space.
    connect(tabs, &QTabWidget::currentChanged, this, [intro, this](int index) {
        intro->setVisible(index != m_notesTabIndex);
    });
    root->addWidget(tabs, 1);

    buildWaveTab(wavePage);
    buildAntennaTab(antennaPage);
    buildScientificTab(sciencePage);
    buildVariablesTab(variablesPage);
    buildConstantsTab(constantsPage);
    buildLaplaceRailTab(laplacePage);
    buildNotesTab(notesPage);
    auto *focusInput = new QShortcut(QKeySequence(QStringLiteral("Ctrl+L")), this);
    focusInput->setContext(Qt::WidgetWithChildrenShortcut);
    connect(focusInput, &QShortcut::activated, this, [this] {
        if (m_calculatorTabs) m_calculatorTabs->setCurrentIndex(m_scientificTabIndex);
        if (m_expression) { m_expression->setFocus(); m_expression->selectAll(); }
    });
}

EmCalculatorWidget::~EmCalculatorWidget()
{
    if (m_variableSaveTimer) m_variableSaveTimer->stop();
    if (m_notesSaveTimer) m_notesSaveTimer->stop();
    if (m_inkSaveTimer) m_inkSaveTimer->stop();
    saveWorksheet();
    saveNotes();
    saveInkNotes();
}

void EmCalculatorWidget::setCreateAntennaHandler(CreateAntennaHandler handler)
{
    m_createAntennaHandler = std::move(handler);
    if (m_createInDesigner) m_createInDesigner->setEnabled(m_antennaValid && static_cast<bool>(m_createAntennaHandler));
}

void EmCalculatorWidget::setActiveAntennaProjectFile(const QString &path)
{
    if (path.trimmed().isEmpty())
    {
        // A Calculator-generated replacement starts an unsaved project; do
        // not keep writing future notes into the previous antenna's sidecars.
        if (m_currentNotebookScope == 1 && (!saveNotes() || !saveWorksheet() || !saveInkNotes()))
        {
            QMessageBox::warning(this, QStringLiteral("Project notes"),
                QStringLiteral("The previous project's notes could not be saved. "
                               "Association was preserved to avoid data loss."));
            return;
        }
        m_activeAntennaProjectFile.clear();
        if (m_currentNotebookScope == 1)
        {
            m_currentNotebookScope = 0;
            if (m_notebookScope)
            {
                const QSignalBlocker blocked(m_notebookScope);
                m_notebookScope->setCurrentIndex(0);
            }
            loadNotes();
            loadWorksheet();
            loadInkNotes();
        }
        if (m_notebookScope)
            m_notebookScope->setItemText(1, QStringLiteral("Project (load or save .qta first)"));
        updateScopeDescription();
        return;
    }
    const QString absolute = QFileInfo(path).absoluteFilePath();
    if (absolute == m_activeAntennaProjectFile) return;
    // Commit the previous project's work before adopting the new path.
    if (m_currentNotebookScope == 1 && (!saveNotes() || !saveWorksheet() || !saveInkNotes()))
    {
        QMessageBox::warning(this, QStringLiteral("Project notes"),
            QStringLiteral("Unable to save the previous project notebook or worksheet. "
                           "Its association was preserved."));
        return;
    }
    const QString previous = m_activeAntennaProjectFile;
    m_activeAntennaProjectFile = absolute;
    if (m_currentNotebookScope == 1 && (!loadNotes() || !loadWorksheet() || !loadInkNotes()))
    {
        m_activeAntennaProjectFile = previous;
        loadNotes();
        loadWorksheet();
        loadInkNotes();
        QMessageBox::warning(this, QStringLiteral("Project notes"),
            QStringLiteral("Cannot read the selected project's notebook or worksheet. "
                           "The previous notebook remains active."));
        return;
    }
    if (m_notebookScope) m_notebookScope->setItemText(1,
        QStringLiteral("Project: %1").arg(QFileInfo(absolute).fileName()));
    updateScopeDescription();
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
    auto *buttonLayout = new QGridLayout(buttons);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    auto *fromFrequency = new QPushButton(QStringLiteral("Frequency -> wavelength"), buttons);
    auto *fromWavelength = new QPushButton(QStringLiteral("Wavelength -> frequency"), buttons);
    auto *copyFrequency = new QPushButton(QStringLiteral("Copy f (Hz)"), buttons);
    auto *copyWavelength = new QPushButton(QStringLiteral("Copy λ (m)"), buttons);
    auto *copy = new QPushButton(QStringLiteral("Copy full result"), buttons);
    m_waveCopyFrequency = copyFrequency;
    m_waveCopyWavelength = copyWavelength;
    m_waveCopyResult = copy;
    buttonLayout->addWidget(fromFrequency, 0, 0, 1, 2);
    buttonLayout->addWidget(fromWavelength, 0, 2, 1, 2);
    buttonLayout->addWidget(copyFrequency, 1, 0);
    buttonLayout->addWidget(copyWavelength, 1, 1);
    buttonLayout->addWidget(copy, 1, 2, 1, 2);
    fromFrequency->setToolTip(QStringLiteral("Use the entered frequency and medium to compute lambda, T, omega and k."));
    fromWavelength->setToolTip(QStringLiteral("Use the entered wavelength and medium to compute frequency and derived wave properties."));
    form->addRow(buttons);
    root->addWidget(inputs);

    m_waveResult = new QPlainTextEdit(page);
    m_waveResult->setReadOnly(true);
    m_waveResult->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_waveResult->setPlaceholderText(QStringLiteral("Calculated wavelength/frequency, period, angular frequency and wave number will appear here."));
    root->addWidget(m_waveResult, 1);

    connect(fromFrequency, &QPushButton::clicked, this, [this] { calculateWaveFromFrequency(); });
    connect(fromWavelength, &QPushButton::clicked, this, [this] { calculateWaveFromWavelength(); });
    connect(copyFrequency, &QPushButton::clicked, this, [this] {
        if (m_waveValid) copyText(precise(m_validatedWaveResult.frequencyHz));
    });
    connect(copyWavelength, &QPushButton::clicked, this, [this] {
        if (m_waveValid) copyText(precise(m_validatedWaveResult.wavelengthM));
    });
    connect(copy, &QPushButton::clicked, this, [this] {
        if (m_waveValid) copyText(m_waveResult->toPlainText());
    });
    const auto waveChanged = [this] { invalidateWaveCalculation(); };
    for (auto *input : {m_waveFrequency, m_waveLength, m_waveEr, m_waveMur})
        connect(input, qOverload<double>(&QDoubleSpinBox::valueChanged), this, waveChanged);
    for (auto *unit : {m_waveFrequencyUnit, m_waveLengthUnit})
        connect(unit, qOverload<int>(&QComboBox::currentIndexChanged), this, waveChanged);
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
    auto *buttonLayout = new QGridLayout(buttons);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    auto *calculate = new QPushButton(QStringLiteral("Calculate antenna dimensions"), buttons);
    auto *copyFrequency = new QPushButton(QStringLiteral("Copy f (Hz)"), buttons);
    auto *copyPrimary = new QPushButton(QStringLiteral("Copy length (m)"), buttons);
    auto *copy = new QPushButton(QStringLiteral("Copy result"), buttons);
    m_createInDesigner = new QPushButton(QStringLiteral("Create in Antenna designer"), buttons);
    m_antennaCopyFrequency = copyFrequency;
    m_antennaCopyPrimary = copyPrimary;
    m_antennaCopyResult = copy;
    m_createInDesigner->setObjectName(QStringLiteral("emCreateAntennaButton"));
    m_createInDesigner->setEnabled(false);
    m_createInDesigner->setToolTip(QStringLiteral("Build the calculated starting geometry in Antenna designer and switch to that workspace. Existing antenna geometry is replaced, with undo history preserved."));
    buttonLayout->addWidget(calculate, 0, 0, 1, 2);
    buttonLayout->addWidget(m_createInDesigner, 0, 2, 1, 2);
    buttonLayout->addWidget(copyFrequency, 1, 0);
    buttonLayout->addWidget(copyPrimary, 1, 1);
    buttonLayout->addWidget(copy, 1, 2, 1, 2);
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
    connect(copyFrequency, &QPushButton::clicked, this, [this] {
        if (m_antennaValid) copyText(precise(m_validatedAntennaResult.frequencyHz));
    });
    connect(copyPrimary, &QPushButton::clicked, this, [this] {
        if (m_antennaValid) copyText(precise(m_validatedAntennaResult.primaryDimensionM));
    });
    connect(copy, &QPushButton::clicked, this, [this] {
        if (m_antennaValid) copyText(m_antennaResult->toPlainText());
    });
    const auto antennaChanged = [this] { invalidateAntennaCalculation(); };
    for (auto *input : {m_antennaFrequency, m_primaryDimension, m_velocityFactor,
                        m_patchEr, m_patchHeight, m_dishDiameter, m_dishEfficiency,
                        m_focalRatio, m_desiredGain})
        connect(input, qOverload<double>(&QDoubleSpinBox::valueChanged), this, antennaChanged);
    for (auto *unit : {m_antennaFrequencyUnit, m_primaryDimensionUnit,
                       m_patchHeightUnit, m_dishDiameterUnit})
        connect(unit, qOverload<int>(&QComboBox::currentIndexChanged), this, antennaChanged);
    connect(m_antennaType, qOverload<int>(&QComboBox::currentIndexChanged), this, antennaChanged);
    connect(m_antennaSolveMode, qOverload<int>(&QComboBox::currentIndexChanged), this, antennaChanged);

    updateAntennaUi();
    calculateAntenna();
}

void EmCalculatorWidget::buildScientificTab(QWidget *page)
{
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget(scroll);
    auto *root = new QVBoxLayout(content);
    root->setSpacing(9);
    scroll->setWidget(content);
    outer->addWidget(scroll);
    auto *formulaBox = new QGroupBox(QStringLiteral("Scientific expression — SI units / worksheet variables"), page);
    auto *form = new QVBoxLayout(formulaBox);

    auto *templateRow = new QWidget(formulaBox);
    auto *templateLayout = new QHBoxLayout(templateRow);
    templateLayout->setContentsMargins(0, 0, 0, 0);
    templateLayout->addWidget(new QLabel(QStringLiteral("Formula"), templateRow));
    m_formulaTemplate = new QComboBox(templateRow);
    m_formulaTemplate->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    for (const auto &entry : kFormulaTemplates) m_formulaTemplate->addItem(QString::fromLatin1(entry.name));
    templateLayout->addWidget(m_formulaTemplate, 1);
    form->addWidget(templateRow);

    m_formulaDescription = new QLabel(formulaBox);
    m_formulaDescription->setWordWrap(true);
    m_formulaDescription->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addWidget(m_formulaDescription);

    m_expression = new QLineEdit(formulaBox);
    m_expression->setObjectName(QStringLiteral("emScientificExpression"));
    m_expression->setPlaceholderText(QStringLiteral("e.g. ke*q/r^2  or  2 nC / (10 cm)^2"));
    m_expression->setToolTip(QStringLiteral("Enter or click Calculate. SI units are automatic for quantities (2 nC, 10 cm). Define reusable variables in Variables & units; use ans for the previous result."));
    m_expression->setClearButtonEnabled(true);
    QFont inputFont = m_expression->font();
    if (inputFont.pointSize() > 0) inputFont.setPointSize(inputFont.pointSize() + 2);
    m_expression->setFont(inputFont);
    form->addWidget(m_expression);

    auto *palette = new QToolButton(formulaBox);
    palette->setText(QStringLiteral("Functions, constants and units  ▸"));
    palette->setCheckable(true);
    palette->setChecked(false);
    palette->setToolButtonStyle(Qt::ToolButtonTextOnly);
    palette->setObjectName(QStringLiteral("emFunctionPaletteToggle"));
    palette->setToolTip(QStringLiteral("Expand to insert math functions, EM constants and common units."));
    form->addWidget(palette, 0, Qt::AlignLeft);
    auto *quick = new QWidget(formulaBox);
    quick->setObjectName(QStringLiteral("emFunctionPalette"));
    auto *paletteLayout = new QVBoxLayout(quick);
    paletteLayout->setContentsMargins(2, 2, 2, 2);
    const struct TokenGroup { const char *title; QStringList tokens; } tokenGroups[] = {
        {"Math", {QStringLiteral("("), QStringLiteral(")"), QStringLiteral("^"), QStringLiteral("ans"),
                    QStringLiteral("sqrt("), QStringLiteral("exp("), QStringLiteral("ln("), QStringLiteral("log10("),
                    QStringLiteral("sin("), QStringLiteral("cos("), QStringLiteral("sind("), QStringLiteral("pow(")}},
        {"Physical constants", {QStringLiteral("eps0"), QStringLiteral("mu0"), QStringLiteral("ke"),
                    QStringLiteral("pi"), QStringLiteral("c0"), QStringLiteral("eta0"),
                    QStringLiteral("kb"), QStringLiteral("e")}},
        {"Units", {QStringLiteral(" nC"), QStringLiteral(" cm"), QStringLiteral(" MHz"), QStringLiteral(" V"),
                    QStringLiteral(" A"), QStringLiteral(" T"), QStringLiteral(" m/s"), QStringLiteral(" Ohm")}}
    };
    for (const auto &group : tokenGroups)
    {
        auto *groupBox = new QGroupBox(QString::fromLatin1(group.title), quick);
        auto *grid = new QGridLayout(groupBox);
        grid->setSpacing(5);
        for (int i = 0; i < group.tokens.size(); ++i)
        {
            const QString token = group.tokens[i];
            auto *button = new QPushButton(token, groupBox);
            button->setMinimumHeight(30);
            button->setToolTip(QStringLiteral("Insert %1 at cursor").arg(token));
            grid->addWidget(button, i / 4, i % 4);
            connect(button, &QPushButton::clicked, this, [this, token] { insertExpressionToken(token); });
        }
        paletteLayout->addWidget(groupBox);
    }
    quick->setVisible(false);
    connect(palette, &QToolButton::toggled, this, [palette, quick](bool visible) {
        quick->setVisible(visible);
        palette->setText(visible ? QStringLiteral("Functions, constants and units  ▾")
                                 : QStringLiteral("Functions, constants and units  ▸"));
    });
    form->addWidget(quick);

    auto *actionRow = new QWidget(formulaBox);
    auto *actions = new QGridLayout(actionRow);
    actions->setContentsMargins(0, 0, 0, 0);
    auto *evaluate = new QPushButton(QStringLiteral("Calculate  ↵"), actionRow);
    evaluate->setObjectName(QStringLiteral("emCalculateButton"));
    evaluate->setDefault(false); // do not take Enter away from the expression field
    auto *copyResult = new QPushButton(QStringLiteral("Copy SI value"), actionRow);
    auto *copyWithUnit = new QPushButton(QStringLiteral("Copy displayed value + unit"), actionRow);
    auto *copyExpression = new QPushButton(QStringLiteral("Copy expression"), actionRow);
    m_scienceCopyValue = copyResult;
    m_scienceCopyUnit = copyWithUnit;
    m_scienceCopyExpression = copyExpression;
    auto *addToNotes = new QPushButton(QStringLiteral("Send to Notes"), actionRow);
    m_scienceSendNotes = addToNotes;
    auto *showConstants = new QPushButton(QStringLiteral("Browse all constants →"), actionRow);
    auto *showVariables = new QPushButton(QStringLiteral("Variables & units →"), actionRow);
    actions->addWidget(evaluate, 0, 0);
    actions->addWidget(copyResult, 0, 1);
    actions->addWidget(copyWithUnit, 0, 2);
    actions->addWidget(copyExpression, 1, 0);
    actions->addWidget(addToNotes, 1, 1);
    actions->addWidget(showConstants, 2, 0);
    actions->addWidget(showVariables, 2, 1);
    form->addWidget(actionRow);

    auto *answerRow = new QWidget(formulaBox);
    auto *answerLayout = new QHBoxLayout(answerRow);
    answerLayout->setContentsMargins(0, 0, 0, 0);
    answerLayout->addWidget(new QLabel(QStringLiteral("Result"), answerRow));
    m_expressionResult = new QLineEdit(answerRow);
    m_expressionResult->setObjectName(QStringLiteral("emScientificResult"));
    m_expressionResult->setReadOnly(true);
    m_expressionResult->setPlaceholderText(QStringLiteral("Press Enter or Calculate"));
    m_expressionResult->setFont(inputFont);
    answerLayout->addWidget(m_expressionResult, 1);
    m_expressionUnit = new QLabel(QStringLiteral("1"), answerRow);
    m_expressionUnit->setObjectName(QStringLiteral("emScientificDerivedUnit"));
    m_expressionUnit->setToolTip(QStringLiteral("Dimensional unit inferred from declared quantities; plain numeric literals are dimensionless."));
    answerLayout->addWidget(m_expressionUnit);
    form->addWidget(answerRow);
    auto *formatRow = new QWidget(formulaBox);
    auto *formatLayout = new QHBoxLayout(formatRow);
    formatLayout->setContentsMargins(0, 0, 0, 0);
    m_autoPrefix = new QCheckBox(QStringLiteral("Automatic SI prefixes"), formatRow);
    m_autoPrefix->setObjectName(QStringLiteral("emAutoPrefix"));
    m_autoPrefix->setToolTip(QStringLiteral("Display 1797 V/m as 1.797 kV/m without changing the exact SI value."));
    formatLayout->addWidget(m_autoPrefix);
    formatLayout->addWidget(new QLabel(QStringLiteral("Significant digits"), formatRow));
    m_significantDigits = new QSpinBox(formatRow);
    m_significantDigits->setObjectName(QStringLiteral("emSignificantDigits"));
    m_significantDigits->setRange(3, 12);
    formatLayout->addWidget(m_significantDigits);
    formatLayout->addStretch(1);
    form->addWidget(formatRow);
    QSettings displaySettings;
    m_autoPrefix->setChecked(displaySettings.value(QStringLiteral("calculator/display/autoPrefix"), true).toBool());
    m_significantDigits->setValue(displaySettings.value(QStringLiteral("calculator/display/significantDigits"), 6).toInt());
    connect(m_autoPrefix, &QCheckBox::toggled, this, [this](bool value) {
        QSettings().setValue(QStringLiteral("calculator/display/autoPrefix"), value);
        refreshScientificDisplay();
    });
    connect(m_significantDigits, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        QSettings().setValue(QStringLiteral("calculator/display/significantDigits"), value);
        refreshScientificDisplay();
    });
    m_expressionStatus = new QLabel(QStringLiteral("Ready — Enter to calculate · Ctrl+L to focus · 'ans' for the previous result."), formulaBox);
    m_expressionStatus->setWordWrap(true);
    form->addWidget(m_expressionStatus);
    root->addWidget(formulaBox);

    auto *historyBox = new QGroupBox(QStringLiteral("Calculation history — double-click to reuse"), page);
    auto *historyLayout = new QVBoxLayout(historyBox);
    m_historyTable = new QTableWidget(0, 2, historyBox);
    m_historyTable->setObjectName(QStringLiteral("emCalculationHistory"));
    m_historyTable->setHorizontalHeaderLabels({QStringLiteral("Expression"), QStringLiteral("Result")});
    m_historyTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_historyTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_historyTable->verticalHeader()->setVisible(false);
    m_historyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_historyTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_historyTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_historyTable->setMinimumHeight(110);
    historyLayout->addWidget(m_historyTable);
    auto *clearHistory = new QPushButton(QStringLiteral("Clear history"), historyBox);
    historyLayout->addWidget(clearHistory, 0, Qt::AlignRight);
    root->addWidget(historyBox, 1);
    auto *help = new QLabel(QStringLiteral(
        "Values: 2 nC, 10 cm, 144 MHz, 3 m/s (converted to SI); named variables from Variables & units. "
        "Addition/subtraction must respect dimensions. "
        "Syntax: + − * / ^ and parentheses; 1e-9 = 10⁻⁹. sin/cos/tan use radians, sind/cosd/tand use degrees. "
        "Functions: sqrt, exp, ln, log10, abs, asin, acos, atan, pow(a,b), min/max, deg2rad/rad2deg. "
        "Constants can be inserted from the Physical constants tab; 'k' means Coulomb's constant, 'kb' means Boltzmann's constant."), page);
    help->setWordWrap(true);
    help->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(help);

    connect(m_formulaTemplate, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) { loadFormulaTemplate(index); });
    connect(evaluate, &QPushButton::clicked, this, [this] { evaluateScientificExpression(); });
    connect(m_expression, &QLineEdit::returnPressed, this, [this] { evaluateScientificExpression(); });
    connect(copyResult, &QPushButton::clicked, this, [this] { if (m_scientificValid) copyText(scientificExactText()); });
    connect(copyWithUnit, &QPushButton::clicked, this, [this] { if (m_scientificValid) copyText(scientificDisplayText()); });
    connect(copyExpression, &QPushButton::clicked, this, [this] { if (m_scientificValid) copyText(m_validatedScientificExpression); });
    connect(addToNotes, &QPushButton::clicked, this, [this] {
        if (!m_notesEditor || !m_scientificValid) return;
        m_notesEditor->appendPlainText(m_validatedScientificExpression + QStringLiteral(" = ")
            + scientificDisplayText() + QStringLiteral(" [SI: ") + scientificExactText() + QStringLiteral("]"));
        if (m_calculatorTabs) m_calculatorTabs->setCurrentIndex(m_notesTabIndex);
        m_expressionStatus->setText(QStringLiteral("Result copied to Notes."));
    });
    connect(showConstants, &QPushButton::clicked, this, [this] {
        if (m_calculatorTabs) m_calculatorTabs->setCurrentIndex(m_constantsTabIndex);
    });
    connect(showVariables, &QPushButton::clicked, this, [this] {
        if (m_calculatorTabs) m_calculatorTabs->setCurrentIndex(m_variablesTabIndex);
    });
    connect(clearHistory, &QPushButton::clicked, this, [this] { if (m_historyTable) m_historyTable->setRowCount(0); });
    connect(m_historyTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (auto *item = m_historyTable->item(row, 0)) {
            m_expression->setText(item->text());
            m_expression->setFocus();
        }
    });
    connect(m_expression, &QLineEdit::textChanged, this, [this] {
        invalidateScientificDisplay();
        m_expressionStatus->setText(QStringLiteral("Modified — press Enter to calculate."));
    });

    loadFormulaTemplate(2);
    evaluateScientificExpression();
}

void EmCalculatorWidget::buildVariablesTab(QWidget *page)
{
    auto *root = new QVBoxLayout(page);
    root->setSpacing(8);
    auto *intro = new QLabel(QStringLiteral(
        "One definition per line, evaluated top to bottom. SI conversion is automatic; units are checked "
        "during addition, subtraction and functions. Unit symbols (such as m, F, V) are reserved; "
        "use names such as force instead of F. Variables are case-sensitive (E differs from e). "
        "Press Calculate to use these variables in the EM calculator."), page);
    intro->setWordWrap(true);
    intro->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(intro);
    m_variableScopeLabel = new QLabel(QStringLiteral("Worksheet scope: Global. Change it in Notes."), page);
    m_variableScopeLabel->setWordWrap(true);
    m_variableScopeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_variableScopeLabel);

    m_variableWorksheet = new QPlainTextEdit(page);
    m_variableWorksheet->setObjectName(QStringLiteral("emVariablesWorksheet"));
    m_variableWorksheet->setPlaceholderText(QStringLiteral("q = 2 nC\nr = 10 cm\nE = ke*q/r^2\nforce = q*E"));
    m_variableWorksheet->setMinimumHeight(125);
    QFont mono = m_variableWorksheet->font();
    mono.setStyleHint(QFont::Monospace);
    m_variableWorksheet->setFont(mono);
    root->addWidget(m_variableWorksheet, 1);

    auto *actions = new QWidget(page);
    auto *layout = new QGridLayout(actions);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *run = new QPushButton(QStringLiteral("Calculate / update variables"), actions);
    run->setObjectName(QStringLiteral("emEvaluateVariablesButton"));
    auto *example = new QPushButton(QStringLiteral("Load example"), actions);
    auto *copy = new QPushButton(QStringLiteral("Copy results"), actions);
    auto *send = new QPushButton(QStringLiteral("Send to Notes"), actions);
    layout->addWidget(run, 0, 0);
    layout->addWidget(example, 0, 1);
    layout->addWidget(copy, 1, 0);
    layout->addWidget(send, 1, 1);
    root->addWidget(actions);

    m_variableStatus = new QLabel(page);
    m_variableStatus->setWordWrap(true);
    m_variableStatus->setObjectName(QStringLiteral("emVariablesStatus"));
    root->addWidget(m_variableStatus);

    m_variableResults = new QTableWidget(0, 4, page);
    m_variableResults->setObjectName(QStringLiteral("emVariablesResultTable"));
    m_variableResults->setHorizontalHeaderLabels({QStringLiteral("Variable"), QStringLiteral("SI value"),
        QStringLiteral("Unit"), QStringLiteral("Expression")});
    m_variableResults->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_variableResults->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_variableResults->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_variableResults->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_variableResults->verticalHeader()->setVisible(false);
    m_variableResults->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_variableResults->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_variableResults->setSelectionMode(QAbstractItemView::SingleSelection);
    m_variableResults->setAlternatingRowColors(true);
    m_variableResults->setMinimumHeight(155);
    root->addWidget(m_variableResults, 1);

    auto *selectedActions = new QWidget(page);
    auto *selectedLayout = new QHBoxLayout(selectedActions);
    selectedLayout->setContentsMargins(0, 0, 0, 0);
    auto *copySelected = new QPushButton(QStringLiteral("Copy selected SI value"), selectedActions);
    auto *insertSelected = new QPushButton(QStringLiteral("Use selected variable in EM calculator"), selectedActions);
    selectedLayout->addWidget(copySelected);
    selectedLayout->addWidget(insertSelected);
    selectedLayout->addStretch();
    root->addWidget(selectedActions);

    auto *help = new QLabel(QStringLiteral(
        "Examples: 2 nC, 10 cm, 9 m^2, 3 cm^2, 3 m/s, (10 cm)^2, 2*cm. "
        "Operators: + - * / ^, sqrt, sin/sind, exp and physical constants pi, eps0, mu0, ke. "
        "Use explicit * between variables: ke*q/r^2. 9 m^2 means 9 square metres, "
        "but (9 m)^2 means 81 square metres. Unit symbols are reserved variable names. "
        "Numbers without units are dimensionless; use a decimal point, e.g. 0.5. "
        "Temperature uses K (no Celsius offset). "
        "This is a dimensional calculator, not a Maxwell PDE solver."), page);
    help->setWordWrap(true);
    help->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(help);

    m_variableSaveTimer = new QTimer(this);
    m_variableSaveTimer->setSingleShot(true);
    m_variableSaveTimer->setInterval(500);
    connect(m_variableSaveTimer, &QTimer::timeout, this, [this] { saveWorksheet(); });
    connect(m_variableWorksheet, &QPlainTextEdit::textChanged, this, [this] {
        if (m_switchingNotebook) return;
        m_userVariables.clear(); // Never reuse stale definitions after an edit.
        invalidateScientificDisplay(); // Dependent scientific result is no longer certified.
        if (m_expressionStatus) m_expressionStatus->setText(QStringLiteral("Variables modified — recalculate worksheet and scientific result."));
        if (m_variableResults) m_variableResults->setRowCount(0);
        m_variableStatus->setText(QStringLiteral("Edited: press Calculate to validate and update variables."));
        m_variableSaveTimer->start();
    });
    connect(run, &QPushButton::clicked, this, [this] { evaluateVariableWorksheet(); });
    connect(example, &QPushButton::clicked, this, [this] {
        m_variableWorksheet->setPlainText(QStringLiteral("# Electric field and force\nq = 2 nC\nr = 10 cm\nE = ke*q/r^2\nforce = q*E"));
        evaluateVariableWorksheet();
    });
    auto resultsText = [this]() {
        QStringList lines;
        for (int r = 0; r < m_variableResults->rowCount(); ++r)
        {
            QString label = m_variableResults->item(r, 0)->text();
            if (label.isEmpty()) label = QStringLiteral("Result %1").arg(r + 1);
            lines << QStringLiteral("%1 = %2 %3").arg(label,
                m_variableResults->item(r, 1)->text(), m_variableResults->item(r, 2)->text());
        }
        return lines.join(QLatin1Char('\n'));
    };
    connect(copy, &QPushButton::clicked, this, [this, resultsText] { copyText(resultsText()); });
    connect(send, &QPushButton::clicked, this, [this, resultsText] {
        if (!m_notesEditor || m_variableResults->rowCount() == 0) return;
        m_notesEditor->appendPlainText(m_variableWorksheet->toPlainText()
            + QStringLiteral("\n# Results (SI)\n") + resultsText());
        if (m_calculatorTabs) m_calculatorTabs->setCurrentIndex(m_notesTabIndex);
    });
    connect(copySelected, &QPushButton::clicked, this, [this] {
        const int r = m_variableResults->currentRow();
        if (r >= 0 && m_variableResults->item(r, 1)) copyText(m_variableResults->item(r, 1)->text());
    });
    auto insert = [this](int r) {
        if (r < 0 || !m_variableResults->item(r, 0)) return;
        const QString name = m_variableResults->item(r, 0)->text();
        if (name.isEmpty()) return;
        insertExpressionToken(name);
        if (m_calculatorTabs) m_calculatorTabs->setCurrentIndex(m_scientificTabIndex);
    };
    connect(insertSelected, &QPushButton::clicked, this, [this, insert] { insert(m_variableResults->currentRow()); });
    connect(m_variableResults, &QTableWidget::cellDoubleClicked, this, [insert](int row, int) { insert(row); });

    QSettings settings;
    QString sheet = settings.value(QStringLiteral("calculator/unitsWorksheet"),
        QStringLiteral("q = 2 nC\nr = 10 cm\nE = ke*q/r^2\nforce = q*E")).toString();
    // The built-in 6.5.0 sample used F for force. In 6.5.1 F is reserved
    // for farads, so migrate ONLY the exact historical sample. Never rewrite
    // custom user worksheets; errors explain how to rename other collisions.
    const QString legacySample = QStringLiteral("q = 2 nC\nr = 10 cm\nE = ke*q/r^2\nF = q*E");
    const QString legacyExample = QStringLiteral("# Electric field and force\n") + legacySample;
    if (sheet == legacySample || sheet == legacyExample)
    {
        sheet.replace(QStringLiteral("F = q*E"), QStringLiteral("force = q*E"));
        settings.setValue(QStringLiteral("calculator/unitsWorksheet"), sheet);
    }
    m_variableWorksheet->setPlainText(sheet);
    evaluateVariableWorksheet();
}

void EmCalculatorWidget::evaluateVariableWorksheet()
{
    if (!m_variableWorksheet || !m_variableResults) return;
    invalidateScientificDisplay();
    if (m_expressionStatus) m_expressionStatus->setText(QStringLiteral("Worksheet recalculated — press Enter to update the scientific result."));
    const EmCalculator::WorksheetResult result =
        EmCalculator::evaluateWorksheet(m_variableWorksheet->toPlainText().toStdString());
    m_userVariables.clear();
    m_variableResults->setRowCount(0);
    for (const auto &entry : result.rows)
    {
        const int row = m_variableResults->rowCount();
        m_variableResults->insertRow(row);
        m_variableResults->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(entry.name)));
        m_variableResults->setItem(row, 1, new QTableWidgetItem(precise(entry.quantity.siValue)));
        m_variableResults->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(EmCalculator::quantityUnit(entry.quantity))));
        m_variableResults->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(entry.expression)));
    }
    if (!result.ok)
    {
        m_variableResults->setRowCount(0); // Do not expose partially computed rows as validated results.
        m_variableStatus->setText(QStringLiteral("Error on line %1, character %2: %3 — worksheet variables not applied.")
            .arg(result.errorLine).arg(result.errorPosition + 1).arg(QString::fromStdString(result.error)));
        return;
    }
    m_userVariables = result.variables;
    m_variableStatus->setText(QStringLiteral("%1 calculation(s), %2 variable(s) ready in EM calculator. SI conversion and unit checking: OK.")
        .arg(static_cast<qulonglong>(result.rows.size())).arg(static_cast<qulonglong>(result.variables.size())));
}

void EmCalculatorWidget::buildConstantsTab(QWidget *page)
{
    auto *root = new QVBoxLayout(page);
    root->setSpacing(8);
    auto *lead = new QLabel(QStringLiteral(
        "Physical constants — CODATA 2022 (NIST). Search by name, symbol, unit or alias; "
        "double-click to insert a constant into the calculator. Values are in coherent SI units."), page);
    lead->setWordWrap(true);
    lead->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(lead);

    auto *searchRow = new QWidget(page);
    auto *rowLayout = new QHBoxLayout(searchRow);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    m_constantSearch = new QLineEdit(searchRow);
    m_constantSearch->setObjectName(QStringLiteral("emConstantsSearch"));
    m_constantSearch->setPlaceholderText(QStringLiteral("Search: permittivity, eps0, F/m, Coulomb, Boltzmann…"));
    m_constantSearch->setClearButtonEnabled(true);
    m_constantCategory = new QComboBox(searchRow);
    m_constantCategory->addItems({QStringLiteral("All fields"), QStringLiteral("Electromagnetism"),
        QStringLiteral("Mathematics"), QStringLiteral("Quantum"), QStringLiteral("Thermodynamics"),
        QStringLiteral("Mechanics"), QStringLiteral("Engineering")});
    rowLayout->addWidget(m_constantSearch, 2);
    rowLayout->addWidget(m_constantCategory, 1);
    root->addWidget(searchRow);

    m_constantsTable = new QTableWidget(page);
    m_constantsTable->setObjectName(QStringLiteral("emPhysicalConstantsTable"));
    m_constantsTable->setColumnCount(5);
    m_constantsTable->setHorizontalHeaderLabels({QStringLiteral("Symbol"), QStringLiteral("SI value"),
        QStringLiteral("Unit"), QStringLiteral("Meaning"), QStringLiteral("Category")});
    const auto &constants = EmCalculator::physicalConstants();
    m_constantsTable->setRowCount(static_cast<int>(constants.size()));
    for (int row = 0; row < m_constantsTable->rowCount(); ++row)
    {
        const auto &c = constants[static_cast<std::size_t>(row)];
        m_constantsTable->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(c.symbol)));
        m_constantsTable->setItem(row, 1, new QTableWidgetItem(precise(c.value)));
        m_constantsTable->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(c.unit)));
        m_constantsTable->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(c.name)));
        m_constantsTable->setItem(row, 4, new QTableWidgetItem(QString::fromStdString(c.category)));
        for (int col = 0; col < 5; ++col) m_constantsTable->item(row, col)->setToolTip(QString::fromStdString(c.description));
    }
    m_constantsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_constantsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_constantsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_constantsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_constantsTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_constantsTable->verticalHeader()->setVisible(false);
    m_constantsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_constantsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_constantsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_constantsTable->setAlternatingRowColors(true);
    root->addWidget(m_constantsTable, 1);

    auto *buttons = new QWidget(page);
    auto *buttonLayout = new QHBoxLayout(buttons);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    auto *insert = new QPushButton(QStringLiteral("Insert in calculator"), buttons);
    auto *copySymbol = new QPushButton(QStringLiteral("Copy symbol"), buttons);
    auto *copyValue = new QPushButton(QStringLiteral("Copy SI value"), buttons);
    m_constantsCount = new QLabel(buttons);
    buttonLayout->addWidget(insert);
    buttonLayout->addWidget(copySymbol);
    buttonLayout->addWidget(copyValue);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(m_constantsCount);
    root->addWidget(buttons);

    m_constantDetails = new QLabel(page);
    m_constantDetails->setWordWrap(true);
    m_constantDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_constantDetails->setMinimumHeight(50);
    root->addWidget(m_constantDetails);
    auto *source = new QLabel(QStringLiteral(
        "Reference: NIST/CODATA 2022 • eps0 and mu0 have measured uncertainties after the 2019 SI redefinition. "
        "'sigma_cu' is a typical material value, not a fundamental constant."), page);
    source->setWordWrap(true);
    source->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(source);

    auto selectedRow = [this]() { return m_constantsTable ? m_constantsTable->currentRow() : -1; };
    auto insertSelected = [this, selectedRow]() {
        const int row = selectedRow();
        if (row < 0 || m_constantsTable->isRowHidden(row)) return;
        insertExpressionToken(m_constantsTable->item(row, 0)->text());
        if (m_calculatorTabs) m_calculatorTabs->setCurrentIndex(m_scientificTabIndex);
        m_expression->setFocus();
    };
    connect(insert, &QPushButton::clicked, this, insertSelected);
    connect(copySymbol, &QPushButton::clicked, this, [this, selectedRow] {
        const int row = selectedRow();
        if (row >= 0 && !m_constantsTable->isRowHidden(row)) copyText(m_constantsTable->item(row, 0)->text());
    });
    connect(copyValue, &QPushButton::clicked, this, [this, selectedRow] {
        const int row = selectedRow();
        if (row >= 0 && !m_constantsTable->isRowHidden(row)) copyText(m_constantsTable->item(row, 1)->text());
    });
    connect(m_constantsTable, &QTableWidget::cellDoubleClicked, this, [insertSelected](int, int) { insertSelected(); });
    connect(m_constantsTable, &QTableWidget::currentCellChanged, this,
        [this](int row, int, int, int) { updateConstantDetails(row); });
    connect(m_constantSearch, &QLineEdit::textChanged, this, [this] { updateConstantsFilter(); });
    connect(m_constantCategory, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updateConstantsFilter(); });
    m_constantsTable->selectRow(2); // eps0 is the first EM constant in the registry
    updateConstantsFilter();
}

void EmCalculatorWidget::updateConstantDetails(int row)
{
    if (!m_constantDetails) return;
    const auto &values = EmCalculator::physicalConstants();
    if (row < 0 || static_cast<std::size_t>(row) >= values.size() || m_constantsTable->isRowHidden(row))
    {
        m_constantDetails->setText(QStringLiteral("Select a constant to see aliases, units and assumptions."));
        return;
    }
    const auto &c = values[static_cast<std::size_t>(row)];
    m_constantDetails->setText(QStringLiteral("%1 — %2 [%3]\nAliases: %4\n%5")
        .arg(QString::fromStdString(c.symbol), QString::fromStdString(c.name),
             QString::fromStdString(c.unit.empty() ? std::string("dimensionless") : c.unit),
             QString::fromStdString(c.aliases.empty() ? std::string("none") : c.aliases),
             QString::fromStdString(c.description)));
}

void EmCalculatorWidget::updateConstantsFilter()
{
    if (!m_constantsTable || !m_constantSearch || !m_constantCategory) return;
    const QString query = m_constantSearch->text().trimmed();
    const QString category = m_constantCategory->currentText();
    const auto &data = EmCalculator::physicalConstants();
    int visible = 0;
    int first = -1;
    for (int row = 0; row < m_constantsTable->rowCount(); ++row)
    {
        const auto &c = data[static_cast<std::size_t>(row)];
        const QString searchable = QStringLiteral("%1 %2 %3 %4 %5 %6")
            .arg(QString::fromStdString(c.symbol), QString::fromStdString(c.name),
                 QString::fromStdString(c.unit), QString::fromStdString(c.aliases),
                 QString::fromStdString(c.description), QString::fromStdString(c.category));
        const bool matches = (category == QLatin1String("All fields") || category == QString::fromStdString(c.category)) &&
                             (query.isEmpty() || searchable.contains(query, Qt::CaseInsensitive));
        m_constantsTable->setRowHidden(row, !matches);
        if (matches) { ++visible; if (first < 0) first = row; }
    }
    if (m_constantsCount) m_constantsCount->setText(QStringLiteral("%1 / %2 constants").arg(visible).arg(static_cast<int>(data.size())));
    if (first >= 0 && (m_constantsTable->currentRow() < 0 || m_constantsTable->isRowHidden(m_constantsTable->currentRow())))
        m_constantsTable->selectRow(first);
    updateConstantDetails(m_constantsTable->currentRow());
}

void EmCalculatorWidget::buildLaplaceRailTab(QWidget *page)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new LaplaceRailWidget(page), 1);
}

void EmCalculatorWidget::buildNotesTab(QWidget *page)
{
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(5, 4, 5, 4);
    root->setSpacing(5);

    auto *scopeRow = new QWidget(page);
    auto *scopeLayout = new QHBoxLayout(scopeRow);
    scopeLayout->setContentsMargins(0, 0, 0, 0);
    scopeLayout->addWidget(new QLabel(QStringLiteral("Notebook"), scopeRow));
    m_notebookScope = new QComboBox(scopeRow);
    m_notebookScope->setObjectName(QStringLiteral("emNotebookScope"));
    m_notebookScope->addItem(QStringLiteral("Global (this computer)"), 0);
    m_notebookScope->addItem(QStringLiteral("Project (load or save .qta first)"), 1);
    m_notebookScope->setToolTip(QStringLiteral("Global or project-associated notes. Project mode stores sidecars beside the .qta file."));
    m_notebookScope->setMaximumWidth(350);
    scopeLayout->addWidget(m_notebookScope);
    m_projectNotesLabel = new QLabel(QStringLiteral("Global notes · no .qta file linked"), scopeRow);
    m_projectNotesLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_projectNotesLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    scopeLayout->addWidget(m_projectNotesLabel, 1);
    root->addWidget(scopeRow);

    auto *noteModes = new QTabWidget(page);
    noteModes->setObjectName(QStringLiteral("notebookModeTabs"));
    auto *textPage = new QWidget(noteModes);
    auto *textLayout = new QVBoxLayout(textPage);
    noteModes->addTab(textPage, QStringLiteral("Text"));
    auto *inkPage = new QWidget(noteModes);
    auto *inkLayout = new QVBoxLayout(inkPage);
    inkLayout->setContentsMargins(4, 3, 4, 3);
    inkLayout->setSpacing(4);
    noteModes->addTab(inkPage, QStringLiteral("Drawing (pen)"));
    root->addWidget(noteModes, 1);

    auto *toolbar = new QWidget(textPage);
    auto *buttons = new QGridLayout(toolbar);
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(6);
    auto addButton = [&](int row, int col, const QString &text, const QString &tip, auto action) {
        auto *button = new QPushButton(text, toolbar);
        button->setToolTip(tip);
        connect(button, &QPushButton::clicked, this, action);
        buttons->addWidget(button, row, col);
        return button;
    };
    addButton(0, 0, QStringLiteral("Open file"), QStringLiteral("Import with confirmation and backup."), [this] { openNotesFile(); });
    addButton(0, 1, QStringLiteral("Restore backup"), QStringLiteral("Restore last imported notebook snapshot for this scope."), [this] { restorePreviousNotes(); });
    addButton(0, 2, QStringLiteral("Export .txt / .md"), QStringLiteral("Export a portable text file."), [this] { saveNotesFile(); });
    addButton(1, 0, QStringLiteral("Undo"), QStringLiteral("Undo (Ctrl+Z)."), [this] { if (m_notesEditor) m_notesEditor->undo(); });
    addButton(1, 1, QStringLiteral("Redo"), QStringLiteral("Redo (Ctrl+Y)."), [this] { if (m_notesEditor) m_notesEditor->redo(); });
    addButton(1, 2, QStringLiteral("Copy"), QStringLiteral("Copy selection (Ctrl+C)."), [this] { if (m_notesEditor) m_notesEditor->copy(); });
    addButton(2, 0, QStringLiteral("Paste"), QStringLiteral("Paste (Ctrl+V)."), [this] { if (m_notesEditor) m_notesEditor->paste(); });
    addButton(2, 1, QStringLiteral("Select all"), QStringLiteral("Select all (Ctrl+A)."), [this] { if (m_notesEditor) m_notesEditor->selectAll(); });
    textLayout->addWidget(toolbar);
    connect(m_notebookScope, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) { changeNotebookScope(index); });

    auto *hint = new QLabel(QStringLiteral(
        "Text notebook: standard keyboard shortcuts, selection and copy/paste. "
        "Choose Global or a project notebook. Project mode creates portable *.qta.notes.md, *.qta.notes.ink.json and *.qta.worksheet.txt "
        "sidecars without changing the .qta schema. Import, export, undo and clipboard shortcuts remain available."), page);
    hint->setWordWrap(true);
    textLayout->addWidget(hint);

    m_notesEditor = new QPlainTextEdit(textPage);
    m_notesEditor->setObjectName(QStringLiteral("emNotesEditor"));
    m_notesEditor->setPlaceholderText(QStringLiteral("Write calculations, reminders, dimensions, observations, test notes, equations, etc. here..."));
    m_notesEditor->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_notesEditor->setTabChangesFocus(false);
    textLayout->addWidget(m_notesEditor, 1);

    m_notesStatus = new QLabel(textPage);
    textLayout->addWidget(m_notesStatus);

    // Guidance is still available via a compact Help control, but the long
    // explanatory paragraph must not consume the notebook's drawing viewport.
    const QString drawingHelp = QStringLiteral(
        "Pen: draw with a stylus or mouse; Text: click to enter an annotation.\n"
        "Select / move: marquee on empty paper, Shift-click to extend selection, drag to move.\n"
        "Line, Arrow, Rectangle, Ellipse: drag to create; Shift forces a square/circle.\n"
        "Electronic symbol: choose a part in Details / symbols, then click the page.\n"
        "Smart wire: connect a symbol terminal to another (graphic links, not SPICE).\n"
        "Insert image / Paste image: annotate screenshots. Shift when resizing an image: stretch.\n"
        "Hand or middle mouse: pan. Ctrl+wheel or pinch: zoom.\n"
        "Pages navigate between notebook sheets; Expand opens a maximized editor.\n"
        "Single-finger/palm touch does not draw ink.");
    auto *inkToolbar = new QWidget(inkPage);
    auto *inkTools = new QGridLayout(inkToolbar);
    inkTools->setContentsMargins(0, 0, 0, 0);
    inkTools->setHorizontalSpacing(6);
    inkTools->setVerticalSpacing(3);
    // Secondary actions live in a collapsible panel. Default to a two/three-row
    // toolbar so the drawing surface takes the remaining available height.
    auto *advancedPanel = new QWidget(inkPage);
    advancedPanel->setObjectName(QStringLiteral("notesDrawingAdvancedPanel"));
    auto *advancedTools = new QGridLayout(advancedPanel);
    advancedTools->setContentsMargins(0, 0, 0, 0);
    advancedTools->setHorizontalSpacing(6);
    advancedTools->setVerticalSpacing(3);
    auto *advancedButton = new QToolButton(inkToolbar);
    advancedButton->setObjectName(QStringLiteral("notesDrawingDetailsToggle"));
    advancedButton->setText(QStringLiteral("Details / symbols"));
    advancedButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    advancedButton->setCheckable(true);
    advancedButton->setToolTip(QStringLiteral("Show/hide component tools, exact zoom and view navigation."));
    inkTools->addWidget(advancedButton, 0, 5);
    connect(advancedButton, &QToolButton::toggled, advancedPanel, &QWidget::setVisible);
    advancedPanel->hide();
    auto *helpButton = new QToolButton(inkToolbar);
    helpButton->setText(QStringLiteral("? Help"));
    helpButton->setObjectName(QStringLiteral("notesDrawingHelp"));
    helpButton->setToolTip(QStringLiteral("Drawing, symbols, page navigation and stylus shortcuts."));
    inkTools->addWidget(helpButton, 0, 6);
    connect(helpButton, &QToolButton::clicked, this, [this, drawingHelp] {
        QMessageBox::information(this, QStringLiteral("Drawing editor — quick guide"), drawingHelp);
    });
    auto *tool = new QComboBox(inkPage);
    tool->addItem(QStringLiteral("Pen"));
    tool->addItem(QStringLiteral("Eraser"));
    tool->addItem(QStringLiteral("Text"));
    tool->addItem(QStringLiteral("Select / move"));
    tool->addItem(QStringLiteral("Line"));
    tool->addItem(QStringLiteral("Arrow"));
    tool->addItem(QStringLiteral("Rectangle"));
    tool->addItem(QStringLiteral("Ellipse / circle"));
    tool->addItem(QStringLiteral("Hand / pan"));
    tool->addItem(QStringLiteral("Electronic symbol"));
    tool->addItem(QStringLiteral("Smart wire (orthogonal)"));
    tool->setObjectName(QStringLiteral("notesDrawingTool"));
    tool->setMinimumWidth(155);
    tool->setToolTip(QStringLiteral("Pen and shapes: drag to draw. Text: click to edit. "
                                    "Select / move: drag a selection rectangle, Shift-click to extend selection, drag selection to move. "
                                    "Smart wire: drag from a highlighted terminal to another; connected ends follow symbols."));
    inkTools->addWidget(tool, 0, 0);
    auto *color = new QComboBox(inkPage);
    color->addItem(QStringLiteral("Dark blue"), QColor(27, 54, 86));
    color->addItem(QStringLiteral("Black"), QColor(25, 25, 25));
    color->addItem(QStringLiteral("Red"), QColor(197, 51, 58));
    color->addItem(QStringLiteral("Green"), QColor(25, 121, 83));
    color->addItem(QStringLiteral("Blue"), QColor(40, 92, 188));
    color->setToolTip(QStringLiteral("Ink color."));
    inkTools->addWidget(color, 0, 1);
    auto *lineWidth = new QSpinBox(inkPage);
    lineWidth->setRange(1, 24);
    lineWidth->setValue(4);
    lineWidth->setSuffix(QStringLiteral(" px"));
    lineWidth->setToolTip(QStringLiteral("Ink thickness on the 1600 x 1000 page."));
    inkTools->addWidget(lineWidth, 0, 2);
    auto *textSize = new QSpinBox(inkPage);
    textSize->setRange(12, 96);
    textSize->setValue(32);
    textSize->setSuffix(QStringLiteral(" px"));
    textSize->setToolTip(QStringLiteral("Text size in paper coordinates (12–96 px)."));
    inkTools->addWidget(textSize, 0, 3);
    int inkButtonColumn = 0;
    auto addInkButton = [&](const QString &label, const QString &tip, auto handler) {
        auto *button = new QPushButton(label, inkPage);
        button->setToolTip(tip);
        inkTools->addWidget(button, 1, inkButtonColumn++);
        connect(button, &QPushButton::clicked, this, handler);
    };
    m_inkCanvas = new NotesInkCanvas(inkPage);
    connect(tool, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, advancedButton](int index) {
        // Keep this index order aligned with NotesInkCanvas::Tool.
        if (index >= 0 && index <= int(NotesInkCanvas::Tool::Wire))
            m_inkCanvas->setTool(static_cast<NotesInkCanvas::Tool>(index));
        // The symbol palette should never be hidden when placement is selected.
        if (index == int(NotesInkCanvas::Tool::Component))
            advancedButton->setChecked(true);
    });
    connect(color, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, color](int) {
        m_inkCanvas->setPenColor(color->currentData().value<QColor>());
    });
    connect(lineWidth, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        m_inkCanvas->setPenWidth(value);
    });
    connect(textSize, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        m_inkCanvas->setTextSize(value);
    });

    // Circuit symbols remain vector annotations. They are NOT electrically connected SPICE devices.
    auto *symbolSelector = new QComboBox(advancedPanel);
    symbolSelector->setObjectName(QStringLiteral("notesSymbolSelector"));
    const auto addSymbolOption = [symbolSelector](const QString &name, const QString &id) {
        symbolSelector->addItem(name,id);
    };
    addSymbolOption(QStringLiteral("Passives · Resistor IEC"),QStringLiteral("resistor"));
    addSymbolOption(QStringLiteral("Passives · Resistor zigzag"),QStringLiteral("resistor_us"));
    addSymbolOption(QStringLiteral("Passives · Potentiometer"),QStringLiteral("potentiometer"));
    addSymbolOption(QStringLiteral("Passives · Capacitor"),QStringLiteral("capacitor"));
    addSymbolOption(QStringLiteral("Passives · Polarized capacitor"),QStringLiteral("capacitor_polar"));
    addSymbolOption(QStringLiteral("Passives · Inductor"),QStringLiteral("inductor"));
    addSymbolOption(QStringLiteral("Passives · Transformer"),QStringLiteral("transformer"));
    addSymbolOption(QStringLiteral("Passives · Fuse"),QStringLiteral("fuse"));
    addSymbolOption(QStringLiteral("Transistors · BJT NPN"),QStringLiteral("bjt_npn"));
    addSymbolOption(QStringLiteral("Transistors · BJT PNP"),QStringLiteral("bjt_pnp"));
    addSymbolOption(QStringLiteral("Transistors · NMOS"),QStringLiteral("mos_n"));
    addSymbolOption(QStringLiteral("Transistors · PMOS"),QStringLiteral("mos_p"));
    addSymbolOption(QStringLiteral("Transistors · N-JFET"),QStringLiteral("jfet_n"));
    addSymbolOption(QStringLiteral("Transistors · P-JFET"),QStringLiteral("jfet_p"));
    addSymbolOption(QStringLiteral("Analog · Operational amplifier"),QStringLiteral("opamp"));
    addSymbolOption(QStringLiteral("Semiconductors · Diode"),QStringLiteral("diode"));
    addSymbolOption(QStringLiteral("Semiconductors · Zener"),QStringLiteral("zener"));
    addSymbolOption(QStringLiteral("Semiconductors · LED"),QStringLiteral("led"));
    addSymbolOption(QStringLiteral("Sources · DC voltage"),QStringLiteral("voltage_dc"));
    addSymbolOption(QStringLiteral("Sources · AC voltage"),QStringLiteral("voltage_ac"));
    addSymbolOption(QStringLiteral("Sources · Current"),QStringLiteral("current_source"));
    addSymbolOption(QStringLiteral("Sources · Battery"),QStringLiteral("battery"));
    addSymbolOption(QStringLiteral("Connections · Ground"),QStringLiteral("ground"));
    addSymbolOption(QStringLiteral("Connections · Switch SPST"),QStringLiteral("switch"));
    addSymbolOption(QStringLiteral("Connections · Junction"),QStringLiteral("junction"));
    addSymbolOption(QStringLiteral("Other · Crystal"),QStringLiteral("crystal"));
    addSymbolOption(QStringLiteral("Other · Antenna"),QStringLiteral("antenna"));
    symbolSelector->setMinimumWidth(260);
    symbolSelector->setToolTip(QStringLiteral("Choose a vector circuit symbol. It is an illustration, not a simulated electrical component."));
    advancedTools->addWidget(new QLabel(QStringLiteral("Component:"),advancedPanel),1,0);
    advancedTools->addWidget(symbolSelector,1,1,1,2);
    auto *symbolSize=new QSpinBox(advancedPanel);
    symbolSize->setObjectName(QStringLiteral("notesSymbolSize"));
    symbolSize->setRange(65,200);
    symbolSize->setSingleStep(10);
    symbolSize->setValue(100);
    symbolSize->setSuffix(QStringLiteral(" %"));
    symbolSize->setToolTip(QStringLiteral("Size of symbols to place and of selected symbols when Apply size is clicked."));
    advancedTools->addWidget(symbolSize,1,3);
    auto *snapSymbols=new QCheckBox(QStringLiteral("Snap 20 px"),advancedPanel);
    snapSymbols->setChecked(true);
    snapSymbols->setToolTip(QStringLiteral("Snap symbol centres to a 20 px drawing grid."));
    advancedTools->addWidget(snapSymbols,1,4);
    auto *chooseSymbol=new QPushButton(QStringLiteral("Place symbol"),advancedPanel);
    chooseSymbol->setToolTip(QStringLiteral("Choose component placement mode. Then click the drawing page; repeated clicks place copies."));
    advancedTools->addWidget(chooseSymbol,1,5);
    auto *rotateSymbol=new QPushButton(QStringLiteral("Rotate 90°"),advancedPanel);
    advancedTools->addWidget(rotateSymbol,1,6);
    auto *setSelectedSize=new QPushButton(QStringLiteral("Apply size"),advancedPanel);
    advancedTools->addWidget(setSelectedSize,1,7);
    auto *renameSymbol=new QPushButton(QStringLiteral("Rename"),advancedPanel);
    advancedTools->addWidget(renameSymbol,2,0);
    auto *removeSymbol=new QPushButton(QStringLiteral("Delete symbol"),advancedPanel);
    advancedTools->addWidget(removeSymbol,2,1);
    auto *symbolHint=new QLabel(QStringLiteral("Place: click canvas • Edit: Select / move then Rotate, Apply size, Rename or Delete • Lines are drawings, not nets."),advancedPanel);
    symbolHint->setWordWrap(true);
    advancedTools->addWidget(symbolHint,2,2,1,6);
    connect(symbolSelector,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,symbolSelector,tool](int) {
        m_inkCanvas->setSymbolType(symbolSelector->currentData().toString());
        tool->setCurrentIndex(int(NotesInkCanvas::Tool::Component));
    });
    connect(symbolSize,qOverload<int>(&QSpinBox::valueChanged),this,[this](int value) {
        m_inkCanvas->setSymbolScale(double(value)/100.0);
    });
    connect(snapSymbols,&QCheckBox::toggled,this,[this](bool enabled) {
        m_inkCanvas->setSymbolSnap(enabled);
    });
    connect(chooseSymbol,&QPushButton::clicked,this,[this,symbolSelector,tool] {
        m_inkCanvas->setSymbolType(symbolSelector->currentData().toString());
        tool->setCurrentIndex(int(NotesInkCanvas::Tool::Component));
    });
    connect(rotateSymbol,&QPushButton::clicked,this,[this] {
        if (!m_inkCanvas->rotateSelectedSymbol() && m_inkStatus)
            m_inkStatus->setText(QStringLiteral("Select a circuit symbol first (Select / move)."));
    });
    connect(setSelectedSize,&QPushButton::clicked,this,[this,symbolSize] {
        if (!m_inkCanvas->resizeSelectedSymbol(double(symbolSize->value())/100.0) && m_inkStatus)
            m_inkStatus->setText(QStringLiteral("Select a circuit symbol first (Select / move)."));
    });
    connect(renameSymbol,&QPushButton::clicked,this,[this] {
        if (!m_inkCanvas->hasSelectedSymbol()) {
            if(m_inkStatus) m_inkStatus->setText(QStringLiteral("Select a circuit symbol first (Select / move)."));
            return;
        }
        bool ok=false;
        const QString label=QInputDialog::getText(this,QStringLiteral("Symbol reference"),
            QStringLiteral("Reference (e.g. R1, Q2, U3):"),QLineEdit::Normal,
            m_inkCanvas->selectedSymbolReference(),&ok);
        if (ok && !m_inkCanvas->renameSelectedSymbol(label) && m_inkStatus)
            m_inkStatus->setText(QStringLiteral("Reference must contain at most 36 characters and one line."));
    });
    connect(removeSymbol,&QPushButton::clicked,this,[this] {
        if (!m_inkCanvas->deleteSelectedSymbol() && m_inkStatus)
            m_inkStatus->setText(QStringLiteral("Select a circuit symbol first (Select / move)."));
    });

    addInkButton(QStringLiteral("Select all"),
                 QStringLiteral("Select all vector objects, ink strokes, images and annotations on the page."),
                 [this,tool] { tool->setCurrentIndex(int(NotesInkCanvas::Tool::Select)); m_inkCanvas->selectAllObjects(); });
    addInkButton(QStringLiteral("Wire tool"),
                 QStringLiteral("Orthogonal wire; endpoints snap to visible component pins (22 page pixels). "
                                "Wires reroute when you move or rotate the connected symbol; drawing only, not SPICE."),
                 [tool] { tool->setCurrentIndex(int(NotesInkCanvas::Tool::Wire)); });
    addInkButton(QStringLiteral("Undo"), QStringLiteral("Undo last stroke/erasure."), [this] { m_inkCanvas->undo(); });
    addInkButton(QStringLiteral("Redo"), QStringLiteral("Redo last ink edit."), [this] { m_inkCanvas->redo(); });
    addInkButton(QStringLiteral("Copy image"), QStringLiteral("Copy drawing as PNG bitmap to clipboard."), [this] { copyInkImage(); });
    addInkButton(QStringLiteral("Export PNG"), QStringLiteral("Export current page at 1600 x 1000 pixels, including images."), [this] { exportInkPng(); });
    addInkButton(QStringLiteral("Insert image"), QStringLiteral("Add a PNG/JPEG/WebP/BMP screenshot or diagram on the current page; image is embedded in the notebook."), [this] {
        const QString path=QFileDialog::getOpenFileName(this, QStringLiteral("Insert image on drawing page"),
            QString(), QStringLiteral("Images (*.png *.jpg *.jpeg *.webp *.bmp)"));
        if (path.isEmpty()) return;
        const QFileInfo info(path);
        if (!info.isFile() || info.size()>20*1024*1024)
        {
            QMessageBox::warning(this, QStringLiteral("Image too large"),
                QStringLiteral("Choose an image file smaller than 20 MiB."));
            return;
        }
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QSize original=reader.size();
        if (!original.isValid() || qint64(original.width())*original.height()>25000000LL)
        {
            QMessageBox::warning(this, QStringLiteral("Image too large"),
                QStringLiteral("Image dimensions are unsupported (maximum 25 million pixels)."));
            return;
        }
        if (original.width()>1400 || original.height()>900)
            reader.setScaledSize(original.scaled(1400,900,Qt::KeepAspectRatio));
        const QImage image=reader.read();
        if (image.isNull() || !m_inkCanvas->addImage(image))
        {
            QMessageBox::warning(this, QStringLiteral("Cannot insert image"),
                QStringLiteral("Unable to decode/store this image. The notebook may be full (24 images per page / 24 MiB per file)."));
            return;
        }
        if (m_inkStatus) m_inkStatus->setText(QStringLiteral("Image inserted — Select / move to reposition it."));
    });
    auto pasteDrawingImage=[this] {
        const QImage image=QApplication::clipboard()->image();
        if (image.isNull())
        {
            if (m_inkStatus) m_inkStatus->setText(QStringLiteral("Clipboard contains no image."));
            return;
        }
        if (!m_inkCanvas->addImage(image))
        {
            QMessageBox::warning(this,QStringLiteral("Cannot paste image"),
                QStringLiteral("The image or notebook exceeds its resource limits."));
            return;
        }
        if (m_inkStatus) m_inkStatus->setText(QStringLiteral("Clipboard image inserted; use Select / move."));
    };
    addInkButton(QStringLiteral("Paste image"), QStringLiteral("Insert the image from clipboard (e.g. Win+Shift+S screenshot)."), pasteDrawingImage);
    auto *pasteInkShortcut=new QShortcut(QKeySequence::Paste,m_inkCanvas);
    pasteInkShortcut->setContext(Qt::WidgetShortcut);
    connect(pasteInkShortcut,&QShortcut::activated,this,pasteDrawingImage);
    addInkButton(QStringLiteral("Clear page"), QStringLiteral("Clear current page (undoable)."), [this] {
        if ((m_inkCanvas->strokeCount() > 0 || m_inkCanvas->textCount() > 0 ||
             m_inkCanvas->shapeCount() > 0 || m_inkCanvas->imageCount() > 0 || m_inkCanvas->symbolCount() > 0) &&
            QMessageBox::warning(this, QStringLiteral("Clear page?"),
            QStringLiteral("Remove all strokes, electronic symbols, technical shapes, images and text from this page? You can undo this action."),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes)
            m_inkCanvas->clearDocument();
    });
    auto *gridCheck = new QCheckBox(QStringLiteral("Grid"), inkPage);
    gridCheck->setChecked(true);
    gridCheck->setToolTip(QStringLiteral("Show or hide the drawing paper grid."));
    inkTools->addWidget(gridCheck, 0, 4);
    connect(gridCheck, &QCheckBox::toggled, this, [this](bool show) { m_inkCanvas->setGridVisible(show); });
    // Zoom is view-only. Insertion/movement always uses page coordinates, so
    // saved strokes, images and text are independent of the window/DPI scale.
    auto *zoomOut = new QPushButton(QStringLiteral("− Zoom"), advancedPanel);
    zoomOut->setObjectName(QStringLiteral("notesZoomOut"));
    advancedTools->addWidget(zoomOut, 0, 0);
    auto *zoomFit = new QPushButton(QStringLiteral("Fit page"), advancedPanel);
    zoomFit->setObjectName(QStringLiteral("notesFitPage"));
    advancedTools->addWidget(zoomFit, 0, 1);
    auto *zoomIn = new QPushButton(QStringLiteral("+ Zoom"), advancedPanel);
    zoomIn->setObjectName(QStringLiteral("notesZoomIn"));
    advancedTools->addWidget(zoomIn, 0, 2);
    auto *zoomLabel = new QLabel(QStringLiteral("100% (fit)"), advancedPanel);
    zoomLabel->setObjectName(QStringLiteral("notesZoomLabel"));
    advancedTools->addWidget(zoomLabel, 0, 3);
    auto *navigationHelp = new QLabel(QStringLiteral("Pan: Hand / middle mouse • Zoom: Ctrl+wheel / pinch"), advancedPanel);
    advancedTools->addWidget(navigationHelp, 0, 4, 1, 4);
    connect(zoomOut, &QPushButton::clicked, this, [this] { m_inkCanvas->zoomOut(); });
    connect(zoomFit, &QPushButton::clicked, this, [this] { m_inkCanvas->fitPage(); });
    connect(zoomIn, &QPushButton::clicked, this, [this] { m_inkCanvas->zoomIn(); });
    m_inkCanvas->setOnViewChanged([zoomLabel](double zoom) {
        zoomLabel->setText(QStringLiteral("%1% (fit ×%2)")
            .arg(qRound(zoom*100.0)).arg(zoom,0,'f',2));
    });
    // Navigation belongs to the canvas toolbar so it also works in expanded mode.
    auto *prevPage = new QPushButton(QStringLiteral("◀ Previous"), inkToolbar);
    prevPage->setObjectName(QStringLiteral("notesPreviousPage"));
    inkTools->addWidget(prevPage, 2, 0);
    m_inkPageSelector = new QSpinBox(inkToolbar);
    m_inkPageSelector->setObjectName(QStringLiteral("notesPageSelector"));
    m_inkPageSelector->setRange(1, 1);
    m_inkPageSelector->setToolTip(QStringLiteral("Go directly to a drawing page."));
    inkTools->addWidget(m_inkPageSelector, 2, 1);
    auto *nextPage = new QPushButton(QStringLiteral("Next ▶"), inkToolbar);
    nextPage->setObjectName(QStringLiteral("notesNextPage"));
    inkTools->addWidget(nextPage, 2, 2);
    auto *addPage = new QPushButton(QStringLiteral("+ Page"), inkToolbar);
    addPage->setObjectName(QStringLiteral("notesAddPage"));
    inkTools->addWidget(addPage, 2, 3);
    auto *deletePage = new QPushButton(QStringLiteral("− Page"), inkToolbar);
    deletePage->setObjectName(QStringLiteral("notesDeletePage"));
    inkTools->addWidget(deletePage, 2, 4);
    auto *expandButton = new QPushButton(QStringLiteral("⛶ Expand drawing"), inkToolbar);
    expandButton->setObjectName(QStringLiteral("notesExpandDrawing"));
    expandButton->setToolTip(QStringLiteral("Edit the same notebook in a maximized window; close to return."));
    inkTools->addWidget(expandButton, 2, 5, 1, 2);
    m_inkPageInfo = new QLabel(inkToolbar);
    inkTools->addWidget(m_inkPageInfo, 2, 7);
    connect(prevPage, &QPushButton::clicked, this, [this] {
        m_inkCanvas->setCurrentPage(m_inkCanvas->currentPageIndex() - 1);
        refreshInkPageControls();
    });
    connect(nextPage, &QPushButton::clicked, this, [this] {
        m_inkCanvas->setCurrentPage(m_inkCanvas->currentPageIndex() + 1);
        refreshInkPageControls();
    });
    connect(m_inkPageSelector, qOverload<int>(&QSpinBox::valueChanged), this, [this](int number) {
        m_inkCanvas->setCurrentPage(number - 1);
        refreshInkPageControls();
    });
    connect(addPage, &QPushButton::clicked, this, [this] {
        if (!m_inkCanvas->addPage())
            QMessageBox::information(this, QStringLiteral("Notebook limit"), QStringLiteral("Maximum of 32 drawing pages."));
        refreshInkPageControls();
    });
    connect(deletePage, &QPushButton::clicked, this, [this] {
        if (m_inkCanvas->pageCount() <= 1) return;
        if (QMessageBox::warning(this, QStringLiteral("Delete drawing page?"),
            QStringLiteral("Delete page %1 and all of its text/ink? This cannot be undone.")
                .arg(m_inkCanvas->currentPageIndex() + 1),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes)
            m_inkCanvas->removeCurrentPage();
        refreshInkPageControls();
    });
    connect(expandButton, &QPushButton::clicked, this, [this, inkPage, inkLayout, inkToolbar, advancedPanel, expandButton] {
        expandButton->setEnabled(false); // prevent nested reparenting of the same canvas
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("QTsignalApp — Notes / Drawing (expanded)"));
        dialog.setWindowFlags(dialog.windowFlags() | Qt::WindowMaximizeButtonHint);
        auto *expanded = new QVBoxLayout(&dialog);
        auto *close = new QPushButton(QStringLiteral("Return to Calculator / Notes"), &dialog);
        close->setObjectName(QStringLiteral("notesCloseExpanded"));
        connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
        expanded->addWidget(close, 0, Qt::AlignRight);
        // Reuse the live canvas: edits, autosave, undo and page navigation are preserved.
        inkLayout->removeWidget(inkToolbar);
        inkLayout->removeWidget(advancedPanel);
        inkLayout->removeWidget(m_inkCanvas);
        inkToolbar->setParent(&dialog);
        advancedPanel->setParent(&dialog);
        m_inkCanvas->setParent(&dialog);
        expanded->addWidget(inkToolbar);
        expanded->addWidget(advancedPanel);
        expanded->addWidget(m_inkCanvas, 1);
        dialog.setMinimumSize(700, 520);
        dialog.showMaximized();
        dialog.exec();
        expanded->removeWidget(inkToolbar);
        expanded->removeWidget(advancedPanel);
        expanded->removeWidget(m_inkCanvas);
        inkToolbar->setParent(inkPage);
        advancedPanel->setParent(inkPage);
        m_inkCanvas->setParent(inkPage);
        inkLayout->insertWidget(0, inkToolbar);
        inkLayout->insertWidget(1, advancedPanel);
        inkLayout->insertWidget(2, m_inkCanvas, 1);
        refreshInkPageControls();
        expandButton->setEnabled(true);
    });
    inkLayout->addWidget(inkToolbar);
    inkLayout->addWidget(advancedPanel);
    inkLayout->addWidget(m_inkCanvas, 1);
    m_inkStatus = new QLabel(QStringLiteral("Drawing ready"), inkPage);
    inkLayout->addWidget(m_inkStatus);
    auto *undoInk = new QShortcut(QKeySequence::Undo, m_inkCanvas);
    undoInk->setContext(Qt::WidgetShortcut);
    connect(undoInk, &QShortcut::activated, this, [this] { m_inkCanvas->undo(); });
    auto *redoInk = new QShortcut(QKeySequence::Redo, m_inkCanvas);
    redoInk->setContext(Qt::WidgetShortcut);
    connect(redoInk, &QShortcut::activated, this, [this] { m_inkCanvas->redo(); });
    m_inkSaveTimer = new QTimer(this);
    m_inkSaveTimer->setSingleShot(true);
    m_inkSaveTimer->setInterval(800);
    connect(m_inkSaveTimer, &QTimer::timeout, this, [this] { saveInkNotes(); });
    m_inkCanvas->setOnModified([this] {
        if (m_switchingNotebook) return;
        if (m_inkSaveTimer) m_inkSaveTimer->start();
        refreshInkPageControls();
        if (m_inkStatus) m_inkStatus->setText(QStringLiteral("Page modified — autosave pending"));
    });

    m_notesSaveTimer = new QTimer(this);
    m_notesSaveTimer->setSingleShot(true);
    m_notesSaveTimer->setInterval(600);
    connect(m_notesSaveTimer, &QTimer::timeout, this, [this] { saveNotes(); });
    connect(m_notesEditor, &QPlainTextEdit::textChanged, this, [this] {
        if (m_switchingNotebook) return;
        if (m_notesSaveTimer) m_notesSaveTimer->start();
        updateNotesStatus(QStringLiteral("Modified — autosave pending"));
    });
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        if (m_notesSaveTimer) m_notesSaveTimer->stop();
        if (m_inkSaveTimer) m_inkSaveTimer->stop();
        saveNotes();
        saveInkNotes();
    });

    loadNotes();
    loadInkNotes();
    refreshInkPageControls();
    updateScopeDescription();
}

void EmCalculatorWidget::refreshInkPageControls()
{
    if (!m_inkCanvas || !m_inkPageSelector || !m_inkPageInfo) return;
    const QSignalBlocker block(m_inkPageSelector);
    m_inkPageSelector->setRange(1, m_inkCanvas->pageCount());
    m_inkPageSelector->setValue(m_inkCanvas->currentPageIndex() + 1);
    m_inkPageInfo->setText(QStringLiteral("%1 / %2")
        .arg(m_inkCanvas->currentPageIndex() + 1).arg(m_inkCanvas->pageCount()));
}

QString EmCalculatorWidget::inkNotesPath() const
{
    if (m_currentNotebookScope == 1)
        return m_activeAntennaProjectFile.isEmpty() ? QString()
             : m_activeAntennaProjectFile + QStringLiteral(".notes.ink.json");
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dir.isEmpty() ? QString() : QDir(dir).filePath(QStringLiteral("global_notes_ink.json"));
}

bool EmCalculatorWidget::saveInkNotes()
{
    if (!m_inkCanvas) return true;
    const QString path = inkNotesPath();
    if (path.isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath()))
    {
        if (m_inkStatus) m_inkStatus->setText(QStringLiteral("ERROR: ink directory unavailable"));
        return false;
    }
    // Preserve older notebook schemas v1-v5 before upgrading to v6.
    // Never overwrite a prior backup; save/load remains atomic and reversible.
    if (QFile::exists(path))
    {
        QFile original(path);
        if (!original.open(QIODevice::ReadOnly))
        {
            if (m_inkStatus) m_inkStatus->setText(QStringLiteral("ERROR: cannot inspect existing notebook."));
            return false;
        }
        const QJsonDocument oldDoc = QJsonDocument::fromJson(original.readAll());
        original.close();
        const int oldVersion = oldDoc.isObject()
            ? oldDoc.object().value(QStringLiteral("version")).toInt(-1) : -1;
        if (oldVersion >= 1 && oldVersion <= 5)
        {
            const QString backupPath = path + QStringLiteral(".v%1.bak").arg(oldVersion);
            if (!QFile::exists(backupPath) && !QFile::copy(path, backupPath))
            {
                if (m_inkStatus) m_inkStatus->setText(QStringLiteral("ERROR: cannot back up legacy notebook."));
                return false;
            }
        }
    }
    QSaveFile file(path);
    const QByteArray data = m_inkCanvas->serialize();
    if (data.size() > 24 * 1024 * 1024)
    {
        if (m_inkStatus) m_inkStatus->setText(QStringLiteral("ERROR: notebook exceeds 24 MiB."));
        return false;
    }
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
    {
        if (m_inkStatus) m_inkStatus->setText(QStringLiteral("ERROR: ink could not be saved: %1").arg(path));
        return false;
    }
    if (m_inkStatus) m_inkStatus->setText(QStringLiteral("Saved page %1/%2 (%3 strokes, %4 shapes, %5 text, %6 images, %7 symbols, %8)")
        .arg(m_inkCanvas->currentPageIndex() + 1).arg(m_inkCanvas->pageCount())
        .arg(m_inkCanvas->strokeCount()).arg(m_inkCanvas->shapeCount())
        .arg(m_inkCanvas->textCount()).arg(m_inkCanvas->imageCount())
        .arg(m_inkCanvas->symbolCount())
        .arg(m_currentNotebookScope == 1 ? QStringLiteral("project") : QStringLiteral("global")));
    return true;
}

bool EmCalculatorWidget::loadInkNotes()
{
    if (!m_inkCanvas) return true;
    const QString path = inkNotesPath();
    if (path.isEmpty()) return false;
    QFile file(path);
    QByteArray data;
    if (file.exists())
    {
        if (!file.open(QIODevice::ReadOnly) || file.size() > 24 * 1024 * 1024)
        {
            if (m_inkStatus) m_inkStatus->setText(QStringLiteral("ERROR: cannot read ink file %1").arg(path));
            return false;
        }
        data = file.readAll();
    }
    QString error;
    if (!m_inkCanvas->loadDocument(data, &error))
    {
        if (m_inkStatus) m_inkStatus->setText(QStringLiteral("ERROR: %1 (%2)").arg(error, path));
        return false;
    }
    if (m_inkSaveTimer) m_inkSaveTimer->stop();
    refreshInkPageControls();
    if (m_inkStatus) m_inkStatus->setText(QStringLiteral("Loaded %1 drawing pages (%2)")
        .arg(m_inkCanvas->pageCount()).arg(m_currentNotebookScope == 1 ? QStringLiteral("project") : QStringLiteral("global")));
    return true;
}

void EmCalculatorWidget::exportInkPng()
{
    if (!m_inkCanvas) return;
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export handwritten drawing"),
        QStringLiteral("QTsignalApp_notes_page_%1.png").arg(m_inkCanvas->currentPageIndex() + 1), QStringLiteral("PNG image (*.png)"));
    if (path.isEmpty()) return;
    if (m_inkCanvas->renderToImage().save(path, "PNG"))
        m_inkStatus->setText(QStringLiteral("PNG exported: %1").arg(path));
    else
        QMessageBox::warning(this, QStringLiteral("Export failed"), QStringLiteral("Unable to save PNG: %1").arg(path));
}

void EmCalculatorWidget::copyInkImage()
{
    if (!m_inkCanvas) return;
    QApplication::clipboard()->setImage(m_inkCanvas->renderToImage());
    if (m_inkStatus) m_inkStatus->setText(QStringLiteral("Drawing copied as image."));
}

QString EmCalculatorWidget::projectNotesSidecarPath() const
{
    return m_activeAntennaProjectFile.isEmpty() ? QString() : m_activeAntennaProjectFile + QStringLiteral(".notes.md");
}

QString EmCalculatorWidget::projectWorksheetSidecarPath() const
{
    return m_activeAntennaProjectFile.isEmpty() ? QString() : m_activeAntennaProjectFile + QStringLiteral(".worksheet.txt");
}

QString EmCalculatorWidget::notesBackupKey() const
{
    if (m_currentNotebookScope == 0) return QStringLiteral("calculatorNotes/backupBeforeImport");
    // Keep the existing global backup compatible with 6.5.1 and isolate
    // a restore point for every project rather than one shared undo slot.
    const QByteArray hash = QCryptographicHash::hash(m_activeAntennaProjectFile.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("calculatorNotes/projectBackups/%1").arg(QString::fromLatin1(hash.constData()));
}

bool EmCalculatorWidget::saveNotes()
{
    if (!m_notesEditor) return true;
    const QString text = m_notesEditor->toPlainText();
    if (m_currentNotebookScope == 1)
    {
        const QString path = projectNotesSidecarPath();
        if (path.isEmpty()) return false;
        QSaveFile file(path);
        const QByteArray data = text.toUtf8();
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        {
            updateNotesStatus(QStringLiteral("ERROR: cannot atomically save project notes to %1").arg(path));
            return false;
        }
    }
    else
    {
        QSettings settings;
        settings.setValue(QStringLiteral("calculatorNotes/plainText"), text);
        settings.sync();
        if (settings.status() != QSettings::NoError)
        {
            updateNotesStatus(QStringLiteral("ERROR: cannot save global notes."));
            return false;
        }
    }
    if (m_notesEditor->document()) m_notesEditor->document()->setModified(false);
    updateNotesStatus(m_currentNotebookScope == 1 ? QStringLiteral("Autosaved to project sidecar")
                                                      : QStringLiteral("Autosaved globally"));
    return true;
}

bool EmCalculatorWidget::loadNotes()
{
    if (!m_notesEditor) return true;
    QString text;
    if (m_currentNotebookScope == 1)
    {
        const QString path = projectNotesSidecarPath();
        if (path.isEmpty()) return false;
        QFile file(path);
        if (file.exists())
        {
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text) || file.size() > 5 * 1024 * 1024)
            {
                updateNotesStatus(QStringLiteral("ERROR: cannot read project notebook: %1").arg(path));
                return false;
            }
            text = QString::fromUtf8(file.readAll());
        }
    }
    else
        text = QSettings().value(QStringLiteral("calculatorNotes/plainText")).toString();
    m_switchingNotebook = true;
    if (m_notesSaveTimer) m_notesSaveTimer->stop();
    m_notesEditor->setPlainText(text);
    if (m_notesEditor->document()) m_notesEditor->document()->setModified(false);
    m_switchingNotebook = false;
    updateNotesStatus(m_currentNotebookScope == 1 ? QStringLiteral("Project notebook loaded")
                                                      : QStringLiteral("Global notebook loaded"));
    return true;
}

bool EmCalculatorWidget::saveWorksheet()
{
    if (!m_variableWorksheet) return true;
    const QString text = m_variableWorksheet->toPlainText();
    if (m_currentNotebookScope == 1)
    {
        const QString path = projectWorksheetSidecarPath();
        if (path.isEmpty()) return false;
        QSaveFile file(path);
        const QByteArray data = text.toUtf8();
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        {
            if (m_variableStatus) m_variableStatus->setText(QStringLiteral("ERROR: worksheet could not be saved to %1").arg(path));
            return false;
        }
        return true;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("calculator/unitsWorksheet"), text);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

bool EmCalculatorWidget::loadWorksheet()
{
    if (!m_variableWorksheet) return true;
    QString text;
    if (m_currentNotebookScope == 1)
    {
        const QString path = projectWorksheetSidecarPath();
        if (path.isEmpty()) return false;
        QFile file(path);
        if (file.exists())
        {
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text) || file.size() > 1024 * 1024)
            {
                if (m_variableStatus) m_variableStatus->setText(QStringLiteral("ERROR: cannot read project worksheet: %1").arg(path));
                return false;
            }
            text = QString::fromUtf8(file.readAll());
        }
    }
    else
        text = QSettings().value(QStringLiteral("calculator/unitsWorksheet"), QStringLiteral("q = 2 nC\nr = 10 cm\nE = ke*q/r^2\nforce = q*E")).toString();
    m_switchingNotebook = true;
    if (m_variableSaveTimer) m_variableSaveTimer->stop();
    m_variableWorksheet->setPlainText(text);
    m_switchingNotebook = false;
    m_userVariables.clear();
    if (text.trimmed().isEmpty())
    {
        m_variableResults->setRowCount(0);
        m_variableStatus->setText(QStringLiteral("New project worksheet — enter variables then Calculate."));
    }
    else evaluateVariableWorksheet();
    return true;
}

void EmCalculatorWidget::changeNotebookScope(int scope)
{
    if (scope == m_currentNotebookScope || !m_notebookScope) return;
    if (scope == 1 && m_activeAntennaProjectFile.isEmpty())
    {
        const QSignalBlocker blocked(m_notebookScope);
        m_notebookScope->setCurrentIndex(m_currentNotebookScope);
        QMessageBox::information(this, QStringLiteral("Project notebook"),
            QStringLiteral("First load or save an antenna .qta file in Antenna designer. "
                           "The corresponding notebook can then be selected here."));
        return;
    }
    if (!saveNotes() || !saveWorksheet() || !saveInkNotes())
    {
        const QSignalBlocker blocked(m_notebookScope);
        m_notebookScope->setCurrentIndex(m_currentNotebookScope);
        QMessageBox::warning(this, QStringLiteral("Notebook save failed"),
                             QStringLiteral("The current notebook or worksheet could not be saved. Scope was not changed."));
        return;
    }
    const int oldScope = m_currentNotebookScope;
    m_currentNotebookScope = scope;
    if (!loadNotes() || !loadWorksheet() || !loadInkNotes())
    {
        m_currentNotebookScope = oldScope;
        const QSignalBlocker blocked(m_notebookScope);
        m_notebookScope->setCurrentIndex(oldScope);
        loadNotes();
        loadWorksheet();
        loadInkNotes();
        QMessageBox::warning(this, QStringLiteral("Notebook load failed"),
                             QStringLiteral("The requested notebook could not be read. The previous one remains selected."));
    }
    updateScopeDescription();
}

void EmCalculatorWidget::updateScopeDescription()
{
    const QString fileName = QFileInfo(m_activeAntennaProjectFile).fileName();
    if (m_projectNotesLabel)
    {
        m_projectNotesLabel->setText(m_activeAntennaProjectFile.isEmpty()
            ? QStringLiteral("Global notes · no .qta file linked")
            : QStringLiteral("Project: %1").arg(fileName));
        m_projectNotesLabel->setToolTip(m_activeAntennaProjectFile.isEmpty()
            ? QStringLiteral("Open a .qta project to enable project-specific notes.")
            : QStringLiteral("Companion files: %1.notes.md, %1.notes.ink.json, %1.worksheet.txt")
                  .arg(m_activeAntennaProjectFile));
    }
    if (m_variableScopeLabel)
        m_variableScopeLabel->setText(m_currentNotebookScope == 1
            ? QStringLiteral("Worksheet scope: Project %1. Sidecar saved atomically next to the .qta.").arg(fileName)
            : QStringLiteral("Worksheet scope: Global. To use a project-specific worksheet, select Project in Notes."));
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
    if (file.size() > 5 * 1024 * 1024)
    {
        updateNotesStatus(QStringLiteral("ERROR: notes import exceeds 5 MiB."));
        return;
    }
    QTextStream stream(&file);
    const QString imported = stream.readAll();
    const QString existing = m_notesEditor->toPlainText();
    if (imported == existing)
    {
        updateNotesStatus(QStringLiteral("Notes are already identical to %1").arg(path));
        return;
    }
    if (!existing.isEmpty())
    {
        const auto confirmation = QMessageBox::question(this, QStringLiteral("Replace notes?"),
            QStringLiteral("Importing %1 will replace your current notebook. A restorable backup will be saved before replacement. Continue?").arg(path),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (confirmation != QMessageBox::Yes) return;
    }
    // Persist a snapshot even when the previous notebook is empty, so the
    // Restore action always corresponds to the most recent import.
    QSettings settings;
    settings.setValue(notesBackupKey(), existing);
    settings.setValue(QStringLiteral("calculatorNotes/backupDateUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    settings.sync();
    if (settings.status() != QSettings::NoError)
    {
        updateNotesStatus(QStringLiteral("ERROR: could not save notes backup; import cancelled."));
        return;
    }
    if (m_notesSaveTimer) m_notesSaveTimer->stop();
    m_notesEditor->setPlainText(imported);
    saveNotes();
    updateNotesStatus(QStringLiteral("Opened %1 — previous notes can be restored from backup").arg(path));
}

void EmCalculatorWidget::restorePreviousNotes()
{
    if (!m_notesEditor) return;
    QSettings settings;
    const QString key = notesBackupKey();
    if (!settings.contains(key))
    {
        updateNotesStatus(QStringLiteral("No earlier notebook backup is available."));
        return;
    }
    const auto reply = QMessageBox::question(this, QStringLiteral("Restore saved notes?"),
        QStringLiteral("Restore notes from the last import backup? The current notebook will become the new backup."),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (reply != QMessageBox::Yes) return;
    const QString backup = settings.value(key).toString();
    const QString current = m_notesEditor->toPlainText();
    settings.setValue(key, current);
    settings.setValue(QStringLiteral("calculatorNotes/backupDateUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    settings.sync();
    if (settings.status() != QSettings::NoError)
    {
        updateNotesStatus(QStringLiteral("ERROR: backup could not be updated; restore cancelled."));
        return;
    }
    if (m_notesSaveTimer) m_notesSaveTimer->stop();
    m_notesEditor->setPlainText(backup);
    saveNotes();
    updateNotesStatus(QStringLiteral("Restored previous notebook from backup."));
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
    m_notesStatus->setText(QStringLiteral("%1 — %2 lines / %3 characters | %4.")
                               .arg(lead).arg(lines).arg(chars)
                               .arg(m_currentNotebookScope == 1 ? QStringLiteral("Project sidecar") : QStringLiteral("Global")));
}

void EmCalculatorWidget::invalidateWaveCalculation()
{
    m_waveValid = false;
    if (m_waveCopyFrequency) m_waveCopyFrequency->setEnabled(false);
    if (m_waveCopyWavelength) m_waveCopyWavelength->setEnabled(false);
    if (m_waveCopyResult) m_waveCopyResult->setEnabled(false);
    if (m_waveResult) m_waveResult->setPlainText(QStringLiteral("Inputs changed — recalculate before copying a result."));
}

void EmCalculatorWidget::displayWaveResult(const EmCalculator::WaveResult &r)
{
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
    m_validatedWaveResult = r;
    m_waveValid = true;
    m_waveResult->setPlainText(text);
    m_waveCopyFrequency->setEnabled(true);
    m_waveCopyWavelength->setEnabled(true);
    m_waveCopyResult->setEnabled(true);
}

void EmCalculatorWidget::calculateWaveFromFrequency()
{
    invalidateWaveCalculation();
    EmCalculator::WaveResult r;
    std::string error;
    if (!EmCalculator::waveFromFrequency(scaledValue(m_waveFrequency->value(), m_waveFrequencyUnit),
                                         m_waveEr->value(), m_waveMur->value(), r, error))
    {
        m_waveResult->setPlainText(QStringLiteral("ERROR: %1").arg(QString::fromStdString(error)));
        return;
    }
    const double selectedScale = m_waveLengthUnit->currentData().toDouble();
    {
        const QSignalBlocker block(m_waveLength);
        m_waveLength->setValue(r.wavelengthM / selectedScale);
    }
    displayWaveResult(r);
}

void EmCalculatorWidget::calculateWaveFromWavelength()
{
    invalidateWaveCalculation();
    EmCalculator::WaveResult r;
    std::string error;
    if (!EmCalculator::waveFromWavelength(scaledValue(m_waveLength->value(), m_waveLengthUnit),
                                          m_waveEr->value(), m_waveMur->value(), r, error))
    {
        m_waveResult->setPlainText(QStringLiteral("ERROR: %1").arg(QString::fromStdString(error)));
        return;
    }
    const double selectedScale = m_waveFrequencyUnit->currentData().toDouble();
    {
        const QSignalBlocker block(m_waveFrequency);
        m_waveFrequency->setValue(r.frequencyHz / selectedScale);
    }
    displayWaveResult(r);
}

void EmCalculatorWidget::invalidateAntennaCalculation()
{
    m_antennaValid = false;
    if (m_createInDesigner) m_createInDesigner->setEnabled(false);
    if (m_antennaCopyFrequency) m_antennaCopyFrequency->setEnabled(false);
    if (m_antennaCopyPrimary) m_antennaCopyPrimary->setEnabled(false);
    if (m_antennaCopyResult) m_antennaCopyResult->setEnabled(false);
    if (m_antennaResult)
        m_antennaResult->setPlainText(QStringLiteral("Inputs changed — recalculate before copying or creating an antenna."));
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
    // Only a validated, current calculation may create geometry. Never
    // substitute an old frequency when the inverse solver reports an error.
    if (!m_antennaValid) return;
    if (!m_createAntennaHandler)
    {
        m_antennaHint->setText(QStringLiteral("Antenna designer is not available in this workspace."));
        return;
    }
    const auto kind = m_validatedAntennaKind;
    const EmCalculator::AntennaInputs in = m_validatedAntennaInputs;
    QStringList dimensions;
    dimensions << QStringLiteral("Frequency: %1 MHz").arg(in.frequencyHz / 1e6, 0, 'g', 9);
    for (const auto &item : m_validatedAntennaResult.items)
    {
        dimensions << QStringLiteral("%1 = %2 %3")
            .arg(QString::fromStdString(item.label), QString::number(item.value, 'g', 8), QString::fromStdString(item.unit));
    }
    auto confirmation = QMessageBox(this);
    confirmation.setIcon(QMessageBox::Warning);
    confirmation.setWindowTitle(QStringLiteral("Preview antenna geometry"));
    confirmation.setText(QStringLiteral("Create %1 in Antenna designer?")
        .arg(QString::fromLatin1(EmCalculator::antennaName(kind))));
    const QString summary = dimensions.mid(0, 6).join(QLatin1Char('\n'));
    confirmation.setInformativeText(summary + QStringLiteral("\n\nThe current geometry will be replaced "
        "(Undo available). Starting estimates only; validate ports and matching."));
    confirmation.setDetailedText(dimensions.join(QLatin1Char('\n')) + QStringLiteral("\nAssumptions: ")
        + QString::fromStdString(m_validatedAntennaResult.assumption));
    auto *confirm = confirmation.addButton(QStringLiteral("Replace geometry"), QMessageBox::AcceptRole);
    confirmation.addButton(QMessageBox::Cancel);
    confirmation.setDefaultButton(QMessageBox::Cancel);
    confirmation.exec();
    if (confirmation.clickedButton() != confirm) return;
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

bool EmCalculatorWidget::calculateAntenna()
{
    invalidateAntennaCalculation();
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
        return false;
    }

    if (inverse)
    {
        const double scale = m_antennaFrequencyUnit->currentData().toDouble();
        const QSignalBlocker block(m_antennaFrequency);
        m_antennaFrequency->setValue(r.frequencyHz / scale);
    }
    else if (kind != EmCalculator::AntennaKind::ParabolicDish)
    {
        const double scale = m_primaryDimensionUnit->currentData().toDouble();
        const QSignalBlocker block(m_primaryDimension);
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
    // Commit only after the entire solve has succeeded. The Designer uses
    // this immutable snapshot, not mutable text or a possibly stale spinbox.
    in.frequencyHz = r.frequencyHz;
    in.primaryDimensionM = r.primaryDimensionM;
    m_validatedAntennaInputs = in;
    m_validatedAntennaKind = kind;
    m_validatedAntennaResult = r;
    m_antennaValid = true;
    m_antennaCopyFrequency->setEnabled(true);
    m_antennaCopyPrimary->setEnabled(true);
    m_antennaCopyResult->setEnabled(true);
    m_createInDesigner->setEnabled(static_cast<bool>(m_createAntennaHandler));
    return true;
}

QString EmCalculatorWidget::scientificExactText() const
{
    return m_scientificValid ? precise(m_validatedScientificQuantity.siValue) : QString();
}

QString EmCalculatorWidget::scientificDisplayText() const
{
    if (!m_scientificValid) return QString();
    const std::string unit = EmCalculator::quantityUnit(m_validatedScientificQuantity);
    const auto presentation = EmDisplay::scaled(m_validatedScientificQuantity.siValue, unit,
        m_autoPrefix && m_autoPrefix->isChecked());
    const int digits = m_significantDigits ? m_significantDigits->value() : 6;
    const QString numeric = QString::number(presentation.value, 'g', digits);
    return presentation.unit == "1" ? numeric : numeric + QStringLiteral(" ") + QString::fromStdString(presentation.unit);
}

void EmCalculatorWidget::invalidateScientificDisplay()
{
    m_scientificValid = false;
    if (m_expressionResult) m_expressionResult->clear();
    if (m_expressionUnit) m_expressionUnit->clear();
    for (QPushButton *button : {m_scienceCopyValue, m_scienceCopyUnit, m_scienceCopyExpression, m_scienceSendNotes})
        if (button) button->setEnabled(false);
}

void EmCalculatorWidget::refreshScientificDisplay()
{
    if (!m_scientificValid) return;
    const std::string unit = EmCalculator::quantityUnit(m_validatedScientificQuantity);
    const auto presentation = EmDisplay::scaled(m_validatedScientificQuantity.siValue, unit,
        m_autoPrefix && m_autoPrefix->isChecked());
    if (m_expressionResult) m_expressionResult->setText(QString::number(presentation.value, 'g',
        m_significantDigits ? m_significantDigits->value() : 6));
    if (m_expressionUnit) m_expressionUnit->setText(QString::fromStdString(presentation.unit));
    if (m_expressionStatus)
        m_expressionStatus->setText(QStringLiteral("Valid — displayed: %1 | exact SI: %2 %3")
            .arg(scientificDisplayText(), scientificExactText(), QString::fromStdString(unit)));
}

void EmCalculatorWidget::evaluateScientificExpression()
{
    const QString expression = m_expression->text().trimmed();
    invalidateScientificDisplay();
    EmCalculator::VariableTable vars = m_userVariables;
    if (m_lastAnswerValid) vars["ans"] = m_lastQuantity;
    const EmCalculator::QuantityResult result = EmCalculator::evaluateQuantity(expression.toStdString(), vars);
    if (!result.ok)
    {
        m_expressionStatus->setText(QStringLiteral("Error at character %1: %2")
            .arg(result.errorPosition + 1).arg(QString::fromStdString(result.error)));
        return;
    }
    m_validatedScientificExpression = expression;
    m_validatedScientificQuantity = result.quantity;
    m_scientificValid = true;
    refreshScientificDisplay();
    for (QPushButton *button : {m_scienceCopyValue, m_scienceCopyUnit, m_scienceCopyExpression, m_scienceSendNotes})
        if (button) button->setEnabled(true);
    m_lastAnswer = result.quantity.siValue;
    m_lastQuantity = result.quantity;
    m_lastAnswerValid = true;
    if (m_historyTable && !expression.isEmpty())
    {
        const QString value = scientificExactText();
        const QString unit = QString::fromStdString(EmCalculator::quantityUnit(result.quantity));
        const QString historyValue = value + QStringLiteral(" ") + unit;
        bool repeated = false;
        if (m_historyTable->rowCount() > 0)
        {
            const auto *latest = m_historyTable->item(0, 0);
            repeated = latest && latest->text() == expression && m_historyTable->item(0, 1)->text() == historyValue;
        }
        if (!repeated)
        {
            m_historyTable->insertRow(0);
            m_historyTable->setItem(0, 0, new QTableWidgetItem(expression));
            m_historyTable->setItem(0, 1, new QTableWidgetItem(historyValue));
            if (m_historyTable->rowCount() > 30) m_historyTable->removeRow(30);
        }
    }
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
    if (!m_expression) return;
    m_expression->setFocus();
    m_expression->insert(token); // replace selection, preserve cursor, support undo
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
