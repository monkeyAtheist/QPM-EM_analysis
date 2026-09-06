#include "magnetostatic_model.h"

#include <QtMath>
#include <algorithm>
#include <cmath>

namespace
{
constexpr double SingularDistance = 1e-8;
constexpr double Pi = 3.1415926535897932384626433832795;

void perpendicularBasis(const MagnetostaticVec3 &axis,
                        MagnetostaticVec3 &u,
                        MagnetostaticVec3 &v)
{
    const MagnetostaticVec3 a = normalized(axis);
    const MagnetostaticVec3 reference = std::abs(a.z) < 0.85
        ? MagnetostaticVec3{0.0, 0.0, 1.0}
        : MagnetostaticVec3{1.0, 0.0, 0.0};
    u = normalized(cross(a, reference));
    v = normalized(cross(a, u));
}

MagnetostaticVec3 finiteSegmentField(const MagnetostaticVec3 &p0,
                                     const MagnetostaticVec3 &p1,
                                     double current,
                                     const MagnetostaticVec3 &point,
                                     bool *singular)
{
    const MagnetostaticVec3 delta = p1 - p0;
    const double length = delta.norm();
    if (length <= SingularDistance) return {};
    const MagnetostaticVec3 axis = delta / length;
    const MagnetostaticVec3 center = (p0 + p1) * 0.5;
    const double halfLength = 0.5 * length;
    const MagnetostaticVec3 r = point - center;
    const double s = dot(r, axis);
    const MagnetostaticVec3 rhoVector = r - axis * s;
    const double rho2 = rhoVector.normSquared();
    if (rho2 <= SingularDistance * SingularDistance)
    {
        if (std::abs(s) <= halfLength + SingularDistance && singular)
            *singular = true;
        return {};
    }
    const double tNear = s - halfLength;
    const double tFar = s + halfLength;
    const double rNear = std::sqrt(rho2 + tNear * tNear);
    const double rFar = std::sqrt(rho2 + tFar * tFar);
    const double geometry = (tFar / rFar - tNear / rNear) / rho2;
    return cross(axis, rhoVector) * (MagnetostaticModel::BiotSavartK * current * geometry);
}

MagnetostaticVec3 currentCylinderField(double totalCurrent,
                                       double innerRadius,
                                       double outerRadius,
                                       double dx,
                                       double dy)
{
    outerRadius = std::max(outerRadius, SingularDistance);
    innerRadius = std::clamp(innerRadius, 0.0, outerRadius * (1.0 - 1e-9));
    const double rho2 = dx * dx + dy * dy;
    const double rho = std::sqrt(rho2);
    if (rho <= SingularDistance) return {};

    double enclosed = totalCurrent;
    if (rho < innerRadius)
        enclosed = 0.0;
    else if (rho < outerRadius)
    {
        if (innerRadius <= SingularDistance)
            enclosed = totalCurrent * rho2 / (outerRadius * outerRadius);
        else
            enclosed = totalCurrent * (rho2 - innerRadius * innerRadius) /
                       (outerRadius * outerRadius - innerRadius * innerRadius);
    }
    const double factor = MagnetostaticModel::Mu0 * enclosed / (2.0 * Pi * rho2);
    return {-factor * dy, factor * dx, 0.0};
}
}

MagnetostaticModel::MagnetostaticModel(QObject *parent)
    : QObject(parent)
{
}

const MagnetostaticSource *MagnetostaticModel::source(int index) const
{
    if (index < 0 || index >= m_sources.size())
        return nullptr;
    return &m_sources.at(index);
}

int MagnetostaticModel::addSource(const MagnetostaticSource &source)
{
    m_sources.push_back(source);
    emit changed();
    return m_sources.size() - 1;
}

bool MagnetostaticModel::updateSource(int index, const MagnetostaticSource &source)
{
    if (index < 0 || index >= m_sources.size())
        return false;
    m_sources[index] = source;
    emit changed();
    return true;
}

bool MagnetostaticModel::removeSource(int index)
{
    if (index < 0 || index >= m_sources.size())
        return false;
    m_sources.removeAt(index);
    emit changed();
    return true;
}

void MagnetostaticModel::clear()
{
    if (m_sources.isEmpty())
        return;
    m_sources.clear();
    emit changed();
}

MagnetostaticVec3 MagnetostaticModel::axisUnit(const MagnetostaticSource &source)
{
    const double az = qDegreesToRadians(source.azimuthDeg);
    const double el = qDegreesToRadians(source.elevationDeg);
    return normalized({std::cos(el) * std::cos(az),
                       std::cos(el) * std::sin(az),
                       std::sin(el)});
}

MagnetostaticFieldResult MagnetostaticModel::fieldAt(const MagnetostaticVec3 &point) const
{
    MagnetostaticFieldResult total;
    for (const auto &source : m_sources)
    {
        const MagnetostaticFieldResult contribution = fieldFromSource(source, point);
        total.magneticFluxDensity += contribution.magneticFluxDensity;
        total.singular = total.singular || contribution.singular;
        if (!contribution.note.isEmpty())
        {
            if (!total.note.isEmpty()) total.note += QStringLiteral(" | ");
            total.note += contribution.note;
        }
    }
    total.magneticField = total.magneticFluxDensity / Mu0;
    return total;
}

