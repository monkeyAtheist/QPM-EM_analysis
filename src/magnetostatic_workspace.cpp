#include "widgets/magnetostatic_workspace.h"
#include "widgets/field_3d_views.h"
#include "widgets/magnetostatic_canvas.h"
#include "field_view_plane.h"
#include "em_engineering_model.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QTabWidget>

#include <algorithm>
#include <cmath>

namespace
{
using Type = MagnetostaticSource::Type;

QDoubleSpinBox *coordinateSpin(QWidget *parent)
{
    auto *spin = new QDoubleSpinBox(parent);
    spin->setDecimals(5);
    spin->setRange(-10000.0, 10000.0);
    return spin;
}

QDoubleSpinBox *positiveSpin(QWidget *parent, double initial)
{
    auto *spin = new QDoubleSpinBox(parent);
    spin->setDecimals(5);
    spin->setRange(1e-6, 10000.0);
    spin->setValue(initial);
    return spin;
}

QLabel *valueLabel(QWidget *parent)
{
    auto *label = new QLabel(QStringLiteral("—"), parent);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(true);
    return label;
}

int typeIndex(Type type) { return static_cast<int>(type); }
Type indexType(int index)
{
    return static_cast<Type>(std::clamp(index, 0, static_cast<int>(Type::InfiniteCurrentSheet)));
}
}

MagnetostaticWorkspace::MagnetostaticWorkspace(QWidget *parent)
    : QWidget(parent), m_model(std::make_unique<MagnetostaticModel>(this))
{
    buildUi();
    seedExample();
    refreshSourceList(0);
    updateMeasurementFromEditors();
}

