#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "widgets/electrostatic_canvas.h"
#include "widgets/field_3d_views.h"
#include "widgets/magnetostatic_workspace.h"
#include "widgets/electromagnetism_reference_widget.h"
#include "widgets/em_calculator_widget.h"
#include "widgets/field_profiles_widget.h"
#include "widgets/em_engineering_workspace.h"
#include "widgets/antenna_designer_widget.h"
#include "field_view_plane.h"
#include "ui_style_manager.h"

#include <QCheckBox>
#include <QActionGroup>
#include <QApplication>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QStatusBar>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QKeySequence>
#include <QDoubleSpinBox>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QLayoutItem>
#include <QFormLayout>
#include <QSizePolicy>

#include <algorithm>
#include <cmath>

namespace
{
using SourceType = ElectrostaticSource::Type;

int typeToIndex(SourceType type)
{
    return static_cast<int>(type);
}

SourceType indexToType(int index)
{
    index = std::clamp(index, 0, static_cast<int>(SourceType::RectangularVolume));
    return static_cast<SourceType>(index);
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      ui(std::make_unique<Ui::MainWindow>()),
      m_electrostaticModel(std::make_unique<ElectrostaticModel>(this))
{
    ui->setupUi(this);
    initializeElectrostaticWorkspace();
    initializeMagnetostaticAndReferenceWorkspaces();
    initializeMenuBar();
}

MainWindow::~MainWindow() = default;

void MainWindow::initializeElectrostaticWorkspace()
{
    ui->electrostaticCanvas->setModel(m_electrostaticModel.get());

    // Keep the electrostatic editor usable with large fonts / Windows DPI scaling.
    // Long source names must not force the form wider than the scroll viewport.
    ui->sourceEditorLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    ui->sourceTypeCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    ui->sourceTypeCombo->setMinimumContentsLength(14);
    ui->newSourceTypeCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    ui->newSourceTypeCombo->setMinimumContentsLength(14);
    ui->sourceCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    ui->sourceCombo->setMinimumContentsLength(14);
    for (QWidget *w : {static_cast<QWidget *>(ui->sourceNameEdit), static_cast<QWidget *>(ui->sourceTypeCombo),
                       static_cast<QWidget *>(ui->newSourceTypeCombo), static_cast<QWidget *>(ui->sourceCombo)})
    {
        QSizePolicy policy = w->sizePolicy();
        policy.setHorizontalPolicy(QSizePolicy::Expanding);
        w->setSizePolicy(policy);
        w->setMinimumWidth(0);
    }

    const QList<SourceType> electroTypes{
        SourceType::PointCharge,
        SourceType::SolidSphere,
        SourceType::SphericalShell,
        SourceType::ThickSphericalShell,
        SourceType::InfiniteCylinderZ,
        SourceType::InfiniteHollowCylinderZ,
        SourceType::InfiniteLine,
        SourceType::FiniteLine,
        SourceType::RectangularPlate,
        SourceType::RectangularVolume,
        SourceType::CircularPlate,
        SourceType::AnnularPlate,
        SourceType::TriangularPlate,
        SourceType::InfinitePlane
    };
    // Enum order is kept stable in the editor, while the Add-source list is grouped pedagogically.
    for (int i = 0; i <= static_cast<int>(SourceType::RectangularVolume); ++i)
        ui->sourceTypeCombo->addItem(ElectrostaticModel::typeName(static_cast<SourceType>(i)));
    for (const SourceType type : electroTypes)
        ui->newSourceTypeCombo->addItem(ElectrostaticModel::typeName(type), static_cast<int>(type));

    ui->strengthUnitCombo->addItems({QStringLiteral("n"), QStringLiteral("u"), QStringLiteral("m"), QStringLiteral("")});
    ui->testChargeUnitCombo->addItems({QStringLiteral("nC"), QStringLiteral("uC"), QStringLiteral("mC"), QStringLiteral("C")});

    connect(ui->addSourceButton, &QPushButton::clicked, this, [this] {
        const int raw = ui->newSourceTypeCombo->currentData().toInt();
        addSource(indexToType(raw));
    });

    connect(ui->removeSourceButton, &QPushButton::clicked, this, [this] {
        if (m_selectedSource < 0)
            return;
        const int old = m_selectedSource;
        m_electrostaticModel->removeSource(old);
        refreshSourceList(std::min(old, m_electrostaticModel->sourceCount() - 1));
    });

    connect(ui->sourceCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_loadingEditor)
            return;
        m_selectedSource = index;
        ui->electrostaticCanvas->setSelectedSource(index);
        if (m_electrostatic3D) m_electrostatic3D->setSelectedSource(index);
        loadSourceEditor(index);
    });

    connect(ui->previousSourceButton, &QPushButton::clicked, this, [this] {
        if (m_electrostaticModel->sourceCount() == 0)
            return;
        int next = m_selectedSource <= 0 ? m_electrostaticModel->sourceCount() - 1 : m_selectedSource - 1;
        ui->sourceCombo->setCurrentIndex(next);
    });

    connect(ui->nextSourceButton, &QPushButton::clicked, this, [this] {
        if (m_electrostaticModel->sourceCount() == 0)
            return;
        int next = (m_selectedSource + 1) % m_electrostaticModel->sourceCount();
        ui->sourceCombo->setCurrentIndex(next);
    });