MagnetostaticVec3 MagnetostaticModel::lorentzForceAt(const MagnetostaticVec3 &point,
                                                      double testChargeC,
                                                      const MagnetostaticVec3 &velocity) const
{
    const auto field = fieldAt(point);
    return cross(velocity, field.magneticFluxDensity) * testChargeC;
}

MagnetostaticVec3 MagnetostaticModel::loopField(const MagnetostaticVec3 &center,
                                                const MagnetostaticVec3 &axis,
                                                double radius,
                                                double effectiveCurrent,
                                                const MagnetostaticVec3 &point,
                                                int segments,
                                                bool *singular)
{
    MagnetostaticVec3 u, v;
    perpendicularBasis(axis, u, v);
    MagnetostaticVec3 result;
    const int n = std::max(24, segments);

    for (int i = 0; i < n; ++i)
    {
        const double a0 = 2.0 * Pi * double(i) / double(n);
        const double a1 = 2.0 * Pi * double(i + 1) / double(n);
        const MagnetostaticVec3 p0 = center + (u * std::cos(a0) + v * std::sin(a0)) * radius;
        const MagnetostaticVec3 p1 = center + (u * std::cos(a1) + v * std::sin(a1)) * radius;
        result += finiteSegmentField(p0, p1, effectiveCurrent, point, singular);
    }
    return result;
}

MagnetostaticFieldResult MagnetostaticModel::fieldFromSource(const MagnetostaticSource &source,
                                                              const MagnetostaticVec3 &point)
{
    MagnetostaticFieldResult out;
    using Type = MagnetostaticSource::Type;

    if (source.type == Type::InfiniteWireZ)
    {
        const double dx = point.x - source.position.x;
        const double dy = point.y - source.position.y;
        const double rho2 = dx * dx + dy * dy;
        if (rho2 < SingularDistance * SingularDistance)
        {
            out.singular = true;
            out.note = QStringLiteral("Measurement point lies on an infinite wire.");
            return out;
        }
        const double factor = Mu0 * source.strength / (2.0 * Pi * rho2);
        out.magneticFluxDensity = {-factor * dy, factor * dx, 0.0};
    }
    else if (source.type == Type::SolidCurrentCylinderZ || source.type == Type::HollowCurrentCylinderZ)
    {
        const double dx = point.x - source.position.x;
        const double dy = point.y - source.position.y;
        const double inner = source.type == Type::HollowCurrentCylinderZ ? source.innerRadius : 0.0;
        out.magneticFluxDensity = currentCylinderField(source.strength, inner, source.radius, dx, dy);
    }
    else if (source.type == Type::FiniteWire)
    {
        const MagnetostaticVec3 axis = axisUnit(source);
        const double length = std::max(source.length, SingularDistance);
        const MagnetostaticVec3 p0 = source.position - axis * (0.5 * length);
        const MagnetostaticVec3 p1 = source.position + axis * (0.5 * length);
        out.magneticFluxDensity = finiteSegmentField(p0, p1, source.strength, point, &out.singular);
        if (out.singular) out.note = QStringLiteral("Measurement point lies on the finite current wire.");
    }
    else if (source.type == Type::CircularLoop)
    {
        out.magneticFluxDensity = loopField(source.position, axisUnit(source),
                                            std::max(source.radius, 1e-9), source.strength,
                                            point, 320, &out.singular);
    }
    else if (source.type == Type::RectangularLoop || source.type == Type::TriangularLoop)
    {
        MagnetostaticVec3 u, v;
        perpendicularBasis(axisUnit(source), u, v);
        const double w = std::max(source.width, SingularDistance);
        const double h = std::max(source.height, SingularDistance);
        if (source.type == Type::RectangularLoop)
        {
            const MagnetostaticVec3 p0 = source.position - u*(0.5*w) - v*(0.5*h);
            const MagnetostaticVec3 p1 = source.position + u*(0.5*w) - v*(0.5*h);
            const MagnetostaticVec3 p2 = source.position + u*(0.5*w) + v*(0.5*h);
            const MagnetostaticVec3 p3 = source.position - u*(0.5*w) + v*(0.5*h);
            out.magneticFluxDensity += finiteSegmentField(p0,p1,source.strength,point,&out.singular);
            out.magneticFluxDensity += finiteSegmentField(p1,p2,source.strength,point,&out.singular);
            out.magneticFluxDensity += finiteSegmentField(p2,p3,source.strength,point,&out.singular);
            out.magneticFluxDensity += finiteSegmentField(p3,p0,source.strength,point,&out.singular);
        }
        else
        {
            // Isosceles triangular loop centred at its centroid.
            const MagnetostaticVec3 p0 = source.position - u*(0.5*w) - v*(h/3.0);
            const MagnetostaticVec3 p1 = source.position + u*(0.5*w) - v*(h/3.0);
            const MagnetostaticVec3 p2 = source.position + v*(2.0*h/3.0);
            out.magneticFluxDensity += finiteSegmentField(p0,p1,source.strength,point,&out.singular);
            out.magneticFluxDensity += finiteSegmentField(p1,p2,source.strength,point,&out.singular);
            out.magneticFluxDensity += finiteSegmentField(p2,p0,source.strength,point,&out.singular);
        }
        if (out.singular) out.note = QStringLiteral("Measurement point lies on a current-loop filament.");
    }
    else if (source.type == Type::Solenoid)
    {
        const MagnetostaticVec3 axis = axisUnit(source);
        const int virtualLoops = std::clamp(source.turns, 1, 28);
        const double effectiveCurrent = source.strength * double(std::max(source.turns, 1)) / double(virtualLoops);
        const double length = std::max(source.length, 1e-9);
        for (int i = 0; i < virtualLoops; ++i)
        {
            const double s = virtualLoops == 1
                ? 0.0
                : -0.5 * length + (double(i) + 0.5) * length / double(virtualLoops);
            const MagnetostaticVec3 center = source.position + axis * s;
            out.magneticFluxDensity += loopField(center, axis, std::max(source.radius, 1e-9), effectiveCurrent,
                                                 point, 88, &out.singular);
        }
    }
    else if (source.type == Type::HelmholtzPair)
    {
        const MagnetostaticVec3 axis = axisUnit(source);
        const double separation = source.length > SingularDistance ? source.length : std::max(source.radius, 1e-9);
        out.magneticFluxDensity += loopField(source.position - axis * (0.5 * separation), axis,
                                             std::max(source.radius, 1e-9), source.strength,
                                             point, 220, &out.singular);
        out.magneticFluxDensity += loopField(source.position + axis * (0.5 * separation), axis,
                                             std::max(source.radius, 1e-9), source.strength,
                                             point, 220, &out.singular);
    }
    else if (source.type == Type::InfiniteCurrentSheet)
    {
        const double dz = point.z - source.position.z;
        if (std::abs(dz) <= SingularDistance)
        {
            out.singular = true;
            out.note = QStringLiteral("Measurement point lies on the ideal infinite current sheet.");
            return out;
        }
        const double az = qDegreesToRadians(source.azimuthDeg);
        const MagnetostaticVec3 kdir{std::cos(az), std::sin(az), 0.0};
        const MagnetostaticVec3 normal{0.0, 0.0, 1.0};
        const double sign = dz > 0.0 ? 1.0 : -1.0;
        out.magneticFluxDensity = cross(kdir, normal) * (0.5 * Mu0 * source.strength * sign);
    }
    else // Magnetic dipole
    {
        const MagnetostaticVec3 r = point - source.position;
        const double rn = r.norm();
        if (rn < SingularDistance)
        {
            out.singular = true;
            out.note = QStringLiteral("Measurement point lies on a magnetic dipole.");
            return out;
        }
        const MagnetostaticVec3 rhat = r / rn;
        const MagnetostaticVec3 m = axisUnit(source) * source.strength;
        out.magneticFluxDensity = (rhat * (3.0 * dot(m, rhat)) - m)
                                   * (BiotSavartK / (rn * rn * rn));
    }

    out.magneticField = out.magneticFluxDensity / Mu0;
    return out;
}