void MagnetostaticWorkspace::buildUi()
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(360);
    scroll->setMaximumWidth(430);
    auto *controls = new QWidget(scroll);
    auto *controlsLayout = new QVBoxLayout(controls);

    auto *sourcesGroup = new QGroupBox(QStringLiteral("Magnetic sources"), controls);
    auto *sourcesLayout = new QVBoxLayout(sourcesGroup);
    m_sourceCombo = new QComboBox(sourcesGroup);
    sourcesLayout->addWidget(m_sourceCombo);
    auto *nav = new QHBoxLayout();
    m_previousButton = new QPushButton(QStringLiteral("◀ Previous"), sourcesGroup);
    m_nextButton = new QPushButton(QStringLiteral("Next ▶"), sourcesGroup);
    nav->addWidget(m_previousButton);
    nav->addWidget(m_nextButton);
    sourcesLayout->addLayout(nav);

    auto *addRow = new QHBoxLayout();
    m_newSourceTypeCombo = new QComboBox(sourcesGroup);
    const QList<Type> addTypes{
        Type::InfiniteWireZ, Type::FiniteWire,
        Type::SolidCurrentCylinderZ, Type::HollowCurrentCylinderZ,
        Type::CircularLoop, Type::RectangularLoop, Type::TriangularLoop,
        Type::Solenoid, Type::HelmholtzPair, Type::InfiniteCurrentSheet,
        Type::MagneticDipole
    };
    for (const Type type : addTypes)
        m_newSourceTypeCombo->addItem(MagnetostaticModel::typeName(type), static_cast<int>(type));
    m_newSourceTypeCombo->setToolTip(QStringLiteral("Choose a magnetostatic source model to add at the current measurement-point position."));
    m_addSourceButton = new QPushButton(QStringLiteral("+ Add"), sourcesGroup);
    m_removeButton = new QPushButton(QStringLiteral("Remove"), sourcesGroup);
    addRow->addWidget(m_newSourceTypeCombo, 1);
    addRow->addWidget(m_addSourceButton);
    addRow->addWidget(m_removeButton);
    sourcesLayout->addLayout(addRow);
    controlsLayout->addWidget(sourcesGroup);

    m_sourceEditorGroup = new QGroupBox(QStringLiteral("Selected source"), controls);
    auto *editor = new QFormLayout(m_sourceEditorGroup);
    m_nameEdit = new QLineEdit(m_sourceEditorGroup);
    m_typeCombo = new QComboBox(m_sourceEditorGroup);
    for (int i = 0; i <= static_cast<int>(Type::InfiniteCurrentSheet); ++i)
        m_typeCombo->addItem(MagnetostaticModel::typeName(static_cast<Type>(i)));
    m_strengthLabel = new QLabel(QStringLiteral("I (A)"), m_sourceEditorGroup);
    auto *strengthRow = new QWidget(m_sourceEditorGroup);
    auto *strengthLayout = new QHBoxLayout(strengthRow);
    strengthLayout->setContentsMargins(0,0,0,0);
    m_strengthSpin = new QDoubleSpinBox(strengthRow);
    m_strengthSpin->setDecimals(6);
    m_strengthSpin->setRange(-1e9, 1e9);
    m_strengthUnitCombo = new QComboBox(strengthRow);
    m_strengthUnitCombo->addItems({QStringLiteral("m"), QStringLiteral(""), QStringLiteral("k")});
    m_strengthUnitCombo->setCurrentIndex(1);
    m_strengthUnitCombo->setMaximumWidth(62);
    strengthLayout->addWidget(m_strengthSpin, 1);
    strengthLayout->addWidget(m_strengthUnitCombo);

    m_xSpin = coordinateSpin(m_sourceEditorGroup);
    m_ySpin = coordinateSpin(m_sourceEditorGroup);
    m_zSpin = coordinateSpin(m_sourceEditorGroup);
    m_radiusLabel = new QLabel(QStringLiteral("Outer radius (m)"), m_sourceEditorGroup);
    m_radiusSpin = positiveSpin(m_sourceEditorGroup, 0.5);
    m_innerRadiusLabel = new QLabel(QStringLiteral("Inner radius (m)"), m_sourceEditorGroup);
    m_innerRadiusSpin = new QDoubleSpinBox(m_sourceEditorGroup);
    m_innerRadiusSpin->setDecimals(5);
    m_innerRadiusSpin->setRange(0.0, 10000.0);
    m_innerRadiusSpin->setValue(0.25);
    m_widthLabel = new QLabel(QStringLiteral("Width (m)"), m_sourceEditorGroup);
    m_widthSpin = positiveSpin(m_sourceEditorGroup, 1.0);
    m_heightLabel = new QLabel(QStringLiteral("Height (m)"), m_sourceEditorGroup);
    m_heightSpin = positiveSpin(m_sourceEditorGroup, 0.7);
    m_lengthLabel = new QLabel(QStringLiteral("Length / separation (m)"), m_sourceEditorGroup);
    m_lengthSpin = positiveSpin(m_sourceEditorGroup, 2.0);
    m_turnsLabel = new QLabel(QStringLiteral("Turns"), m_sourceEditorGroup);
    m_turnsSpin = new QSpinBox(m_sourceEditorGroup);
    m_turnsSpin->setRange(1, 1000000);
    m_turnsSpin->setValue(100);
    m_wireDiameterLabel = new QLabel(QStringLiteral("Wire diameter (mm)"), m_sourceEditorGroup);
    m_wireDiameterSpin = positiveSpin(m_sourceEditorGroup, 0.8);
    m_wireDiameterSpin->setRange(0.001, 100000.0);
    m_radialBuildLabel = new QLabel(QStringLiteral("Radial build (mm)"), m_sourceEditorGroup);
    m_radialBuildSpin = positiveSpin(m_sourceEditorGroup, 0.8);
    m_radialBuildSpin->setRange(0.001, 100000.0);
    m_coreMuRLabel = new QLabel(QStringLiteral("Effective core μr"), m_sourceEditorGroup);
    m_coreMuRSpin = positiveSpin(m_sourceEditorGroup, 1.0);
    m_coreMuRSpin->setRange(0.000001, 1000000.0);
    m_azimuthLabel = new QLabel(QStringLiteral("Azimuth (°)"), m_sourceEditorGroup);
    m_azimuthSpin = new QDoubleSpinBox(m_sourceEditorGroup);
    m_azimuthSpin->setRange(-360.0, 360.0);
    m_azimuthSpin->setDecimals(2);
    m_elevationLabel = new QLabel(QStringLiteral("Elevation (°)"), m_sourceEditorGroup);
    m_elevationSpin = new QDoubleSpinBox(m_sourceEditorGroup);
    m_elevationSpin->setRange(-90.0, 90.0);
    m_elevationSpin->setDecimals(2);
    m_elevationSpin->setValue(90.0);
    m_applyButton = new QPushButton(QStringLiteral("Apply source parameters"), m_sourceEditorGroup);

    editor->addRow(QStringLiteral("Name"), m_nameEdit);
    editor->addRow(QStringLiteral("Type"), m_typeCombo);
    editor->addRow(m_strengthLabel, strengthRow);
    editor->addRow(QStringLiteral("X (m)"), m_xSpin);
    editor->addRow(QStringLiteral("Y (m)"), m_ySpin);
    editor->addRow(QStringLiteral("Z (m)"), m_zSpin);
    editor->addRow(m_radiusLabel, m_radiusSpin);
    editor->addRow(m_innerRadiusLabel, m_innerRadiusSpin);
    editor->addRow(m_widthLabel, m_widthSpin);
    editor->addRow(m_heightLabel, m_heightSpin);
    editor->addRow(m_lengthLabel, m_lengthSpin);
    editor->addRow(m_turnsLabel, m_turnsSpin);
    editor->addRow(m_wireDiameterLabel, m_wireDiameterSpin);
    editor->addRow(m_radialBuildLabel, m_radialBuildSpin);
    editor->addRow(m_coreMuRLabel, m_coreMuRSpin);
    editor->addRow(m_azimuthLabel, m_azimuthSpin);
    editor->addRow(m_elevationLabel, m_elevationSpin);
    editor->addRow(m_applyButton);
    controlsLayout->addWidget(m_sourceEditorGroup);

    auto *derivedGroup = new QGroupBox(QStringLiteral("Selected winding derived values"), controls);
    auto *derivedForm = new QFormLayout(derivedGroup);
    m_sourceInductance = valueLabel(derivedGroup);
    m_sourceWireResistance = valueLabel(derivedGroup);
    m_sourceStoredEnergy = valueLabel(derivedGroup);
    m_sourceDerivedNote = new QLabel(derivedGroup);
    m_sourceDerivedNote->setWordWrap(true);
    derivedForm->addRow(QStringLiteral("Estimated inductance"), m_sourceInductance);
    derivedForm->addRow(QStringLiteral("Copper resistance"), m_sourceWireResistance);
    derivedForm->addRow(QStringLiteral("Stored energy at I"), m_sourceStoredEnergy);
    derivedForm->addRow(m_sourceDerivedNote);
    controlsLayout->addWidget(derivedGroup);

    auto *measurementGroup = new QGroupBox(QStringLiteral("Measurement point M / test particle"), controls);
    auto *measurement = new QFormLayout(measurementGroup);
    m_measureX = coordinateSpin(measurementGroup);
    m_measureY = coordinateSpin(measurementGroup);
    m_measureZ = coordinateSpin(measurementGroup);
    m_testCharge = new QDoubleSpinBox(measurementGroup);
    m_testCharge->setDecimals(6);
    m_testCharge->setRange(-1e9, 1e9);
    m_testCharge->setValue(1.0);
    m_testChargeUnit = new QComboBox(measurementGroup);
    m_testChargeUnit->addItems({QStringLiteral("nC"), QStringLiteral("uC"), QStringLiteral("mC"), QStringLiteral("C")});
    m_testChargeUnit->setCurrentIndex(1);
    auto *qRow = new QWidget(measurementGroup);
    auto *qLayout = new QHBoxLayout(qRow);
    qLayout->setContentsMargins(0,0,0,0);
    qLayout->addWidget(m_testCharge, 1);
    qLayout->addWidget(m_testChargeUnit);
    m_velocityX = coordinateSpin(measurementGroup);
    m_velocityY = coordinateSpin(measurementGroup);
    m_velocityZ = coordinateSpin(measurementGroup);
    m_velocityX->setValue(1.0);
    measurement->addRow(QStringLiteral("X (m)"), m_measureX);
    measurement->addRow(QStringLiteral("Y (m)"), m_measureY);
    measurement->addRow(QStringLiteral("Z (m)"), m_measureZ);
    measurement->addRow(QStringLiteral("Test charge"), qRow);
    measurement->addRow(QStringLiteral("vx (m/s)"), m_velocityX);
    measurement->addRow(QStringLiteral("vy (m/s)"), m_velocityY);
    measurement->addRow(QStringLiteral("vz (m/s)"), m_velocityZ);
    controlsLayout->addWidget(measurementGroup);

    auto *results = new QGroupBox(QStringLiteral("Magnetic field / Lorentz force at M"), controls);
    auto *resultsForm = new QFormLayout(results);
    auto makeValue = [results]() {
        auto *label = new QLabel(QStringLiteral("0"), results);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return label;
    };
    m_bMagnitude = makeValue(); m_bx = makeValue(); m_by = makeValue(); m_bz = makeValue();
    m_hMagnitude = makeValue(); m_forceMagnitude = makeValue(); m_fx = makeValue(); m_fy = makeValue(); m_fz = makeValue();
    m_note = new QLabel(results); m_note->setWordWrap(true); m_note->setStyleSheet(QStringLiteral("color:#7a4b00;"));
    resultsForm->addRow(QStringLiteral("|B|"), m_bMagnitude);
    resultsForm->addRow(QStringLiteral("Bx"), m_bx);
    resultsForm->addRow(QStringLiteral("By"), m_by);
    resultsForm->addRow(QStringLiteral("Bz"), m_bz);
    resultsForm->addRow(QStringLiteral("|H|"), m_hMagnitude);
    resultsForm->addRow(QStringLiteral("|F|"), m_forceMagnitude);
    resultsForm->addRow(QStringLiteral("Fx"), m_fx);
    resultsForm->addRow(QStringLiteral("Fy"), m_fy);
    resultsForm->addRow(QStringLiteral("Fz"), m_fz);
    resultsForm->addRow(m_note);
    controlsLayout->addWidget(results);
    controlsLayout->addStretch(1);
    scroll->setWidget(controls);
    root->addWidget(scroll, 0);

    auto *right = new QVBoxLayout();
    auto *toolbar = new QHBoxLayout();
    m_showVectors = new QCheckBox(QStringLiteral("Show B vectors"), this);
    m_showVectors->setChecked(true);
    auto *planeLabel = new QLabel(QStringLiteral("View plane:"), this);
    m_planeCombo = new QComboBox(this);
    m_planeCombo->addItems({QStringLiteral("XY"), QStringLiteral("XZ"), QStringLiteral("YZ")});
    m_planeCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_planeCombo->setMinimumContentsLength(3);
    m_planeCombo->setMinimumWidth(92);
    auto *centerButton = new QPushButton(QStringLiteral("Center / reset zoom"), this);
    toolbar->addWidget(m_showVectors);
    toolbar->addSpacing(16);
    toolbar->addWidget(planeLabel);
    toolbar->addWidget(m_planeCombo);
    toolbar->addStretch(1);
    toolbar->addWidget(centerButton);
    right->addLayout(toolbar);

    auto *visualizationToolbar = new QHBoxLayout();
    auto *scalarLabel = new QLabel(QStringLiteral("Scalar map:"), this);
    auto *scalarCombo = new QComboBox(this);
    scalarCombo->addItems({QStringLiteral("Off"), QStringLiteral("|B|")});
    scalarCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    scalarCombo->setMinimumContentsLength(4);
    scalarCombo->setMinimumWidth(104);
    scalarCombo->setToolTip(QStringLiteral("Magnetic-flux-density magnitude heatmap in the active plane slice (logarithmic scale)."));
    auto *contoursCheck = new QCheckBox(QStringLiteral("Contours"), this);
    contoursCheck->setToolTip(QStringLiteral("Show iso-|B| contour lines."));
    contoursCheck->setEnabled(false);
    auto *fieldLinesCheck = new QCheckBox(QStringLiteral("Field lines"), this);
    fieldLinesCheck->setToolTip(QStringLiteral("Integrate projected magnetic-field trajectories in the active plane slice."));
    visualizationToolbar->addWidget(scalarLabel);
    visualizationToolbar->addWidget(scalarCombo);
    visualizationToolbar->addSpacing(12);
    visualizationToolbar->addWidget(contoursCheck);
    visualizationToolbar->addWidget(fieldLinesCheck);
    visualizationToolbar->addStretch(1);
    right->addLayout(visualizationToolbar);

    m_canvas = new MagnetostaticCanvas(this);
    m_canvas->setModel(m_model.get());
    m_field3D = new MagnetostaticField3DView(this);
    m_field3D->setModel(m_model.get());
    m_field3D->setShowFieldVectors(m_showVectors->isChecked());
    m_field3D->setEditPlane(m_planeCombo->currentIndex());
    m_field3D->sourceSelected = [this](int index) {
        if (index >= 0 && index < m_sourceCombo->count() && m_sourceCombo->currentIndex() != index)
            m_sourceCombo->setCurrentIndex(index);
    };
    m_field3D->sourceMoveRequested = [this](int index, const MagnetostaticVec3 &position) {
        const auto *source = m_model->source(index);
        if (!source) return;
        auto edited = *source; edited.position = position; m_model->updateSource(index, edited);
        if (index == m_selectedSource)
        {
            const QSignalBlocker bx(m_xSpin), by(m_ySpin), bz(m_zSpin);
            m_xSpin->setValue(position.x); m_ySpin->setValue(position.y); m_zSpin->setValue(position.z);
        }
        updateResults();
    };
    m_field3D->sourceCreateRequested = [this](int typeIndex, const MagnetostaticVec3 &position) {
        const Type type=indexType(typeIndex);MagnetostaticSource source;source.type=type;source.position=position;source.name=QStringLiteral("%1 %2").arg(MagnetostaticModel::typeName(type)).arg(m_model->sourceCount()+1);source.strength=1.0;source.radius=0.45;source.innerRadius=0.20;source.width=1.0;source.height=0.7;source.length=(type==Type::HelmholtzPair?0.45:1.6);source.turns=100;source.elevationDeg=(type==Type::FiniteWire)?0.0:90.0;
        const int index=m_model->addSource(source);refreshSourceList(index);updateResults();
    };
    m_field3D->sourceDeleteRequested = [this](int index) {
        if(index<0||index>=m_model->sourceCount())return;m_model->removeSource(index);refreshSourceList(std::min(index,m_model->sourceCount()-1));updateResults();
    };
    m_fieldTabs = new QTabWidget(this);
    m_fieldTabs->setDocumentMode(true);
    m_fieldTabs->addTab(m_canvas, QStringLiteral("2D field view"));
    m_fieldTabs->addTab(m_field3D, QStringLiteral("3D field view"));
    m_fieldTabs->setTabToolTip(0, QStringLiteral("Reference 2D slice view of the magnetostatic field."));
    m_fieldTabs->setTabToolTip(1, QStringLiteral("Spatial view of the same magnetostatic model. Drag sources to move them; right-click directly in the view to create/delete sources; Delete removes the selected source."));
    right->addWidget(m_fieldTabs, 1);
    root->addLayout(right, 1);

    connect(m_addSourceButton, &QPushButton::clicked, this, [this] {
        addSource(indexType(m_newSourceTypeCombo->currentData().toInt()));
    });
    connect(m_removeButton, &QPushButton::clicked, this, [this] {
        if (m_selectedSource < 0) return;
        const int old = m_selectedSource;
        m_model->removeSource(old);
        refreshSourceList(std::min(old, m_model->sourceCount() - 1));
    });
    connect(m_previousButton, &QPushButton::clicked, this, [this] {
        if (m_model->sourceCount() == 0) return;
        const int next = m_selectedSource <= 0 ? m_model->sourceCount() - 1 : m_selectedSource - 1;
        m_sourceCombo->setCurrentIndex(next);
    });
    connect(m_nextButton, &QPushButton::clicked, this, [this] {
        if (m_model->sourceCount() == 0) return;
        m_sourceCombo->setCurrentIndex((m_selectedSource + 1) % m_model->sourceCount());
    });
    connect(m_sourceCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_loading) return;
        m_selectedSource = index;
        m_canvas->setSelectedSource(index);
        if (m_field3D) m_field3D->setSelectedSource(index);
        loadSourceEditor(index);
    });
    connect(m_typeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        if (!m_loading) { updateEditorVisibility(); updateStrengthLabels(); }
    });
    connect(m_strengthUnitCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        if (m_selectedSource >= 0) loadSourceEditor(m_selectedSource);
    });
    connect(m_applyButton, &QPushButton::clicked, this, &MagnetostaticWorkspace::applySourceEditor);
    connect(m_canvas, &MagnetostaticCanvas::selectedSourceChanged, this, [this](int index) {
        if (index >= 0 && index < m_sourceCombo->count() && m_sourceCombo->currentIndex() != index)
            m_sourceCombo->setCurrentIndex(index);
    });
    connect(m_canvas, &MagnetostaticCanvas::measurementPointChanged, this, [this](double x, double y, double z) {
        const QSignalBlocker bx(m_measureX), by(m_measureY), bz(m_measureZ);
        m_measureX->setValue(x); m_measureY->setValue(y); m_measureZ->setValue(z);
        updateMeasurementFromEditors();
    });
    connect(m_canvas, &MagnetostaticCanvas::sourcePositionEdited, this, [this](int index, double x, double y, double z) {
        const auto *src = m_model->source(index);
        if (!src) return;
        auto edited = *src;
        edited.position = {x,y,z};
        m_model->updateSource(index, edited);
        if (index == m_selectedSource)
        {
            const QSignalBlocker bx(m_xSpin), by(m_ySpin), bz(m_zSpin);
            m_xSpin->setValue(x); m_ySpin->setValue(y); m_zSpin->setValue(z);
        }
        updateResults();
    });
    const auto measurementChanged = [this] { updateMeasurementFromEditors(); };
    connect(m_measureX, qOverload<double>(&QDoubleSpinBox::valueChanged), this, measurementChanged);
    connect(m_measureY, qOverload<double>(&QDoubleSpinBox::valueChanged), this, measurementChanged);
    connect(m_measureZ, qOverload<double>(&QDoubleSpinBox::valueChanged), this, measurementChanged);
    connect(m_testCharge, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { updateResults(); });
    connect(m_testChargeUnit, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updateResults(); });
    connect(m_velocityX, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { updateResults(); });
    connect(m_velocityY, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { updateResults(); });
    connect(m_velocityZ, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { updateResults(); });
    connect(m_showVectors, &QCheckBox::toggled, m_canvas, &MagnetostaticCanvas::setShowFieldVectors);
    connect(m_showVectors, &QCheckBox::toggled, m_field3D, &MagnetostaticField3DView::setShowFieldVectors);
    connect(centerButton, &QPushButton::clicked, m_canvas, &MagnetostaticCanvas::centerView);
    connect(centerButton, &QPushButton::clicked, m_field3D, &MagnetostaticField3DView::centerView);
    connect(scalarCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, contoursCheck](int index) {
        m_canvas->setScalarMap(static_cast<MagnetostaticCanvas::ScalarMap>(std::clamp(index, 0, 1)));
        contoursCheck->setEnabled(index != 0);
    });
    connect(contoursCheck, &QCheckBox::toggled, m_canvas, &MagnetostaticCanvas::setShowContours);
    connect(fieldLinesCheck, &QCheckBox::toggled, m_canvas, &MagnetostaticCanvas::setShowFieldLines);
    connect(m_planeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_canvas->setViewPlane(static_cast<FieldViewPlane>(std::clamp(index, 0, 2)));
        if (m_field3D) m_field3D->setEditPlane(index);
    });
    connect(m_model.get(), &MagnetostaticModel::changed, this, [this] {
        if (m_field3D) m_field3D->update();
        updateResults();
    });
}