    connect(ui->applySourceButton, &QPushButton::clicked, this, &MainWindow::applySourceEditor);
    connect(ui->sourceTypeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        if (!m_loadingEditor)
        {
            updateSourceEditorVisibility();
            updateStrengthUnitLabels();
        }
    });
    connect(ui->strengthUnitCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        updateStrengthUnitLabels();
        if (m_selectedSource >= 0)
            loadSourceEditor(m_selectedSource);
    });

    connect(ui->electrostaticCanvas, &ElectrostaticCanvas::selectedSourceChanged, this, [this](int index) {
        if (index >= 0 && index < ui->sourceCombo->count() && ui->sourceCombo->currentIndex() != index)
            ui->sourceCombo->setCurrentIndex(index);
    });

    connect(ui->electrostaticCanvas, &ElectrostaticCanvas::measurementPointChanged, this, [this](double x, double y, double z) {
        {
            const QSignalBlocker blockX(ui->measureXSpin);
            const QSignalBlocker blockY(ui->measureYSpin);
            const QSignalBlocker blockZ(ui->measureZSpin);
            ui->measureXSpin->setValue(x);
            ui->measureYSpin->setValue(y);
            ui->measureZSpin->setValue(z);
        }
        setMeasurementFromEditors();
    });

    connect(ui->electrostaticCanvas, &ElectrostaticCanvas::sourcePositionEdited, this,
            [this](int index, double x, double y, double z) {
        const auto *current = m_electrostaticModel->source(index);
        if (!current)
            return;
        ElectrostaticSource edited = *current;
        edited.position = {x, y, z};
        m_electrostaticModel->updateSource(index, edited);
        if (index == m_selectedSource)
        {
            const QSignalBlocker bx(ui->sourceXSpin);
            const QSignalBlocker by(ui->sourceYSpin);
            const QSignalBlocker bz(ui->sourceZSpin);
            ui->sourceXSpin->setValue(x);
            ui->sourceYSpin->setValue(y);
            ui->sourceZSpin->setValue(z);
        }
        updateMeasurementResults();
    });

    const auto measurementChanged = [this] { setMeasurementFromEditors(); };
    connect(ui->measureXSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, measurementChanged);
    connect(ui->measureYSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, measurementChanged);
    connect(ui->measureZSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, measurementChanged);
    connect(ui->testChargeSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { updateMeasurementResults(); });
    connect(ui->testChargeUnitCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updateMeasurementResults(); });

    connect(ui->showFieldVectorsCheck, &QCheckBox::toggled, ui->electrostaticCanvas, &ElectrostaticCanvas::setShowFieldVectors);
    connect(ui->centerViewButton, &QPushButton::clicked, ui->electrostaticCanvas, &ElectrostaticCanvas::centerView);

    // Keep compact XY/XZ/YZ selectors readable even with a larger font or QSS padding.
    ui->electroPlaneCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    ui->electroPlaneCombo->setMinimumContentsLength(3);
    ui->electroPlaneCombo->setMinimumWidth(92);

    // Visualization overlays: keep the interaction toolbar simple and put analysis layers on a second row.
    auto *visualizationToolbar = new QHBoxLayout();
    auto *scalarLabel = new QLabel(QStringLiteral("Scalar map:"), ui->Electrostatique);
    auto *scalarCombo = new QComboBox(ui->Electrostatique);
    scalarCombo->addItems({QStringLiteral("Off"), QStringLiteral("|E|"), QStringLiteral("Potential V")});
    scalarCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    scalarCombo->setMinimumContentsLength(10);
    scalarCombo->setMinimumWidth(128);
    scalarCombo->setToolTip(QStringLiteral("Heatmap over the active XY/XZ/YZ slice. |E| uses a logarithmic color scale."));
    auto *contoursCheck = new QCheckBox(QStringLiteral("Contours"), ui->Electrostatique);
    contoursCheck->setToolTip(QStringLiteral("For Potential V these contours are equipotential lines; for |E| they are iso-magnitude lines."));
    contoursCheck->setEnabled(false);
    auto *fieldLinesCheck = new QCheckBox(QStringLiteral("Field lines"), ui->Electrostatique);
    fieldLinesCheck->setToolTip(QStringLiteral("Integrate projected electric-field trajectories in the active plane slice."));
    visualizationToolbar->addWidget(scalarLabel);
    visualizationToolbar->addWidget(scalarCombo);
    visualizationToolbar->addSpacing(12);
    visualizationToolbar->addWidget(contoursCheck);
    visualizationToolbar->addWidget(fieldLinesCheck);
    visualizationToolbar->addStretch(1);
    ui->canvasColumnLayout->insertLayout(1, visualizationToolbar);

    // Additive 3D visualization: the existing ElectrostaticCanvas remains the exact 2D widget.
    // Both tabs observe the same ElectrostaticModel; no 2D rendering or physics code is replaced.
    m_electrostatic3D = new ElectrostaticField3DView(ui->Electrostatique);
    m_electrostatic3D->setModel(m_electrostaticModel.get());
    m_electrostatic3D->setShowFieldVectors(ui->showFieldVectorsCheck->isChecked());
    m_electrostatic3D->setEditPlane(ui->electroPlaneCombo->currentIndex());
    m_electrostatic3D->sourceSelected = [this](int index) {
        if (index >= 0 && index < ui->sourceCombo->count() && ui->sourceCombo->currentIndex() != index)
            ui->sourceCombo->setCurrentIndex(index);
    };
    m_electrostatic3D->sourceMoveRequested = [this](int index, const ElectrostaticVec3 &position) {
        const auto *current = m_electrostaticModel->source(index);
        if (!current) return;
        ElectrostaticSource edited = *current;
        edited.position = position;
        m_electrostaticModel->updateSource(index, edited);
        if (index == m_selectedSource)
        {
            const QSignalBlocker bx(ui->sourceXSpin), by(ui->sourceYSpin), bz(ui->sourceZSpin);
            ui->sourceXSpin->setValue(position.x); ui->sourceYSpin->setValue(position.y); ui->sourceZSpin->setValue(position.z);
        }
        updateMeasurementResults();
    };
    m_electrostatic3D->sourceCreateRequested = [this](int typeIndex, const ElectrostaticVec3 &position) {
        const SourceType type=indexToType(typeIndex);ElectrostaticSource source;source.type=type;source.position=position;source.name=QStringLiteral("%1 %2").arg(ElectrostaticModel::typeName(type)).arg(m_electrostaticModel->sourceCount()+1);source.strength=1.0e-9;source.radius=0.35;source.innerRadius=0.15;source.length=1.5;source.width=1.5;source.height=0.8;source.thickness=0.1;
        const int index=m_electrostaticModel->addSource(source);refreshSourceList(index);updateMeasurementResults();
    };
    m_electrostatic3D->sourceDeleteRequested = [this](int index) {
        if(index<0||index>=m_electrostaticModel->sourceCount())return;m_electrostaticModel->removeSource(index);refreshSourceList(std::min(index,m_electrostaticModel->sourceCount()-1));updateMeasurementResults();
    };
    m_electroViewTabs = new QTabWidget(ui->Electrostatique);
    m_electroViewTabs->setDocumentMode(true);
    if (QLayoutItem *oldCanvasItem = ui->canvasColumnLayout->replaceWidget(ui->electrostaticCanvas, m_electroViewTabs))
        delete oldCanvasItem;
    m_electroViewTabs->addTab(ui->electrostaticCanvas, QStringLiteral("2D field view"));
    m_electroViewTabs->addTab(m_electrostatic3D, QStringLiteral("3D field view"));
    m_electroViewTabs->setTabToolTip(0, QStringLiteral("Reference 2D slice view of the electrostatic field."));
    m_electroViewTabs->setTabToolTip(1, QStringLiteral("Spatial view of the same electrostatic model. Drag sources to move them; right-click directly in the view to create/delete sources; Delete removes the selected source."));

    connect(ui->showFieldVectorsCheck, &QCheckBox::toggled, m_electrostatic3D, &ElectrostaticField3DView::setShowFieldVectors);
    connect(ui->centerViewButton, &QPushButton::clicked, m_electrostatic3D, &ElectrostaticField3DView::centerView);

    connect(scalarCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, contoursCheck](int index) {
        ui->electrostaticCanvas->setScalarMap(static_cast<ElectrostaticCanvas::ScalarMap>(std::clamp(index, 0, 2)));
        contoursCheck->setEnabled(index != 0);
    });
    connect(contoursCheck, &QCheckBox::toggled, ui->electrostaticCanvas, &ElectrostaticCanvas::setShowContours);
    connect(fieldLinesCheck, &QCheckBox::toggled, ui->electrostaticCanvas, &ElectrostaticCanvas::setShowFieldLines);

    connect(m_electrostaticModel.get(), &ElectrostaticModel::changed, this, [this] {
        if (m_electrostatic3D) m_electrostatic3D->update();
        updateMeasurementResults();
    });

    // Start with a small symmetric example in nanocoulombs.
    const struct Seed { double q; double x; double y; } seeds[] = {
        {+4.0e-9, +1.0, +1.0},
        {-1.0e-9, +1.0, -1.0},
        {+5.0e-9, -1.0, +1.0},
        {-3.0e-9, -1.0, -1.0}
    };
    int number = 1;
    for (const auto &seed : seeds)
    {
        ElectrostaticSource source;
        source.name = QStringLiteral("Q%1").arg(number++);
        source.type = SourceType::PointCharge;
        source.position = {seed.x, seed.y, 0.0};
        source.strength = seed.q;
        m_electrostaticModel->addSource(source);
    }

    ui->measureXSpin->setValue(0.0);
    ui->measureYSpin->setValue(0.0);
    ui->measureZSpin->setValue(0.0);
    ui->testChargeSpin->setValue(1.0);
    ui->showFieldVectorsCheck->setChecked(true);

    refreshSourceList(0);
    setMeasurementFromEditors();
}

