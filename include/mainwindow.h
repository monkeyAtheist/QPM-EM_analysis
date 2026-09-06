#pragma once

#include "electrostatic_model.h"

#include <QMainWindow>
#include <memory>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QComboBox;
class QWidget;
class QTabWidget;
class MagnetostaticWorkspace;
class ElectrostaticField3DView;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    void initializeElectrostaticWorkspace();
    void initializeMagnetostaticAndReferenceWorkspaces();
    void initializeMenuBar();
    void addSource(ElectrostaticSource::Type type);
    void refreshSourceList(int selectIndex = -2);
    void loadSourceEditor(int index);
    void applySourceEditor();
    void updateSourceEditorVisibility();
    void updateMeasurementResults();
    void setMeasurementFromEditors();
    void updateStrengthUnitLabels();

    double chargeUnitScale(const QComboBox *combo) const;
    static QString formatEngineering(double value, const QString &unit);

    std::unique_ptr<Ui::MainWindow> ui;
    std::unique_ptr<ElectrostaticModel> m_electrostaticModel;
    ElectrostaticField3DView *m_electrostatic3D = nullptr;
    QTabWidget *m_electroViewTabs = nullptr;
    QWidget *m_fieldStudiesWorkspace = nullptr;
    QTabWidget *m_fieldStudiesTabs = nullptr;
    MagnetostaticWorkspace *m_magnetostaticWorkspace = nullptr;
    int m_selectedSource = -1;
    bool m_loadingEditor = false;
    QWidget *m_referenceWorkspace = nullptr;
    QWidget *m_calculatorWorkspace = nullptr;
    QWidget *m_antennaDesignerWorkspace = nullptr;
    QWidget *m_engineeringWorkspace = nullptr;
};