void MagnetostaticWorkspace::seedExample()
{
    MagnetostaticSource wire;
    wire.name = QStringLiteral("I1");
    wire.type = Type::InfiniteWireZ;
    wire.position = {-0.9, 0.0, 0.0};
    wire.strength = 3.0;
    m_model->addSource(wire);

    MagnetostaticSource loop;
    loop.name = QStringLiteral("Loop 1");
    loop.type = Type::CircularLoop;
    loop.position = {0.9, 0.0, 0.0};
    loop.strength = 2.0;
    loop.radius = 0.45;
    loop.azimuthDeg = 0.0;
    loop.elevationDeg = 90.0;
    m_model->addSource(loop);

    m_measureX->setValue(0.0);
    m_measureY->setValue(0.6);
    m_measureZ->setValue(0.0);
}

void MagnetostaticWorkspace::addSource(Type type)
{
    MagnetostaticSource source;
    source.type = type;
    source.position = m_canvas->measurementPoint();
    source.name = QStringLiteral("%1 %2").arg(MagnetostaticModel::typeName(type)).arg(m_model->sourceCount() + 1);
    source.strength = type == Type::MagneticDipole ? 1.0 : 1.0;
    source.radius = 0.45;
    source.innerRadius = 0.20;
    source.width = 1.0;
    source.height = 0.7;
    source.length = type == Type::HelmholtzPair ? 0.45 : 1.6;
    source.turns = 100;
    source.elevationDeg = (type == Type::FiniteWire) ? 0.0 : 90.0;
    const int index = m_model->addSource(source);
    refreshSourceList(index);
}

