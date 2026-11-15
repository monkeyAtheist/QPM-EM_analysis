#include "electrostatic_model.h"
#include <QtTest/QtTest>
#include <cmath>

class Electrostatic61300Test : public QObject
{
    Q_OBJECT
private slots:
    void uniformlyChargedSphereGaussLaw()
    {
        ElectrostaticModel model;
        ElectrostaticSource src;
        src.type=ElectrostaticSource::Type::SolidSphere;
        src.position={0,0,0};
        src.radius=1.0;
        src.strength=1.0; // TOTAL charge 1 C, not rho=1 C/m^3
        model.addSource(src);
        const double k=ElectrostaticModel::CoulombK;
        auto result=model.fieldAt({2,0,0});
        QVERIFY(!result.singular);
        QVERIFY(std::abs(result.electricField.x-k/4.0)/(k/4.0)<1e-12);
        QVERIFY(std::abs(result.electricField.y)<1e-8);
        QVERIFY(std::abs(result.potential-k/2.0)/(k/2.0)<1e-12);
        result=model.fieldAt({0.5,0,0});
        QVERIFY(std::abs(result.electricField.x-k*0.5)/(k*0.5)<1e-12);
        const auto center=model.fieldAt({0,0,0});
        QVERIFY(std::abs(center.electricField.x)<1e-8);
        QVERIFY(std::abs(center.potential-k*1.5)/(k*1.5)<1e-12);
        result=model.fieldAt({1,0,0});
        QVERIFY(std::abs(result.electricField.x-k)/k<1e-12);
        // If the INPUT were rho=1 C/m^3, Q=4pi/3 C is required.
        src.strength=4.0*std::acos(-1.0)/3.0;
        model.updateSource(0,src);
        result=model.fieldAt({2,0,0});
        QVERIFY(std::abs(result.electricField.x-9.41174222814915e9)/9.41174222814915e9<1e-9);
    }
    void otherAnalyticalElectrostaticSources()
    {
        ElectrostaticModel model;
        ElectrostaticSource src;
        src.position={0,0,0};
        src.strength=1.0;
        src.radius=1.0;
        const double k=ElectrostaticModel::CoulombK;
        const double eps0=ElectrostaticModel::Epsilon0;
        src.type=ElectrostaticSource::Type::PointCharge;
        model.addSource(src);
        auto result=model.fieldAt({2,0,0});
        QVERIFY(std::abs(result.electricField.x-k/4.0)<2e-3);
        src.type=ElectrostaticSource::Type::SphericalShell;
        model.updateSource(0,src);
        result=model.fieldAt({0.5,0,0});
        QVERIFY(std::abs(result.electricField.x)<1e-8);
        QVERIFY(std::abs(result.potential-k)<1e-3);
        result=model.fieldAt({2,0,0});
        QVERIFY(std::abs(result.electricField.x-k/4.0)<2e-3);
        src.type=ElectrostaticSource::Type::ThickSphericalShell;
        src.innerRadius=0.5;
        model.updateSource(0,src);
        result=model.fieldAt({0.2,0,0});
        QVERIFY(std::abs(result.electricField.x)<1e-5);
        src.type=ElectrostaticSource::Type::InfiniteLine;
        model.updateSource(0,src);
        result=model.fieldAt({2,0,0});
        QVERIFY(!result.potentialDefined);
        QVERIFY(std::abs(result.electricField.x-1/(4*std::acos(-1.0)*eps0))<1e-3);
        src.type=ElectrostaticSource::Type::InfinitePlane;
        model.updateSource(0,src);
        result=model.fieldAt({0,0,2});
        QVERIFY(std::abs(result.electricField.z-1/(2*eps0))<1e-2);
        result=model.fieldAt({0,0,-2});
        QVERIFY(std::abs(result.electricField.z+1/(2*eps0))<1e-2);
        src.type=ElectrostaticSource::Type::InfiniteCylinderZ;
        model.updateSource(0,src);
        result=model.fieldAt({0.5,0,0});
        QVERIFY(std::abs(result.electricField.x-0.5/(2*std::acos(-1.0)*eps0))<1e-3);
        result=model.fieldAt({2,0,0});
        QVERIFY(std::abs(result.electricField.x-1/(4*std::acos(-1.0)*eps0))<1e-3);
        src.type=ElectrostaticSource::Type::FiniteLine;
        src.length=1.0;
        model.updateSource(0,src);
        result=model.fieldAt({2,0,0});
        const double finiteExpected=k*(1/1.5-1/2.5);
        QVERIFY(std::abs(result.electricField.x-finiteExpected)/finiteExpected<1e-10);
    }
};
QTEST_MAIN(Electrostatic61300Test)
#include "electrostatic_61300.moc"
