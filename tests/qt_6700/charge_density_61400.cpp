#include "electrostatic_model.h"
#include "electrostatic_charge_density.h"
#include <QtTest/QtTest>
#include <cmath>

class ChargeDensity61400 : public QObject
{
    Q_OBJECT
private slots:
    void spherePhysicalFieldTracksDensity()
    {
        using namespace ElectrostaticChargeDensity;
        ElectrostaticModel model;
        ElectrostaticSource source;
        source.type = ElectrostaticSource::Type::SolidSphere;
        source.radius = 1.0;
        Dimensions dim;
        dim.radius = source.radius;
        auto rel = relation(Shape::SolidSphere,dim);
        source.strength = rel.primaryFromDensity(1.0); // rho=1 C/m3, NOT Q=1 C
        model.addSource(source);
        auto result = model.fieldAt({2,0,0});
        const double expected = ElectrostaticModel::CoulombK * (4.0*Pi/3.0) / 4.0;
        QVERIFY(std::abs(result.electricField.x-expected)/expected < 1e-11);
        source.radius=2.0;
        dim.radius=source.radius;
        rel=relation(Shape::SolidSphere,dim);
        source.strength=rel.primaryFromDensity(1.0);
        model.updateSource(0,source);
        // r=3 m, same rho; total charge increases by factor 8.
        result=model.fieldAt({3,0,0});
        const double outside = ElectrostaticModel::CoulombK * (32.0*Pi/3.0) / 9.0;
        QVERIFY(std::abs(result.electricField.x-outside)/outside < 1e-11);
    }
};
QTEST_GUILESS_MAIN(ChargeDensity61400)
#include "charge_density_61400.moc"