void MagnetostaticWorkspace::refreshSourceList(int selectIndex)
{
    m_loading = true;
    const QSignalBlocker block(m_sourceCombo);
    m_sourceCombo->clear();
    for (int i = 0; i < m_model->sourceCount(); ++i)
    {
        const auto *src = m_model->source(i);
        m_sourceCombo->addItem(src ? src->name : QStringLiteral("Source %1").arg(i + 1));
    }
    if (selectIndex == -2) selectIndex = m_selectedSource;
    if (m_sourceCombo->count() == 0) selectIndex = -1;
    else selectIndex = std::clamp(selectIndex, 0, m_sourceCombo->count() - 1);
    m_sourceCombo->setCurrentIndex(selectIndex);
    m_loading = false;
    m_selectedSource = selectIndex;
    m_canvas->setSelectedSource(selectIndex);
    if (m_field3D) m_field3D->setSelectedSource(selectIndex);
    loadSourceEditor(selectIndex);
}

void MagnetostaticWorkspace::loadSourceEditor(int index)
{
    m_loading = true;
    const auto *src = m_model->source(index);
    const bool enabled = src != nullptr;
    m_sourceEditorGroup->setEnabled(enabled);
    if (src)
    {
        m_nameEdit->setText(src->name);
        m_typeCombo->setCurrentIndex(typeIndex(src->type));
        m_strengthSpin->setValue(src->strength / strengthScale());
        m_xSpin->setValue(src->position.x); m_ySpin->setValue(src->position.y); m_zSpin->setValue(src->position.z);
        m_radiusSpin->setValue(src->radius); m_innerRadiusSpin->setValue(src->innerRadius);
        m_widthSpin->setValue(src->width); m_heightSpin->setValue(src->height);
        m_lengthSpin->setValue(src->length); m_turnsSpin->setValue(src->turns);
        m_wireDiameterSpin->setValue(src->wireDiameter * 1e3);
        m_radialBuildSpin->setValue(src->radialBuild * 1e3);
        m_coreMuRSpin->setValue(src->coreRelativePermeability);
        m_azimuthSpin->setValue(src->azimuthDeg); m_elevationSpin->setValue(src->elevationDeg);
    }
    updateEditorVisibility();
    updateStrengthLabels();
    m_loading = false;
}

