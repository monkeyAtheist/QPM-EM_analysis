#pragma once

#include "magnetostatic_model.h"

#include <QWidget>
#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;
class MagnetostaticCanvas;
class MagnetostaticField3DView;

class MagnetostaticWorkspace final : public QWidget
{
    Q_OBJECT

public:
    explicit MagnetostaticWorkspace(QWidget *parent = nullptr);

    MagnetostaticModel *model() const noexcept { return m_model.get(); }
    MagnetostaticCanvas *canvas() const noexcept { return m_canvas; }
    QTabWidget *fieldViewTabs() const noexcept { return m_fieldTabs; }

private:
    void buildUi();
    void seedExample();
    void addSource(MagnetostaticSource::Type type);
    void refreshSourceList(int selectIndex = -2);
    void loadSourceEditor(int index);
    void applySourceEditor();
    void updateEditorVisibility();
    void updateStrengthLabels();
    void updateMeasurementFromEditors();
    void updateResults();

    double strengthScale() const;
    double testChargeScale() const;
    static QString formatEngineering(double value, const QString &unit);

    std::unique_ptr<MagnetostaticModel> m_model;
    MagnetostaticCanvas *m_canvas = nullptr;
    MagnetostaticField3DView *m_field3D = nullptr;
    QTabWidget *m_fieldTabs = nullptr;

    QComboBox *m_sourceCombo = nullptr;
    QComboBox *m_newSourceTypeCombo = nullptr;
    QPushButton *m_addSourceButton = nullptr;
    QPushButton *m_previousButton = nullptr;
    QPushButton *m_nextButton = nullptr;
    QPushButton *m_removeButton = nullptr;

    QGroupBox *m_sourceEditorGroup = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QComboBox *m_typeCombo = nullptr;
    QLabel *m_strengthLabel = nullptr;
    QDoubleSpinBox *m_strengthSpin = nullptr;
    QComboBox *m_strengthUnitCombo = nullptr;
    QDoubleSpinBox *m_xSpin = nullptr;
    QDoubleSpinBox *m_ySpin = nullptr;
    QDoubleSpinBox *m_zSpin = nullptr;
    QLabel *m_radiusLabel = nullptr;
    QDoubleSpinBox *m_radiusSpin = nullptr;
    QLabel *m_innerRadiusLabel = nullptr;
    QDoubleSpinBox *m_innerRadiusSpin = nullptr;
    QLabel *m_widthLabel = nullptr;
    QDoubleSpinBox *m_widthSpin = nullptr;
    QLabel *m_heightLabel = nullptr;
    QDoubleSpinBox *m_heightSpin = nullptr;
    QLabel *m_lengthLabel = nullptr;
    QDoubleSpinBox *m_lengthSpin = nullptr;
    QLabel *m_turnsLabel = nullptr;
    QSpinBox *m_turnsSpin = nullptr;
    QLabel *m_wireDiameterLabel = nullptr;
    QDoubleSpinBox *m_wireDiameterSpin = nullptr;
    QLabel *m_radialBuildLabel = nullptr;
    QDoubleSpinBox *m_radialBuildSpin = nullptr;
    QLabel *m_coreMuRLabel = nullptr;
    QDoubleSpinBox *m_coreMuRSpin = nullptr;
    QLabel *m_azimuthLabel = nullptr;
    QDoubleSpinBox *m_azimuthSpin = nullptr;
    QLabel *m_elevationLabel = nullptr;
    QDoubleSpinBox *m_elevationSpin = nullptr;
    QPushButton *m_applyButton = nullptr;

    QDoubleSpinBox *m_measureX = nullptr;
    QDoubleSpinBox *m_measureY = nullptr;
    QDoubleSpinBox *m_measureZ = nullptr;
    QDoubleSpinBox *m_testCharge = nullptr;
    QComboBox *m_testChargeUnit = nullptr;
    QDoubleSpinBox *m_velocityX = nullptr;
    QDoubleSpinBox *m_velocityY = nullptr;
    QDoubleSpinBox *m_velocityZ = nullptr;

    QLabel *m_bMagnitude = nullptr;
    QLabel *m_bx = nullptr;
    QLabel *m_by = nullptr;
    QLabel *m_bz = nullptr;
    QLabel *m_hMagnitude = nullptr;
    QLabel *m_forceMagnitude = nullptr;
    QLabel *m_fx = nullptr;
    QLabel *m_fy = nullptr;
    QLabel *m_fz = nullptr;
    QLabel *m_note = nullptr;
    QLabel *m_sourceInductance = nullptr;
    QLabel *m_sourceWireResistance = nullptr;
    QLabel *m_sourceStoredEnergy = nullptr;
    QLabel *m_sourceDerivedNote = nullptr;

    QCheckBox *m_showVectors = nullptr;
    QComboBox *m_planeCombo = nullptr;

    int m_selectedSource = -1;
    bool m_loading = false;
};