void MainWindow::initializeMagnetostaticAndReferenceWorkspaces()
{
    // Build the magnetostatic editor in the existing Designer page first; both
    // legacy .ui pages are then regrouped below under one first-class
    // "Field studies" workspace.
    auto *magneticLayout = new QVBoxLayout(ui->Signal);
    magneticLayout->setContentsMargins(0, 0, 0, 0);
    auto *magnetostaticWorkspace = new MagnetostaticWorkspace(ui->Signal);
    m_magnetostaticWorkspace = magnetostaticWorkspace;
    magneticLayout->addWidget(magnetostaticWorkspace);

    // Profiles and point probes now live exactly where the field graphics live.
    // Each static-field workspace gets only the quantities belonging to its
    // model instead of the former mixed top-level "Sondes & coupes" page.
    if (m_electroViewTabs)
    {
        auto *profile = createElectrostaticProfilePage(m_electrostaticModel.get(), ui->electrostaticCanvas, m_electroViewTabs);
        auto *probes = createElectrostaticPointProbesPage(m_electrostaticModel.get(), ui->electrostaticCanvas, m_electroViewTabs);
        const int profileIndex = m_electroViewTabs->addTab(profile, QStringLiteral("Electrostatic profile"));
        const int probesIndex = m_electroViewTabs->addTab(probes, QStringLiteral("Point probes"));
        m_electroViewTabs->setTabToolTip(profileIndex, QStringLiteral("Sample E or V along a 3D A→B segment; A/B remain draggable on the 2D field view."));
        m_electroViewTabs->setTabToolTip(probesIndex, QStringLiteral("Electrostatic-only point probes: |E|, Ex, Ey, Ez and V."));
    }
    if (auto *tabs = magnetostaticWorkspace->fieldViewTabs())
    {
        auto *profile = createMagnetostaticProfilePage(magnetostaticWorkspace->model(), magnetostaticWorkspace->canvas(), tabs);
        auto *probes = createMagnetostaticPointProbesPage(magnetostaticWorkspace->model(), magnetostaticWorkspace->canvas(), tabs);
        const int profileIndex = tabs->addTab(profile, QStringLiteral("Magnetostatic profile"));
        const int probesIndex = tabs->addTab(probes, QStringLiteral("Point probes"));
        tabs->setTabToolTip(profileIndex, QStringLiteral("Sample B or H along a 3D A→B segment; A/B remain draggable on the 2D field view."));
        tabs->setTabToolTip(probesIndex, QStringLiteral("Magnetostatic-only point probes: |B|, Bx, By, Bz and |H|."));
    }

    // Group Electrostatics and Magnetostatics under a single top-level workspace.
    // Remove the old top-level tabs without deleting their pages, then reparent
    // both pages into the new inner tab widget.
    const int signalIndex = ui->tabWidget->indexOf(ui->Signal);
    const int electroIndex = ui->tabWidget->indexOf(ui->Electrostatique);
    int insertIndex = 0;
    if (signalIndex >= 0 && electroIndex >= 0)
        insertIndex = std::min(signalIndex, electroIndex);
    else if (signalIndex >= 0)
        insertIndex = signalIndex;
    else if (electroIndex >= 0)
        insertIndex = electroIndex;

    if (signalIndex >= 0 && electroIndex >= 0)
    {
        ui->tabWidget->removeTab(std::max(signalIndex, electroIndex));
        ui->tabWidget->removeTab(std::min(signalIndex, electroIndex));
    }
    else
    {
        if (signalIndex >= 0) ui->tabWidget->removeTab(signalIndex);
        if (electroIndex >= 0) ui->tabWidget->removeTab(electroIndex);
    }

    m_fieldStudiesWorkspace = new QWidget(ui->tabWidget);
    auto *fieldStudiesLayout = new QVBoxLayout(m_fieldStudiesWorkspace);
    fieldStudiesLayout->setContentsMargins(0, 0, 0, 0);
    m_fieldStudiesTabs = new QTabWidget(m_fieldStudiesWorkspace);
    m_fieldStudiesTabs->setDocumentMode(true);
    ui->Electrostatique->setParent(m_fieldStudiesTabs);
    ui->Signal->setParent(m_fieldStudiesTabs);
    m_fieldStudiesTabs->addTab(ui->Electrostatique, QStringLiteral("Electrostatics"));
    m_fieldStudiesTabs->addTab(ui->Signal, QStringLiteral("Magnetostatics"));
    m_fieldStudiesTabs->setTabToolTip(0, QStringLiteral("Static electric fields, potential, force, profiles and electrostatic point probes."));
    m_fieldStudiesTabs->setTabToolTip(1, QStringLiteral("Static magnetic fields, Lorentz force, winding estimates, profiles and magnetostatic point probes."));
    fieldStudiesLayout->addWidget(m_fieldStudiesTabs, 1);
    const int fieldStudiesIndex = ui->tabWidget->insertTab(insertIndex, m_fieldStudiesWorkspace, QStringLiteral("Field studies"));
    ui->tabWidget->setTabToolTip(fieldStudiesIndex, QStringLiteral("Electrostatics and magnetostatics studies grouped in one workspace."));
    ui->tabWidget->setCurrentIndex(fieldStudiesIndex);
    m_fieldStudiesTabs->setCurrentWidget(ui->Electrostatique);

    auto *calculator = new EmCalculatorWidget(ui->tabWidget);
    m_calculatorWorkspace = calculator;
    const int calculatorTab = ui->tabWidget->addTab(calculator, QStringLiteral("Calculator / Notes"));
    ui->tabWidget->setTabToolTip(calculatorTab, QStringLiteral("Wave/frequency conversion, antenna starting dimensions, EM scientific calculator and persistent plain-text notes."));

    auto *reference = new ElectromagnetismReferenceWidget(ui->tabWidget);
    m_referenceWorkspace = reference;
    ui->tabWidget->addTab(reference, QStringLiteral("Equations & references"));

    // The antenna CAD workspace is a first-class main workspace rather than a nested
    // RF/EM calculator tab. This gives the designer the full application width/height
    // and still lets the RF-chain workspace consume its impedance/sweep signals.
    auto *antennaDesigner = new AntennaDesignerWidget(ui->tabWidget);
    m_antennaDesignerWorkspace = antennaDesigner;
    const int antennaDesignerTab = ui->tabWidget->addTab(antennaDesigner, QStringLiteral("Antenna designer"));
    ui->tabWidget->setTabToolTip(antennaDesignerTab, QStringLiteral("3D antenna CAD, geometry, presets, full-wave educational solvers and results."));
    calculator->setCreateAntennaHandler([this, antennaDesigner](EmCalculator::AntennaKind kind, const EmCalculator::AntennaInputs &inputs, QString &error) {
        const bool ok = antennaDesigner->createFromCalculator(kind, inputs, &error);
        if (ok) ui->tabWidget->setCurrentWidget(antennaDesigner);
        return ok;
    });

    auto *engineering = new ElectromagneticEngineeringWorkspace(antennaDesigner, ui->tabWidget);
    m_engineeringWorkspace = engineering;
    const int engineeringTab = ui->tabWidget->addTab(engineering, QStringLiteral("RF / EM / components  ⓘ"));
    ui->tabWidget->setTabToolTip(engineeringTab, QStringLiteral(
        "<qt><b>RF / EM / components</b><br/><br/>"
        "This workspace combines time-harmonic electromagnetic models and engineering calculators. "
        "Closed-form approximations state their validity range. Arbitrary antenna impedance or arbitrary electrostatic "
        "capacitance requires a full-wave, BEM or FEM solver and is not silently approximated."
        "</qt>"));

    // Enlarge the electrostatic field-view column; the former explanatory footer
    // is replaced by the plane selector and the in-place analysis tabs above.
    ui->canvasColumnLayout->setStretch(2, 1);
    ui->electrostaticLayout->setStretch(1, 1);
    ui->electrostaticCanvas->setMinimumSize(650, 500);
    connect(ui->electroPlaneCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        ui->electrostaticCanvas->setViewPlane(static_cast<FieldViewPlane>(std::clamp(index, 0, 2)));
        if (m_electrostatic3D) m_electrostatic3D->setEditPlane(index);
    });
}


