#include "electrostatic_model.h"

#include <QtMath>
#include <algorithm>
#include <limits>

namespace
{
constexpr double SingularDistance = 1.0e-9;
constexpr double Pi = 3.1415926535897932384626433832795;

ElectrostaticFieldResult pointContribution(double charge,
                                           const ElectrostaticVec3 &sourcePosition,
                                           const ElectrostaticVec3 &point)
{
    ElectrostaticFieldResult result;
    const ElectrostaticVec3 r = point - sourcePosition;
    const double r2 = r.normSquared();
    if (r2 <= SingularDistance * SingularDistance)
    {
        result.singular = true;
        result.potentialDefined = false;
        result.note = QStringLiteral("Measurement point coincides with a charge element.");
        return result;
    }

    const double distance = std::sqrt(r2);
    result.electricField = r * (ElectrostaticModel::CoulombK * charge / (r2 * distance));
    result.potential = ElectrostaticModel::CoulombK * charge / distance;
    return result;
}

ElectrostaticVec3 rotateXY(double x, double y, double angleRad)
{
    const double c = std::cos(angleRad);
    const double s = std::sin(angleRad);
    return {x * c - y * s, x * s + y * c, 0.0};
}

ElectrostaticFieldResult solidSphereContribution(double totalCharge,
                                                  double radius,
                                                  const ElectrostaticVec3 &center,
                                                  const ElectrostaticVec3 &point)
{
    ElectrostaticFieldResult result;
    radius = std::max(radius, SingularDistance);
    const ElectrostaticVec3 r = point - center;
    const double distance = r.norm();
    if (distance >= radius)
        return pointContribution(totalCharge, center, point);

    result.electricField = r * (ElectrostaticModel::CoulombK * totalCharge /
                                (radius * radius * radius));
    const double ratio = distance / radius;
    result.potential = ElectrostaticModel::CoulombK * totalCharge
                     * (3.0 - ratio * ratio) / (2.0 * radius);
    return result;
}

ElectrostaticVec3 infiniteSolidCylinderField(double lambda,
                                             double radius,
                                             double dx,
                                             double dy)
{
    const double rho2 = dx * dx + dy * dy;
    const double rho = std::sqrt(rho2);
    radius = std::max(radius, SingularDistance);
    if (rho <= SingularDistance)
        return {};
    if (rho < radius)
    {
        const double factor = lambda /
            (2.0 * Pi * ElectrostaticModel::Epsilon0 * radius * radius);
        return {factor * dx, factor * dy, 0.0};
    }
    const double factor = lambda /
        (2.0 * Pi * ElectrostaticModel::Epsilon0 * rho2);
    return {factor * dx, factor * dy, 0.0};
}

void accumulate(ElectrostaticFieldResult &dst, const ElectrostaticFieldResult &src)
{
    dst.electricField += src.electricField;
    if (dst.potentialDefined && src.potentialDefined)
        dst.potential += src.potential;
    else
        dst.potentialDefined = false;
    dst.singular = dst.singular || src.singular;
    if (!src.note.isEmpty())
    {
        if (!dst.note.isEmpty()) dst.note += QStringLiteral(" | ");
        dst.note += src.note;
    }
}

ElectrostaticFieldResult integrateRectangularPlate(const ElectrostaticSource &source,
                                                   const ElectrostaticVec3 &point)
{
    ElectrostaticFieldResult result;
    const double width = std::max(source.width, SingularDistance);
    const double height = std::max(source.height, SingularDistance);
    const double aspect = width / height;
    const int nx = std::clamp(int(std::round(24.0 * std::sqrt(aspect))), 12, 56);
    const int ny = std::clamp(int(std::round(24.0 / std::sqrt(aspect))), 12, 56);
    const int count = nx * ny;
    const double dq = source.strength / double(count);
    const double angle = qDegreesToRadians(source.angleDeg);

    for (int ix = 0; ix < nx; ++ix)
    {
        const double x = (-0.5 + (double(ix) + 0.5) / double(nx)) * width;
        for (int iy = 0; iy < ny; ++iy)
        {
            const double y = (-0.5 + (double(iy) + 0.5) / double(ny)) * height;
            const ElectrostaticVec3 offset = rotateXY(x, y, angle);
            const ElectrostaticVec3 elementPos{source.position.x + offset.x,
                                               source.position.y + offset.y,
                                               source.position.z};
            accumulate(result, pointContribution(dq, elementPos, point));
        }
    }
    if (result.singular)
        result.note = QStringLiteral("Measurement point lies on the numerical rectangular-plate mesh; move it slightly away from the source plane.");
    return result;
}

ElectrostaticFieldResult integrateRectangularVolume(const ElectrostaticSource &source,
                                                    const ElectrostaticVec3 &point)
{
    ElectrostaticFieldResult result;
    const double width = std::max(source.width, SingularDistance);
    const double height = std::max(source.height, SingularDistance);
    const double thickness = std::max(source.thickness, SingularDistance);
    const double angle = qDegreesToRadians(source.angleDeg);

    // Tensor-product 6-point Gauss-Legendre integration over a uniformly charged
    // rectangular parallelepiped. Because rho=Q/(W H t), mapping [-1,1]^3 onto
    // the physical volume gives dq = Q * wx*wy*wz / 8.
    // Even-order nodes avoid placing a quadrature point at the geometric centre.
    static constexpr double node[6] = {
        -0.9324695142031521, -0.6612093864662645, -0.2386191860831969,
         0.2386191860831969,  0.6612093864662645,  0.9324695142031521
    };
    static constexpr double weight[6] = {
        0.1713244923791704, 0.3607615730481386, 0.4679139345726910,
        0.4679139345726910, 0.3607615730481386, 0.1713244923791704
    };

    for (int ix = 0; ix < 6; ++ix)
    {
        const double x = 0.5 * width * node[ix];
        for (int iy = 0; iy < 6; ++iy)
        {
            const double y = 0.5 * height * node[iy];
            const ElectrostaticVec3 xy = rotateXY(x, y, angle);
            for (int iz = 0; iz < 6; ++iz)
            {
                const double z = 0.5 * thickness * node[iz];
                const double dq = source.strength * weight[ix] * weight[iy] * weight[iz] / 8.0;
                const ElectrostaticVec3 elementPos{source.position.x + xy.x,
                                                   source.position.y + xy.y,
                                                   source.position.z + z};
                accumulate(result, pointContribution(dq, elementPos, point));
            }
        }
    }
    if (result.singular)
        result.note = QStringLiteral("Measurement point coincides with a rectangular-volume quadrature node; move it slightly or change the dimensions.");
    else
        result.note = QStringLiteral("Finite-thickness rectangular slab evaluated by 6x6x6 Gauss-Legendre volume integration.");
    return result;
}

ElectrostaticFieldResult integrateCircularPlate(const ElectrostaticSource &source,
                                                const ElectrostaticVec3 &point,
                                                bool annular)
{
    ElectrostaticFieldResult result;
    const double outer = std::max(source.radius, SingularDistance);
    const double inner = annular ? std::clamp(source.innerRadius, 0.0, outer * (1.0 - 1e-9)) : 0.0;
    const double area = Pi * (outer * outer - inner * inner);
    if (area <= 0.0) return result;

    // Equal-area radial bins: q is identical in every polar cell, which avoids
    // overweighting the centre and is stable for educational off-axis studies.
    const int nr = 26;
    const int nphi = 72;
    const double dq = source.strength / double(nr * nphi);
    const double angle = qDegreesToRadians(source.angleDeg);
    const double in2 = inner * inner;
    const double out2 = outer * outer;
    for (int ir = 0; ir < nr; ++ir)
    {
        const double f = (double(ir) + 0.5) / double(nr);
        const double r = std::sqrt(in2 + f * (out2 - in2));
        for (int ip = 0; ip < nphi; ++ip)
        {
            const double phi = 2.0 * Pi * (double(ip) + 0.5) / double(nphi);
            const ElectrostaticVec3 offset = rotateXY(r * std::cos(phi), r * std::sin(phi), angle);
            const ElectrostaticVec3 elementPos{source.position.x + offset.x,
                                               source.position.y + offset.y,
                                               source.position.z};
            accumulate(result, pointContribution(dq, elementPos, point));
        }
    }
    if (result.singular)
        result.note = QStringLiteral("Measurement point lies on the numerical circular-plate mesh; move it slightly away from the source plane.");
    return result;
}

ElectrostaticFieldResult integrateTriangularPlate(const ElectrostaticSource &source,
                                                  const ElectrostaticVec3 &point)
{
    ElectrostaticFieldResult result;
    const double width = std::max(source.width, SingularDistance);
    const double height = std::max(source.height, SingularDistance);
    const double angle = qDegreesToRadians(source.angleDeg);
    const int nx = 64;
    const int ny = 56;

    struct Sample { double x; double y; };
    QVector<Sample> samples;
    samples.reserve(nx * ny / 2);
    // Isosceles triangle centred at its centroid: base y=-h/3, apex y=+2h/3.
    const double yBase = -height / 3.0;
    for (int iy = 0; iy < ny; ++iy)
    {
        const double y = yBase + (double(iy) + 0.5) * height / double(ny);
        const double t = std::clamp((y - yBase) / height, 0.0, 1.0);
        const double halfWidth = 0.5 * width * (1.0 - t);
        for (int ix = 0; ix < nx; ++ix)
        {
            const double x = (-0.5 + (double(ix) + 0.5) / double(nx)) * width;
            if (std::abs(x) <= halfWidth)
                samples.push_back({x, y});
        }
    }
    if (samples.isEmpty()) return result;
    const double dq = source.strength / double(samples.size());
    for (const auto &sample : samples)
    {
        const ElectrostaticVec3 offset = rotateXY(sample.x, sample.y, angle);
        const ElectrostaticVec3 elementPos{source.position.x + offset.x,
                                           source.position.y + offset.y,
                                           source.position.z};
        accumulate(result, pointContribution(dq, elementPos, point));
    }
    if (result.singular)
        result.note = QStringLiteral("Measurement point lies on the numerical triangular-plate mesh; move it slightly away from the source plane.");
    return result;
}
} // namespace

