#pragma once

// Geometry-only conversions for uniformly distributed electrostatic charge.
// No Qt dependency: this contract is also exercised in a portable C++17 test.
// 'strength' is Q [C] for finite objects, lambda [C/m] for infinite cylinders,
// or sigma [C/m2] for an infinite plane (those latter two have no finite Q).

#include <cmath>

namespace ElectrostaticChargeDensity
{
constexpr double Pi = 3.1415926535897932384626433832795;

enum class Shape
{
    None,
    SolidSphere,
    SphericalSurface,
    ThickSphericalShell,
    InfiniteSolidCylinder,
    InfiniteHollowCylinder,
    FiniteLine,
    RectangularSurface,
    RectangularVolume,
    CircularSurface,
    AnnularSurface,
    TriangularSurface
};

enum class Kind { None, Linear, Surface, Volume };

struct Dimensions
{
    double radius = 0.0;
    double innerRadius = 0.0;
    double length = 0.0;
    double width = 0.0;
    double height = 0.0;
    double thickness = 0.0;
};

struct Relation
{
    Kind densityKind = Kind::None;
    double measure = 0.0; // Q / density, or lambda / rho for infinite cylinders
    bool primaryIsTotalCharge = false;

    bool valid() const noexcept
    {
        return densityKind != Kind::None && std::isfinite(measure) && measure > 0.0;
    }
    double densityFromPrimary(double primary) const noexcept
    {
        return valid() && std::isfinite(primary) ? primary / measure : NAN;
    }
    double primaryFromDensity(double density) const noexcept
    {
        return valid() && std::isfinite(density) ? density * measure : NAN;
    }
};

inline Relation relation(Shape shape, const Dimensions &d) noexcept
{
    const auto positive = [](double v) { return std::isfinite(v) && v > 0.0; };
    const bool r = positive(d.radius);
    const bool ri = std::isfinite(d.innerRadius) && d.innerRadius >= 0.0 && r && d.innerRadius < d.radius;
    const double outer2 = d.radius * d.radius;
    const double inner2 = d.innerRadius * d.innerRadius;
    const double outer3 = outer2 * d.radius;
    const double inner3 = inner2 * d.innerRadius;
    switch (shape)
    {
    case Shape::SolidSphere:
        return {Kind::Volume, r ? 4.0 * Pi * outer3 / 3.0 : 0.0, true};
    case Shape::SphericalSurface:
        return {Kind::Surface, r ? 4.0 * Pi * outer2 : 0.0, true};
    case Shape::ThickSphericalShell:
        return {Kind::Volume, ri ? 4.0 * Pi * (outer3 - inner3) / 3.0 : 0.0, true};
    case Shape::InfiniteSolidCylinder:
        return {Kind::Volume, r ? Pi * outer2 : 0.0, false};
    case Shape::InfiniteHollowCylinder:
        return {Kind::Volume, ri ? Pi * (outer2 - inner2) : 0.0, false};
    case Shape::FiniteLine:
        return {Kind::Linear, positive(d.length) ? d.length : 0.0, true};
    case Shape::RectangularSurface:
        return {Kind::Surface, positive(d.width) && positive(d.height) ? d.width * d.height : 0.0, true};
    case Shape::RectangularVolume:
        return {Kind::Volume, positive(d.width) && positive(d.height) && positive(d.thickness)
                    ? d.width * d.height * d.thickness : 0.0, true};
    case Shape::CircularSurface:
        return {Kind::Surface, r ? Pi * outer2 : 0.0, true};
    case Shape::AnnularSurface:
        return {Kind::Surface, ri ? Pi * (outer2 - inner2) : 0.0, true};
    case Shape::TriangularSurface:
        return {Kind::Surface, positive(d.width) && positive(d.height) ? 0.5 * d.width * d.height : 0.0, true};
    case Shape::None:
        break;
    }
    return {};
}
} // namespace ElectrostaticChargeDensity