void MainWindow::initializeMenuBar()
{
    auto *fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    auto *exitAction = fileMenu->addAction(QStringLiteral("E&xit"));
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    auto *viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    auto *appearanceMenu = viewMenu->addMenu(QStringLiteral("&Appearance"));

    UiStyle::Settings settings = UiStyle::loadSettings();
    auto settingsState = std::make_shared<UiStyle::Settings>(settings);
    auto applySettings = [this, settingsState] {
        UiStyle::saveSettings(*settingsState);
        if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance()))
            UiStyle::apply(*app, *settingsState);
        statusBar()->showMessage(QStringLiteral("Appearance: %1 / %2 / %3")
                                     .arg(UiStyle::themeName(settingsState->theme),
                                          UiStyle::accentName(settingsState->accent),
                                          UiStyle::densityName(settingsState->density)), 3500);
    };

    auto *themeMenu = appearanceMenu->addMenu(QStringLiteral("Theme"));
    auto *themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);
    const struct ThemeEntry { const char *text; UiStyle::ThemeMode mode; } themes[] = {
        {"System", UiStyle::ThemeMode::System}, {"Light", UiStyle::ThemeMode::Light}, {"Dark", UiStyle::ThemeMode::Dark}
    };
    for (const auto &entry : themes)
    {
        auto *action = themeMenu->addAction(QString::fromLatin1(entry.text));
        action->setCheckable(true);
        action->setChecked(settings.theme == entry.mode);
        themeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [settingsState, applySettings, mode = entry.mode] {
            settingsState->theme = mode;
            applySettings();
        });
    }

    auto *accentMenu = appearanceMenu->addMenu(QStringLiteral("Accent color"));
    auto *accentGroup = new QActionGroup(this);
    accentGroup->setExclusive(true);
    const struct AccentEntry { const char *text; UiStyle::Accent accent; } accents[] = {
        {"Blue", UiStyle::Accent::Blue}, {"Teal", UiStyle::Accent::Teal}, {"Amber", UiStyle::Accent::Amber}, {"Purple", UiStyle::Accent::Purple}
    };
    for (const auto &entry : accents)
    {
        auto *action = accentMenu->addAction(QString::fromLatin1(entry.text));
        action->setCheckable(true);
        action->setChecked(settings.accent == entry.accent);
        accentGroup->addAction(action);
        connect(action, &QAction::triggered, this, [settingsState, applySettings, accent = entry.accent] {
            settingsState->accent = accent;
            applySettings();
        });
    }

    auto *densityMenu = appearanceMenu->addMenu(QStringLiteral("Control density"));
    auto *densityGroup = new QActionGroup(this);
    densityGroup->setExclusive(true);
    const struct DensityEntry { const char *text; UiStyle::Density density; } densities[] = {
        {"Compact", UiStyle::Density::Compact}, {"Comfortable", UiStyle::Density::Comfortable}, {"Spacious", UiStyle::Density::Spacious}
    };
    for (const auto &entry : densities)
    {
        auto *action = densityMenu->addAction(QString::fromLatin1(entry.text));
        action->setCheckable(true);
        action->setChecked(settings.density == entry.density);
        densityGroup->addAction(action);
        connect(action, &QAction::triggered, this, [settingsState, applySettings, density = entry.density] {
            settingsState->density = density;
            applySettings();
        });
    }

    appearanceMenu->addSeparator();
    auto *fontSmaller = appearanceMenu->addAction(QStringLiteral("Smaller UI font"));
    fontSmaller->setShortcut(QKeySequence(QStringLiteral("Ctrl+-")));
    auto *fontLarger = appearanceMenu->addAction(QStringLiteral("Larger UI font"));
    fontLarger->setShortcut(QKeySequence(QStringLiteral("Ctrl++")));
    auto *fontReset = appearanceMenu->addAction(QStringLiteral("Reset UI font size"));
    connect(fontSmaller, &QAction::triggered, this, [settingsState, applySettings] {
        settingsState->fontPointDelta = std::max(-3, settingsState->fontPointDelta - 1);
        applySettings();
    });
    connect(fontLarger, &QAction::triggered, this, [settingsState, applySettings] {
        settingsState->fontPointDelta = std::min(6, settingsState->fontPointDelta + 1);
        applySettings();
    });
    connect(fontReset, &QAction::triggered, this, [settingsState, applySettings] {
        settingsState->fontPointDelta = 0;
        applySettings();
    });

    auto *optionsMenu = menuBar()->addMenu(QStringLiteral("&Options"));
    auto *antennaOptionsMenu = optionsMenu->addMenu(QStringLiteral("Antenna designer"));
    auto *showWireRadius = antennaOptionsMenu->addAction(QStringLiteral("Show physical wire thickness (2r)"));
    showWireRadius->setCheckable(true);
    {
        QSettings appSettings;
        const bool enabled=appSettings.value(QStringLiteral("antennaDesigner/showPhysicalWireRadius"),false).toBool();
        showWireRadius->setChecked(enabled);
        if(auto *designer=qobject_cast<AntennaDesignerWidget *>(m_antennaDesignerWorkspace))designer->setShowPhysicalWireRadius(enabled);
    }
    showWireRadius->setToolTip(QStringLiteral("Render each antenna wire with its physical diameter in the 2D and 3D CAD views. A thin centerline remains visible/selectable for very small radii."));
    connect(showWireRadius,&QAction::toggled,this,[this](bool enabled){
        QSettings appSettings;appSettings.setValue(QStringLiteral("antennaDesigner/showPhysicalWireRadius"),enabled);
        if(auto *designer=qobject_cast<AntennaDesignerWidget *>(m_antennaDesignerWorkspace))designer->setShowPhysicalWireRadius(enabled);
        statusBar()->showMessage(enabled?QStringLiteral("Antenna designer: physical wire thickness enabled."):QStringLiteral("Antenna designer: centerline wire rendering enabled."),2500);
    });

    auto *simulationMenu = menuBar()->addMenu(QStringLiteral("&Simulation"));
    auto *showElectro = simulationMenu->addAction(QStringLiteral("Electrostatics workspace"));
    auto *showMagnetic = simulationMenu->addAction(QStringLiteral("Magnetostatics workspace"));
    auto *showAntenna = simulationMenu->addAction(QStringLiteral("Antenna designer workspace"));
    auto *showRf = simulationMenu->addAction(QStringLiteral("RF / EM / components workspace"));
    connect(showElectro, &QAction::triggered, this, [this] {
        if (m_fieldStudiesWorkspace) ui->tabWidget->setCurrentWidget(m_fieldStudiesWorkspace);
        if (m_fieldStudiesTabs) m_fieldStudiesTabs->setCurrentWidget(ui->Electrostatique);
    });
    connect(showMagnetic, &QAction::triggered, this, [this] {
        if (m_fieldStudiesWorkspace) ui->tabWidget->setCurrentWidget(m_fieldStudiesWorkspace);
        if (m_fieldStudiesTabs) m_fieldStudiesTabs->setCurrentWidget(ui->Signal);
    });
    connect(showAntenna, &QAction::triggered, this, [this] { if (m_antennaDesignerWorkspace) ui->tabWidget->setCurrentWidget(m_antennaDesignerWorkspace); });
    connect(showRf, &QAction::triggered, this, [this] { if (m_engineeringWorkspace) ui->tabWidget->setCurrentWidget(m_engineeringWorkspace); });

    auto *toolsMenu = menuBar()->addMenu(QStringLiteral("&Tools"));
    auto *resetAppearance = toolsMenu->addAction(QStringLiteral("Reset appearance settings"));
    connect(resetAppearance, &QAction::triggered, this, [this, settingsState] {
        if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance()))
        {
            UiStyle::resetToDefaults(*app);
            *settingsState = UiStyle::loadSettings();
            statusBar()->showMessage(QStringLiteral("Appearance settings reset. Restart is not required."), 3500);
        }
    });

    auto *helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    auto *calculatorAction = helpMenu->addAction(QStringLiteral("Engineering calculator"));
    connect(calculatorAction, &QAction::triggered, this, [this] {
        if (m_calculatorWorkspace) ui->tabWidget->setCurrentWidget(m_calculatorWorkspace);
    });
    auto *referenceAction = helpMenu->addAction(QStringLiteral("Equations && references"));
    referenceAction->setShortcut(QKeySequence(QStringLiteral("F1")));
    connect(referenceAction, &QAction::triggered, this, [this] {
        if (m_referenceWorkspace) ui->tabWidget->setCurrentWidget(m_referenceWorkspace);
    });
    helpMenu->addSeparator();
    auto *aboutAction = helpMenu->addAction(QStringLiteral("About QTsignalApp"));
    connect(aboutAction, &QAction::triggered, this, [this] {
        QMessageBox::about(this, QStringLiteral("About QTsignalApp"),
                           QStringLiteral("QTsignalApp %1\nElectromagnetism Field Explorer\n\nStatic-field workspaces, RF/EM engineering tools, numerical BEM/MoM/FDTD solvers, generalized 3D thin-wire antennas, PEC/RWG surfaces, self-consistent hybrid wire + surface MoM, wire/PEC galvanic junctions, effective dielectric substrates, printed microstrip/inset-patch design starts, broadband impedance/S11/VSWR/Smith analysis, radiation patterns, VNA tools, a copy-friendly wave/antenna/EM engineering calculator and configurable UI appearance.\n\nThe advanced full-wave solvers are educational engineering models; convergence studies and external validation remain required for critical designs.")
                               .arg(QApplication::applicationVersion()));
    });
}