ElectrostaticModel::ElectrostaticModel(QObject *parent)
    : QObject(parent)
{
}

const ElectrostaticSource *ElectrostaticModel::source(int index) const
{
    if (index < 0 || index >= m_sources.size())
        return nullptr;
    return &m_sources.at(index);
}

int ElectrostaticModel::addSource(const ElectrostaticSource &source)
{
    m_sources.push_back(source);
    emit changed();
    return m_sources.size() - 1;
}

bool ElectrostaticModel::updateSource(int index, const ElectrostaticSource &source)
{
    if (index < 0 || index >= m_sources.size())
        return false;
    m_sources[index] = source;
    emit changed();
    return true;
}

bool ElectrostaticModel::removeSource(int index)
{
    if (index < 0 || index >= m_sources.size())
        return false;
    m_sources.removeAt(index);
    emit changed();
    return true;
}

void ElectrostaticModel::clear()
{
    if (m_sources.isEmpty())
        return;
    m_sources.clear();
    emit changed();
}

ElectrostaticFieldResult ElectrostaticModel::fieldAt(const ElectrostaticVec3 &point) const
{
    ElectrostaticFieldResult total;
    for (const auto &source : m_sources)
        accumulate(total, fieldFromSource(source, point));
    return total;
}

ElectrostaticVec3 ElectrostaticModel::forceAt(const ElectrostaticVec3 &point, double testChargeC) const
{
    return fieldAt(point).electricField * testChargeC;
}

