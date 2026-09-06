#pragma once

#include <QWidget>

class AntennaDesignerWidget;

class ElectromagneticEngineeringWorkspace final : public QWidget
{
    Q_OBJECT
public:
    explicit ElectromagneticEngineeringWorkspace(AntennaDesignerWidget *antennaDesigner, QWidget *parent = nullptr);
};