void MainWindow::addSource(ElectrostaticSource::Type type)
{
    const ElectrostaticVec3 measure = ui->electrostaticCanvas->measurementPoint();
    ElectrostaticSource source;
    source.type = type;
    source.position = measure;
    source.name = QStringLiteral("%1 %2")
                      .arg(ElectrostaticModel::typeName(type))
                      .arg(m_electrostaticModel->sourceCount() + 1);
    source.strength = type == SourceType::InfiniteCylinderZ ? 1.0e-9 : 1.0e-9;
    source.radius = 0.35;
    source.innerRadius = 0.15;
    source.length = 1.5;
    source.width = 1.5;
    source.height = 0.8;
    source.thickness = 0.1;

    const int index = m_electrostaticModel->addSource(source);
    refreshSourceList(index);
}

void MainWindow::refreshSourceList(int selectIndex)
{
    m_loadingEditor = true;
    const QSignalBlocker blocker(ui->sourceCombo);
    ui->sourceCombo->clear();
    for (int i = 0; i < m_electrostaticModel->sourceCount(); ++i)
    {
        const auto *source = m_electrostaticModel->source(i);
        ui->sourceCombo->addItem(source ? source->name : QStringLiteral("Source %1").arg(i + 1));
    }

    if (selectIndex == -2)
        selectIndex = m_selectedSource;
    if (ui->sourceCombo->count() == 0)
        selectIndex = -1;
    else
        selectIndex = std::clamp(selectIndex, 0, ui->sourceCombo->count() - 1);

    ui->sourceCombo->setCurrentIndex(selectIndex);
    m_loadingEditor = false;
    m_selectedSource = selectIndex;
    ui->electrostaticCanvas->setSelectedSource(selectIndex);
    if (m_electrostatic3D) m_electrostatic3D->setSelectedSource(selectIndex);
    loadSourceEditor(selectIndex);
}