void MagnetostaticWorkspace::applySourceEditor()
{
    const auto *current = m_model->source(m_selectedSource);
    if (!current) return;
    MagnetostaticSource edited = *current;
    edited.name = m_nameEdit->text().trimmed().isEmpty() ? current->name : m_nameEdit->text().trimmed();
    edited.type = indexType(m_typeCombo->currentIndex());
    edited.strength = m_strengthSpin->value() * strengthScale();
    edited.position = {m_xSpin->value(), m_ySpin->value(), m_zSpin->value()};
    edited.radius = m_radiusSpin->value();
    edited.innerRadius = std::clamp(m_innerRadiusSpin->value(), 0.0, edited.radius * (1.0 - 1e-9));
    edited.width = m_widthSpin->value(); edited.height = m_heightSpin->value();
    edited.length = m_lengthSpin->value(); edited.turns = m_turnsSpin->value();
    edited.wireDiameter = m_wireDiameterSpin->value() * 1e-3;
    edited.radialBuild = m_radialBuildSpin->value() * 1e-3;
    edited.coreRelativePermeability = m_coreMuRSpin->value();
    edited.azimuthDeg = m_azimuthSpin->value(); edited.elevationDeg = m_elevationSpin->value();
    m_model->updateSource(m_selectedSource, edited);
    refreshSourceList(m_selectedSource);
}

