#pragma once

#include <QWidget>

class ElectrostaticModel;
class ElectrostaticCanvas;
class MagnetostaticModel;
class MagnetostaticCanvas;

// Legacy combined container kept for compatibility with older callers.
class FieldProfilesWidget final : public QWidget
{
public:
    explicit FieldProfilesWidget(ElectrostaticModel *electrostaticModel,
                                 ElectrostaticCanvas *electrostaticCanvas,
                                 MagnetostaticModel *magnetostaticModel,
                                 MagnetostaticCanvas *magnetostaticCanvas,
                                 QWidget *parent = nullptr);
};

// Domain-specific pages used directly inside the Electrostatics / Magnetostatics
// field-view tab stacks.  This avoids a separate top-level "Probes & profiles"
// workspace and keeps each analysis tool next to the field it samples.
QWidget *createElectrostaticProfilePage(ElectrostaticModel *model,
                                        ElectrostaticCanvas *canvas,
                                        QWidget *parent = nullptr);
QWidget *createElectrostaticPointProbesPage(ElectrostaticModel *model,
                                            ElectrostaticCanvas *canvas,
                                            QWidget *parent = nullptr);
QWidget *createMagnetostaticProfilePage(MagnetostaticModel *model,
                                        MagnetostaticCanvas *canvas,
                                        QWidget *parent = nullptr);
QWidget *createMagnetostaticPointProbesPage(MagnetostaticModel *model,
                                            MagnetostaticCanvas *canvas,
                                            QWidget *parent = nullptr);