void MainWindow::loadSourceEditor(int index)
{
    m_loadingEditor = true;
    const auto *source = m_electrostaticModel->source(index);
    const bool enabled = source != nullptr;
    ui->sourceEditorGroup->setEnabled(enabled);
    ui->removeSourceButton->setEnabled(enabled);
    ui->previousSourceButton->setEnabled(m_electrostaticModel->sourceCount() > 1);
    ui->nextSourceButton->setEnabled(m_electrostaticModel->sourceCount() > 1);

    if (!source)
    {
        m_loadingEditor = false;
        return;
    }

    ui->sourceNameEdit->setText(source->name);
    ui->sourceTypeCombo->setCurrentIndex(typeToIndex(source->type));
    updateStrengthUnitLabels();
    const double scale = chargeUnitScale(ui->strengthUnitCombo);
    ui->sourceStrengthSpin->setValue(source->strength / scale);
    ui->sourceXSpin->setValue(source->position.x);
    ui->sourceYSpin->setValue(source->position.y);
    ui->sourceZSpin->setValue(source->position.z);
    ui->sourceRadiusSpin->setValue(source->radius);
    ui->sourceInnerRadiusSpin->setValue(source->innerRadius);
    ui->sourceLengthSpin->setValue(source->length);
    ui->sourceWidthSpin->setValue(source->width);
    ui->sourceHeightSpin->setValue(source->height);
    ui->sourceThicknessSpin->setValue(source->thickness);
    ui->sourceAngleSpin->setValue(source->angleDeg);
    updateSourceEditorVisibility();
    m_loadingEditor = false;
}