void MagnetostaticWorkspace::updateEditorVisibility()
{
    const Type type = indexType(m_typeCombo->currentIndex());
    const bool radius = type == Type::CircularLoop || type == Type::Solenoid || type == Type::HelmholtzPair ||
                        type == Type::SolidCurrentCylinderZ || type == Type::HollowCurrentCylinderZ;
    const bool innerRadius = type == Type::HollowCurrentCylinderZ;
    const bool dimensions = type == Type::RectangularLoop || type == Type::TriangularLoop;
    const bool length = type == Type::FiniteWire || type == Type::Solenoid || type == Type::HelmholtzPair;
    const bool turns = type == Type::Solenoid;
    const bool fixedZAxis = type == Type::InfiniteWireZ || type == Type::SolidCurrentCylinderZ || type == Type::HollowCurrentCylinderZ;
    const bool sheet = type == Type::InfiniteCurrentSheet;
    const bool orientation = !fixedZAxis;
    m_radiusLabel->setVisible(radius); m_radiusSpin->setVisible(radius);
    m_innerRadiusLabel->setVisible(innerRadius); m_innerRadiusSpin->setVisible(innerRadius);
    m_widthLabel->setVisible(dimensions); m_widthSpin->setVisible(dimensions);
    m_heightLabel->setVisible(dimensions); m_heightSpin->setVisible(dimensions);
    m_lengthLabel->setVisible(length); m_lengthSpin->setVisible(length);
    m_turnsLabel->setVisible(turns); m_turnsSpin->setVisible(turns);
    m_wireDiameterLabel->setVisible(turns); m_wireDiameterSpin->setVisible(turns);
    m_radialBuildLabel->setVisible(turns); m_radialBuildSpin->setVisible(turns);
    m_coreMuRLabel->setVisible(turns); m_coreMuRSpin->setVisible(turns);
    m_azimuthLabel->setVisible(orientation); m_azimuthSpin->setVisible(orientation);
    m_elevationLabel->setVisible(orientation && !sheet); m_elevationSpin->setVisible(orientation && !sheet);
    if (sheet) m_azimuthLabel->setText(QStringLiteral("Current azimuth (°)"));
    else m_azimuthLabel->setText(QStringLiteral("Azimuth (°)"));
}