QString ElectrostaticModel::typeName(ElectrostaticSource::Type type)
{
    using Type = ElectrostaticSource::Type;
    switch (type)
    {
    case Type::PointCharge: return QStringLiteral("Point charge");
    case Type::SolidSphere: return QStringLiteral("Uniform solid sphere");
    case Type::SphericalShell: return QStringLiteral("Charged spherical shell (surface)");
    case Type::InfiniteCylinderZ: return QStringLiteral("Infinite solid cylinder (Z axis)");
    case Type::FiniteLine: return QStringLiteral("Finite charged wire");
    case Type::RectangularPlate: return QStringLiteral("Rectangular charged plate");
    case Type::ThickSphericalShell: return QStringLiteral("Thick charged sphere with hollow core");
    case Type::InfiniteHollowCylinderZ: return QStringLiteral("Infinite hollow charged cylinder (Z axis)");
    case Type::InfiniteLine: return QStringLiteral("Infinite line charge (Z axis)");
    case Type::CircularPlate: return QStringLiteral("Circular charged plate / disk");
    case Type::AnnularPlate: return QStringLiteral("Annular charged plate");
    case Type::TriangularPlate: return QStringLiteral("Triangular charged plate");
    case Type::InfinitePlane: return QStringLiteral("Infinite charged plane (XY)");
    case Type::RectangularVolume: return QStringLiteral("Rectangular charged plate (finite thickness)");
    }
    return QStringLiteral("Unknown");
}

