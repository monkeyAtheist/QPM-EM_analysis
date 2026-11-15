#include "electrostatic_charge_density.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace ElectrostaticChargeDensity;

bool close(double a,double b,double tol=1e-12)
{
    return std::abs(a-b) <= tol*std::max(1.0,std::max(std::abs(a),std::abs(b)));
}

int main()
{
    Dimensions d;
    d.radius = 1.0;
    auto r=relation(Shape::SolidSphere,d);
    assert(r.valid() && r.primaryIsTotalCharge && r.densityKind==Kind::Volume);
    assert(close(r.measure,4*Pi/3));
    assert(close(r.densityFromPrimary(1),3/(4*Pi)));
    assert(close(r.primaryFromDensity(1),4*Pi/3));
    assert(close(r.primaryFromDensity(-2),-8*Pi/3));

    // When changing radius with a density-driven edit, Q must follow R^3;
    // when changing radius with a Q-driven edit, density must follow 1/R^3.
    d.radius=2;
    r=relation(Shape::SolidSphere,d);
    assert(close(r.primaryFromDensity(1),32*Pi/3));
    assert(close(r.densityFromPrimary(1),3/(32*Pi)));
    d.radius=1;
    r=relation(Shape::SphericalSurface,d);
    assert(close(r.measure,4*Pi));
    assert(r.densityKind==Kind::Surface);

    d.innerRadius=.5;
    r=relation(Shape::ThickSphericalShell,d);
    assert(close(r.measure,4*Pi/3*(1-.125)));
    r=relation(Shape::InfiniteSolidCylinder,d);
    assert(close(r.measure,Pi));
    assert(!r.primaryIsTotalCharge && r.densityKind==Kind::Volume);
    r=relation(Shape::InfiniteHollowCylinder,d);
    assert(close(r.measure,Pi*.75));

    d.length=4;
    r=relation(Shape::FiniteLine,d);
    assert(close(r.measure,4));
    assert(close(r.densityFromPrimary(12),3));
    assert(r.densityKind==Kind::Linear);
    d.width=3; d.height=2; d.thickness=.2;
    assert(close(relation(Shape::RectangularSurface,d).measure,6));
    assert(close(relation(Shape::RectangularVolume,d).measure,1.2));
    assert(close(relation(Shape::TriangularSurface,d).measure,3));
    assert(close(relation(Shape::CircularSurface,d).measure,Pi));
    assert(close(relation(Shape::AnnularSurface,d).measure,.75*Pi));
    assert(!relation(Shape::None,d).valid());
    d.radius=0;
    assert(!relation(Shape::SolidSphere,d).valid());
    d.radius=1; d.innerRadius=1;
    assert(!relation(Shape::AnnularSurface,d).valid());
    assert(!relation(Shape::ThickSphericalShell,d).valid());
    std::cout << "6.14 charge-density measures and bidirectional conversion: PASS\n";
}