void MagnetostaticWorkspace::updateStrengthLabels()
{
    const Type type = indexType(m_typeCombo->currentIndex());
    const QString prefix = m_strengthUnitCombo->currentText();
    m_strengthLabel->setText(QStringLiteral("%1 (%2%3)")
                                 .arg(MagnetostaticModel::strengthSymbol(type), prefix,
                                      MagnetostaticModel::strengthUnit(type)));
}

void MagnetostaticWorkspace::updateMeasurementFromEditors()
{
    const MagnetostaticVec3 point{m_measureX->value(), m_measureY->value(), m_measureZ->value()};
    m_canvas->setMeasurementPoint(point);
    if (m_field3D) m_field3D->setMeasurementPoint(point);
    updateResults();
}

void MagnetostaticWorkspace::updateResults()
{
    const MagnetostaticVec3 p{m_measureX->value(), m_measureY->value(), m_measureZ->value()};
    const auto field = m_model->fieldAt(p);
    const MagnetostaticVec3 velocity{m_velocityX->value(), m_velocityY->value(), m_velocityZ->value()};
    const auto force = m_model->lorentzForceAt(p, m_testCharge->value() * testChargeScale(), velocity);

    m_bMagnitude->setText(formatEngineering(field.magneticFluxDensity.norm(), QStringLiteral("T")));
    m_bx->setText(formatEngineering(field.magneticFluxDensity.x, QStringLiteral("T")));
    m_by->setText(formatEngineering(field.magneticFluxDensity.y, QStringLiteral("T")));
    m_bz->setText(formatEngineering(field.magneticFluxDensity.z, QStringLiteral("T")));
    m_hMagnitude->setText(formatEngineering(field.magneticField.norm(), QStringLiteral("A/m")));
    m_forceMagnitude->setText(formatEngineering(force.norm(), QStringLiteral("N")));
    m_fx->setText(formatEngineering(force.x, QStringLiteral("N")));
    m_fy->setText(formatEngineering(force.y, QStringLiteral("N")));
    m_fz->setText(formatEngineering(force.z, QStringLiteral("N")));
    m_note->setText(field.note);

    const auto *selected = m_model->source(m_selectedSource);
    if (selected && (selected->type == Type::Solenoid || selected->type == Type::CircularLoop))
    {
        if (selected->type == Type::Solenoid)
        {
            EmEngineering::SolenoidInput in;
            in.turns = selected->turns;
            in.radiusM = selected->radius;
            in.lengthM = selected->length;
            in.wireDiameterM = selected->wireDiameter;
            in.radialBuildM = selected->radialBuild;
            in.relativePermeability = selected->coreRelativePermeability;
            in.currentA = selected->strength;
            const auto derived = EmEngineering::solenoid(in);
            m_sourceInductance->setText(formatEngineering(derived.recommendedInductanceH, QStringLiteral("H")));
            m_sourceWireResistance->setText(formatEngineering(derived.wireResistanceOhm, QStringLiteral("Ω")));
            m_sourceStoredEnergy->setText(formatEngineering(derived.storedEnergyJ, QStringLiteral("J")));
            m_sourceDerivedNote->setText(derived.note + QStringLiteral(" The canvas B-field still models the winding in free space; core μr is not multiplied into the external numerical Biot–Savart field."));
        }
        else
        {
            const double L = EmEngineering::circularLoopInductance(selected->radius, selected->wireDiameter*0.5, 1);
            m_sourceInductance->setText(formatEngineering(L, QStringLiteral("H")));
            m_sourceWireResistance->setText(QStringLiteral("—"));
            m_sourceStoredEnergy->setText(formatEngineering(0.5*L*selected->strength*selected->strength, QStringLiteral("J")));
            m_sourceDerivedNote->setText(QStringLiteral("Thin circular-wire loop inductance estimate. Wire diameter is taken from the winding field when available."));
        }
    }
    else
    {
        m_sourceInductance->setText(QStringLiteral("—"));
        m_sourceWireResistance->setText(QStringLiteral("—"));
        m_sourceStoredEnergy->setText(QStringLiteral("—"));
        m_sourceDerivedNote->setText(QStringLiteral("Derived L/R values are available for loops and solenoids."));
    }
}