QString MagnetostaticModel::typeName(MagnetostaticSource::Type type)
{
    using Type = MagnetostaticSource::Type;
    switch (type)
    {
    case Type::InfiniteWireZ: return QStringLiteral("Infinite wire (Z)");
    case Type::FiniteWire: return QStringLiteral("Finite straight wire");
    case Type::CircularLoop: return QStringLiteral("Circular loop");
    case Type::Solenoid: return QStringLiteral("Finite solenoid");
    case Type::MagneticDipole: return QStringLiteral("Magnetic dipole");
    case Type::SolidCurrentCylinderZ: return QStringLiteral("Infinite solid current cylinder (Z)");
    case Type::HollowCurrentCylinderZ: return QStringLiteral("Infinite hollow current cylinder (Z)");
    case Type::RectangularLoop: return QStringLiteral("Rectangular current loop");
    case Type::TriangularLoop: return QStringLiteral("Triangular current loop");
    case Type::HelmholtzPair: return QStringLiteral("Helmholtz coil pair");
    case Type::InfiniteCurrentSheet: return QStringLiteral("Infinite current sheet (XY)");
    }
    return QStringLiteral("Magnetic source");
}

QString MagnetostaticModel::strengthSymbol(MagnetostaticSource::Type type)
{
    if (type == MagnetostaticSource::Type::MagneticDipole)
        return QStringLiteral("m");
    if (type == MagnetostaticSource::Type::InfiniteCurrentSheet)
        return QStringLiteral("K");
    return QStringLiteral("I");
}

QString MagnetostaticModel::strengthUnit(MagnetostaticSource::Type type)
{
    if (type == MagnetostaticSource::Type::MagneticDipole)
        return QStringLiteral("A.m²");
    if (type == MagnetostaticSource::Type::InfiniteCurrentSheet)
        return QStringLiteral("A/m");
    return QStringLiteral("A");
}