void MainWindow::applySourceEditor()
{
    if (m_selectedSource < 0)
        return;

    const auto *current = m_electrostaticModel->source(m_selectedSource);
    if (!current)
        return;

    ElectrostaticSource source = *current;
    source.name = ui->sourceNameEdit->text().trimmed();
    if (source.name.isEmpty())
        source.name = QStringLiteral("Source %1").arg(m_selectedSource + 1);
    source.type = indexToType(ui->sourceTypeCombo->currentIndex());
    source.strength = ui->sourceStrengthSpin->value() * chargeUnitScale(ui->strengthUnitCombo);
    source.position = {ui->sourceXSpin->value(), ui->sourceYSpin->value(), ui->sourceZSpin->value()};
    source.radius = std::max(1.0e-6, ui->sourceRadiusSpin->value());
    source.innerRadius = std::clamp(ui->sourceInnerRadiusSpin->value(), 0.0, source.radius * (1.0 - 1e-9));
    source.length = std::max(1.0e-6, ui->sourceLengthSpin->value());
    source.width = std::max(1.0e-6, ui->sourceWidthSpin->value());
    source.height = std::max(1.0e-6, ui->sourceHeightSpin->value());
    source.thickness = std::max(1.0e-6, ui->sourceThicknessSpin->value());
    source.angleDeg = ui->sourceAngleSpin->value();

    m_electrostaticModel->updateSource(m_selectedSource, source);
    refreshSourceList(m_selectedSource);
}

void MainWindow::updateSourceEditorVisibility()
{
    const SourceType type = indexToType(ui->sourceTypeCombo->currentIndex());
    const bool radius = type == SourceType::SolidSphere || type == SourceType::SphericalShell ||
                        type == SourceType::ThickSphericalShell || type == SourceType::InfiniteCylinderZ ||
                        type == SourceType::InfiniteHollowCylinderZ || type == SourceType::CircularPlate ||
                        type == SourceType::AnnularPlate;
    const bool innerRadius = type == SourceType::ThickSphericalShell ||
                             type == SourceType::InfiniteHollowCylinderZ ||
                             type == SourceType::AnnularPlate;
    const bool length = type == SourceType::FiniteLine;
    const bool widthHeight = type == SourceType::RectangularPlate || type == SourceType::RectangularVolume || type == SourceType::TriangularPlate;
    const bool thickness = type == SourceType::RectangularVolume;
    const bool finitePlate = widthHeight || type == SourceType::CircularPlate || type == SourceType::AnnularPlate;
    const bool angle = length || finitePlate;

    ui->radiusLabel->setVisible(radius);
    ui->sourceRadiusSpin->setVisible(radius);
    ui->innerRadiusLabel->setVisible(innerRadius);
    ui->sourceInnerRadiusSpin->setVisible(innerRadius);
    ui->lengthLabel->setVisible(length);
    ui->sourceLengthSpin->setVisible(length);
    ui->widthLabel->setVisible(widthHeight);
    ui->sourceWidthSpin->setVisible(widthHeight);
    ui->heightLabel->setVisible(widthHeight);
    ui->sourceHeightSpin->setVisible(widthHeight);
    ui->thicknessLabel->setVisible(thickness);
    ui->sourceThicknessSpin->setVisible(thickness);
    ui->angleLabel->setVisible(angle);
    ui->sourceAngleSpin->setVisible(angle);
}