double MagnetostaticWorkspace::strengthScale() const
{
    switch (m_strengthUnitCombo->currentIndex())
    {
    case 0: return 1e-3;
    case 2: return 1e3;
    default: return 1.0;
    }
}

double MagnetostaticWorkspace::testChargeScale() const
{
    switch (m_testChargeUnit->currentIndex())
    {
    case 0: return 1e-9;
    case 1: return 1e-6;
    case 2: return 1e-3;
    default: return 1.0;
    }
}

QString MagnetostaticWorkspace::formatEngineering(double value, const QString &unit)
{
    if (!std::isfinite(value)) return QStringLiteral("n/a");
    if (std::abs(value) < 1e-30) return QStringLiteral("0 %1").arg(unit);
    struct Prefix { double scale; const char *name; };
    static const Prefix prefixes[] = {
        {1e12,"T"},{1e9,"G"},{1e6,"M"},{1e3,"k"},{1.0,""},
        {1e-3,"m"},{1e-6,"u"},{1e-9,"n"},{1e-12,"p"}
    };
    const double a = std::abs(value);
    for (const auto &p : prefixes)
    {
        const double scaled = a / p.scale;
        if (scaled >= 1.0 && scaled < 1000.0)
            return QStringLiteral("%1 %2%3").arg(value / p.scale, 0, 'g', 7).arg(QString::fromLatin1(p.name), unit);
    }
    return QStringLiteral("%1 %2").arg(value, 0, 'g', 7).arg(unit);
}