QString ElectrostaticModel::strengthSymbol(ElectrostaticSource::Type type)
{
    using Type = ElectrostaticSource::Type;
    if (type == Type::InfiniteCylinderZ || type == Type::InfiniteHollowCylinderZ || type == Type::InfiniteLine)
        return QStringLiteral("lambda");
    if (type == Type::InfinitePlane)
        return QStringLiteral("sigma");
    return QStringLiteral("Q");
}

QString ElectrostaticModel::strengthUnit(ElectrostaticSource::Type type)
{
    using Type = ElectrostaticSource::Type;
    if (type == Type::InfiniteCylinderZ || type == Type::InfiniteHollowCylinderZ || type == Type::InfiniteLine)
        return QStringLiteral("C/m");
    if (type == Type::InfinitePlane)
        return QStringLiteral("C/m²");
    return QStringLiteral("C");
}

ElectrostaticFieldResult ElectrostaticModel::fieldFromSource(const ElectrostaticSource &source,
                                                             const ElectrostaticVec3 &point)
{
    using Type = ElectrostaticSource::Type;

    if (source.type == Type::PointCharge)
        return pointContribution(source.strength, source.position, point);

    if (source.type == Type::SolidSphere)
        return solidSphereContribution(source.strength, source.radius, source.position, point);

    if (source.type == Type::SphericalShell)
    {
        ElectrostaticFieldResult result;
        const ElectrostaticVec3 r = point - source.position;
        const double distance = r.norm();
        const double radius = std::max(source.radius, SingularDistance);
        if (distance >= radius)
            return pointContribution(source.strength, source.position, point);
        result.electricField = {};
        result.potential = CoulombK * source.strength / radius;
        return result;
    }

    if (source.type == Type::ThickSphericalShell)
    {
        const double outer = std::max(source.radius, SingularDistance);
        const double inner = std::clamp(source.innerRadius, 0.0, outer * (1.0 - 1e-9));
        if (inner <= SingularDistance)
            return solidSphereContribution(source.strength, outer, source.position, point);
        const double volumeFactor = outer * outer * outer - inner * inner * inner;
        const double outerEquivalentQ = source.strength * (outer * outer * outer) / volumeFactor;
        const double innerEquivalentQ = source.strength * (inner * inner * inner) / volumeFactor;
        ElectrostaticFieldResult result = solidSphereContribution(outerEquivalentQ, outer, source.position, point);
        const auto cavity = solidSphereContribution(-innerEquivalentQ, inner, source.position, point);
        accumulate(result, cavity);
        return result;
    }

    if (source.type == Type::InfiniteCylinderZ)
    {
        ElectrostaticFieldResult result;
        const double dx = point.x - source.position.x;
        const double dy = point.y - source.position.y;
        result.electricField = infiniteSolidCylinderField(source.strength, source.radius, dx, dy);
        result.potentialDefined = false;
        result.note = QStringLiteral("Infinite-cylinder potential requires a reference radius; only E is reported.");
        return result;
    }

    if (source.type == Type::InfiniteHollowCylinderZ)
    {
        ElectrostaticFieldResult result;
        const double outer = std::max(source.radius, SingularDistance);
        const double inner = std::clamp(source.innerRadius, 0.0, outer * (1.0 - 1e-9));
        const double dx = point.x - source.position.x;
        const double dy = point.y - source.position.y;
        if (inner <= SingularDistance)
            result.electricField = infiniteSolidCylinderField(source.strength, outer, dx, dy);
        else
        {
            const double areaFactor = outer * outer - inner * inner;
            const double outerLambda = source.strength * outer * outer / areaFactor;
            const double innerLambda = source.strength * inner * inner / areaFactor;
            result.electricField = infiniteSolidCylinderField(outerLambda, outer, dx, dy)
                                 - infiniteSolidCylinderField(innerLambda, inner, dx, dy);
        }
        result.potentialDefined = false;
        result.note = QStringLiteral("Infinite hollow-cylinder potential requires a reference radius; only E is reported.");
        return result;
    }

    if (source.type == Type::InfiniteLine)
    {
        ElectrostaticFieldResult result;
        const double dx = point.x - source.position.x;
        const double dy = point.y - source.position.y;
        const double rho2 = dx * dx + dy * dy;
        if (rho2 <= SingularDistance * SingularDistance)
        {
            result.singular = true;
            result.potentialDefined = false;
            result.note = QStringLiteral("Measurement point lies on the infinite line charge.");
            return result;
        }
        const double factor = source.strength / (2.0 * Pi * Epsilon0 * rho2);
        result.electricField = {factor * dx, factor * dy, 0.0};
        result.potentialDefined = false;
        result.note = QStringLiteral("Infinite-line potential requires a reference radius; only E is reported.");
        return result;
    }

    if (source.type == Type::InfinitePlane)
    {
        ElectrostaticFieldResult result;
        const double dz = point.z - source.position.z;
        if (std::abs(dz) <= SingularDistance)
        {
            result.singular = true;
            result.potentialDefined = false;
            result.note = QStringLiteral("Measurement point lies on the ideal infinite charged plane.");
            return result;
        }
        const double sign = dz > 0.0 ? 1.0 : -1.0;
        result.electricField = {0.0, 0.0, sign * source.strength / (2.0 * Epsilon0)};
        result.potentialDefined = false;
        result.note = QStringLiteral("The absolute potential of an infinite plane is reference-dependent; only E is reported.");
        return result;
    }

    if (source.type == Type::FiniteLine)
    {
        // Exact field of a uniformly charged finite line. source.strength is the TOTAL charge Q.
        ElectrostaticFieldResult result;
        const double length = std::max(source.length, SingularDistance);
        const double halfLength = 0.5 * length;
        const double lambda = source.strength / length;
        const double angle = qDegreesToRadians(source.angleDeg);
        const ElectrostaticVec3 axis{std::cos(angle), std::sin(angle), 0.0};
        const ElectrostaticVec3 r = point - source.position;
        const double s = r.x * axis.x + r.y * axis.y + r.z * axis.z;
        const ElectrostaticVec3 rhoVector = r - axis * s;
        const double rho2 = rhoVector.normSquared();
        const double tNear = s - halfLength;
        const double tFar = s + halfLength;
        const double rNear = std::sqrt(rho2 + tNear * tNear);
        const double rFar = std::sqrt(rho2 + tFar * tFar);

        if (rho2 <= SingularDistance * SingularDistance)
        {
            if (std::abs(s) <= halfLength + SingularDistance)
            {
                result.singular = true;
                result.potentialDefined = false;
                result.note = QStringLiteral("Measurement point lies on the finite charged wire.");
                return result;
            }
            const double axialFactor = CoulombK * lambda * (1.0 / rNear - 1.0 / rFar);
            result.electricField = axis * axialFactor;
            const double absS = std::abs(s);
            result.potential = CoulombK * lambda
                * std::log((absS + halfLength) / (absS - halfLength));
            return result;
        }

        const double rho = std::sqrt(rho2);
        const double axialFactor = 1.0 / rNear - 1.0 / rFar;
        const double transverseFactor = (tFar / rFar - tNear / rNear) / rho2;
        result.electricField = (axis * axialFactor + rhoVector * transverseFactor)
                               * (CoulombK * lambda);
        result.potential = CoulombK * lambda
            * (std::asinh(tFar / rho) - std::asinh(tNear / rho));
        return result;
    }

    if (source.type == Type::RectangularPlate)
        return integrateRectangularPlate(source, point);
    if (source.type == Type::RectangularVolume)
        return integrateRectangularVolume(source, point);
    if (source.type == Type::CircularPlate)
        return integrateCircularPlate(source, point, false);
    if (source.type == Type::AnnularPlate)
        return integrateCircularPlate(source, point, true);
    if (source.type == Type::TriangularPlate)
        return integrateTriangularPlate(source, point);

    return {};
}