void MainWindow::updateStrengthUnitLabels()
{
    const SourceType type = indexToType(ui->sourceTypeCombo->currentIndex());
    const QString prefix = ui->strengthUnitCombo->currentText();
    const QString base = ElectrostaticModel::strengthUnit(type);
    ui->strengthLabel->setText(QStringLiteral("%1 (%2%3)")
                                   .arg(ElectrostaticModel::strengthSymbol(type), prefix, base));

    QString assumption;
    switch (type)
    {
    case SourceType::PointCharge:
        assumption = QStringLiteral("Q is the point charge located at X/Y/Z.");
        break;
    case SourceType::SolidSphere:
        assumption = QStringLiteral("Q is distributed uniformly through the full sphere volume. Exact Gauss-law field and potential are used.");
        break;
    case SourceType::SphericalShell:
        assumption = QStringLiteral("Q is distributed on an infinitesimally thin spherical surface. The electric field is exactly zero inside.");
        break;
    case SourceType::ThickSphericalShell:
        assumption = QStringLiteral("Q is distributed uniformly between the inner and outer radii. The concentric hollow core contains no charge; the field inside the cavity is exactly zero.");
        break;
    case SourceType::InfiniteCylinderZ:
        assumption = QStringLiteral("lambda [C/m] is distributed uniformly over the solid circular cross-section. Exact Gauss-law E is used; absolute potential needs a reference radius.");
        break;
    case SourceType::InfiniteHollowCylinderZ:
        assumption = QStringLiteral("lambda [C/m] is distributed uniformly in the annulus between inner and outer radii. The concentric cavity is uncharged.");
        break;
    case SourceType::InfiniteLine:
        assumption = QStringLiteral("lambda [C/m] is an ideal infinite line charge parallel to Z. E=lambda/(2*pi*eps0*rho); absolute potential is reference-dependent.");
        break;
    case SourceType::FiniteLine:
        assumption = QStringLiteral("Q is the TOTAL charge distributed uniformly over the finite wire: lambda = Q/L. The exact finite-line field is used.");
        break;
    case SourceType::RectangularPlate:
        assumption = QStringLiteral("Q is the TOTAL charge distributed uniformly over the infinitesimally thin rectangular plate: sigma = Q/(W H). Off-axis E and V use numerical surface integration.");
        break;
    case SourceType::RectangularVolume:
        assumption = QStringLiteral("Q is the TOTAL charge distributed uniformly through a finite rectangular slab of width W, height H and thickness t: rho = Q/(W H t). E and V use 3D Gauss-Legendre volume integration. Thickness is along local Z; Angle XY rotates the slab around Z.");
        break;
    case SourceType::CircularPlate:
        assumption = QStringLiteral("Q is distributed uniformly over a circular disk of outer radius R. Off-axis E and V use equal-area polar integration.");
        break;
    case SourceType::AnnularPlate:
        assumption = QStringLiteral("Q is distributed uniformly over an annulus between inner and outer radii. The central circular region is uncharged.");
        break;
    case SourceType::TriangularPlate:
        assumption = QStringLiteral("Q is distributed uniformly over an isosceles triangular plate of the specified width and height. The source position is its centroid.");
        break;
    case SourceType::InfinitePlane:
        assumption = QStringLiteral("sigma [C/m²] is the surface charge density of an ideal infinite XY plane. |E|=|sigma|/(2 eps0) on either side; absolute potential is reference-dependent.");
        break;
    }
    ui->strengthLabel->setToolTip(assumption);
    ui->sourceStrengthSpin->setToolTip(assumption);
}

void MainWindow::setMeasurementFromEditors()
{
    ElectrostaticVec3 point{ui->measureXSpin->value(), ui->measureYSpin->value(), ui->measureZSpin->value()};
    ui->electrostaticCanvas->setMeasurementPoint(point);
    if (m_electrostatic3D) m_electrostatic3D->setMeasurementPoint(point);
    updateMeasurementResults();
}

void MainWindow::updateMeasurementResults()
{
    const ElectrostaticVec3 point{ui->measureXSpin->value(), ui->measureYSpin->value(), ui->measureZSpin->value()};
    const ElectrostaticFieldResult field = m_electrostaticModel->fieldAt(point);
    const double eNorm = field.electricField.norm();

    ui->eMagnitudeLabel->setText(formatEngineering(eNorm, QStringLiteral("V/m")));
    ui->exLabel->setText(formatEngineering(field.electricField.x, QStringLiteral("V/m")));
    ui->eyLabel->setText(formatEngineering(field.electricField.y, QStringLiteral("V/m")));
    ui->ezLabel->setText(formatEngineering(field.electricField.z, QStringLiteral("V/m")));

    if (field.potentialDefined)
        ui->potentialLabel->setText(formatEngineering(field.potential, QStringLiteral("V")));
    else
        ui->potentialLabel->setText(QStringLiteral("n/a (reference-dependent)"));

    const double testCharge = ui->testChargeSpin->value() * chargeUnitScale(ui->testChargeUnitCombo);
    const ElectrostaticVec3 force = field.electricField * testCharge;
    ui->forceMagnitudeLabel->setText(formatEngineering(force.norm(), QStringLiteral("N")));
    ui->fxLabel->setText(formatEngineering(force.x, QStringLiteral("N")));
    ui->fyLabel->setText(formatEngineering(force.y, QStringLiteral("N")));
    ui->fzLabel->setText(formatEngineering(force.z, QStringLiteral("N")));

    if (field.singular)
        ui->measurementNoteLabel->setText(QStringLiteral("Singularity: move the measurement point away from the source."));
    else
        ui->measurementNoteLabel->setText(field.note);
}

double MainWindow::chargeUnitScale(const QComboBox *combo) const
{
    const QString unit = combo->currentText();
    if (unit.startsWith(QLatin1Char('n'))) return 1.0e-9;
    if (unit.startsWith(QLatin1Char('u')) || unit.startsWith(QChar(0x00B5))) return 1.0e-6;
    if (unit.startsWith(QLatin1Char('m'))) return 1.0e-3;
    return 1.0;
}

QString MainWindow::formatEngineering(double value, const QString &unit)
{
    if (!std::isfinite(value))
        return QStringLiteral("n/a");
    if (value == 0.0)
        return QStringLiteral("0 %1").arg(unit);

    struct Prefix { int exponent; const char *symbol; };
    static const Prefix prefixes[] = {
        {-12, "p"}, {-9, "n"}, {-6, "u"}, {-3, "m"},
        {0, ""}, {3, "k"}, {6, "M"}, {9, "G"}, {12, "T"}
    };

    const double magnitude = std::abs(value);
    int exponent = int(std::floor(std::log10(magnitude) / 3.0)) * 3;
    exponent = std::clamp(exponent, -12, 12);
    const double scaled = value / std::pow(10.0, exponent);
    const char *symbol = "";
    for (const auto &prefix : prefixes)
        if (prefix.exponent == exponent) { symbol = prefix.symbol; break; }

    return QStringLiteral("%1 %2%3")
        .arg(scaled, 0, 'g', 6)
        .arg(QString::fromLatin1(symbol), unit);
}
