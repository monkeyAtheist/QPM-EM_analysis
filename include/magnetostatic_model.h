#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include <cmath>

struct MagnetostaticVec3
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    MagnetostaticVec3 operator+(const MagnetostaticVec3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
    MagnetostaticVec3 operator-(const MagnetostaticVec3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
    MagnetostaticVec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    MagnetostaticVec3 operator/(double s) const { return {x / s, y / s, z / s}; }
    MagnetostaticVec3 &operator+=(const MagnetostaticVec3 &o) { x += o.x; y += o.y; z += o.z; return *this; }

    double normSquared() const { return x * x + y * y + z * z; }
    double norm() const { return std::sqrt(normSquared()); }
};

inline double dot(const MagnetostaticVec3 &a, const MagnetostaticVec3 &b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline MagnetostaticVec3 cross(const MagnetostaticVec3 &a, const MagnetostaticVec3 &b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

inline MagnetostaticVec3 normalized(const MagnetostaticVec3 &v)
{
    const double n = v.norm();
    return n > 1e-18 ? v / n : MagnetostaticVec3{0.0, 0.0, 1.0};
}

struct MagnetostaticFieldResult
{
    MagnetostaticVec3 magneticFluxDensity; // B [T]
    MagnetostaticVec3 magneticField;        // H [A/m], vacuum approximation B / mu0
    bool singular = false;
    QString note;
};

struct MagnetostaticSource
{
    enum class Type
    {
        // Original indices kept stable.
        InfiniteWireZ,
        FiniteWire,
        CircularLoop,
        Solenoid,
        MagneticDipole,

        // 5.19 educational source library.
        SolidCurrentCylinderZ,
        HollowCurrentCylinderZ,
        RectangularLoop,
        TriangularLoop,
        HelmholtzPair,
        InfiniteCurrentSheet
    };

    QString name;
    Type type = Type::InfiniteWireZ;
    MagnetostaticVec3 position;

    // Current [A] for wires/loops/coils/current cylinders; magnetic moment [A.m^2]
    // for dipole; surface-current density K [A/m] for InfiniteCurrentSheet.
    double strength = 1.0;
    double radius = 0.5;       // loop/solenoid/cylinder outer radius [m]
    double innerRadius = 0.25; // hollow-current-cylinder inner radius [m]
    double length = 2.0;       // finite wire/solenoid or Helmholtz separation [m]
    double width = 1.0;        // rectangular/triangular loop width [m]
    double height = 0.7;       // rectangular/triangular loop height [m]
    int turns = 100;           // solenoid turns
    double wireDiameter = 0.0008; // winding wire diameter [m]
    double radialBuild = 0.0008;  // winding radial depth [m]
    double coreRelativePermeability = 1.0; // effective mu_r for derived L/B estimates only
    double azimuthDeg = 0.0;   // source axis direction in XY; sheet current direction for current sheet
    double elevationDeg = 90.0;// source axis elevation from XY
};

class MagnetostaticModel final : public QObject
{
    Q_OBJECT

public:
    explicit MagnetostaticModel(QObject *parent = nullptr);

    static constexpr double Mu0 = 1.2566370614359173e-6; // 4*pi*1e-7 H/m
    static constexpr double BiotSavartK = 1.0e-7;        // mu0 / (4*pi)

    int sourceCount() const noexcept { return m_sources.size(); }
    const QVector<MagnetostaticSource> &sources() const noexcept { return m_sources; }
    const MagnetostaticSource *source(int index) const;

    int addSource(const MagnetostaticSource &source);
    bool updateSource(int index, const MagnetostaticSource &source);
    bool removeSource(int index);
    void clear();

    MagnetostaticFieldResult fieldAt(const MagnetostaticVec3 &point) const;
    MagnetostaticVec3 lorentzForceAt(const MagnetostaticVec3 &point,
                                     double testChargeC,
                                     const MagnetostaticVec3 &velocityMetersPerSecond) const;

    static QString typeName(MagnetostaticSource::Type type);
    static QString strengthSymbol(MagnetostaticSource::Type type);
    static QString strengthUnit(MagnetostaticSource::Type type);
    static MagnetostaticVec3 axisUnit(const MagnetostaticSource &source);

signals:
    void changed();

private:
    static MagnetostaticFieldResult fieldFromSource(const MagnetostaticSource &source,
                                                     const MagnetostaticVec3 &point);
    static MagnetostaticVec3 loopField(const MagnetostaticVec3 &center,
                                       const MagnetostaticVec3 &axis,
                                       double radius,
                                       double effectiveCurrent,
                                       const MagnetostaticVec3 &point,
                                       int segments,
                                       bool *singular = nullptr);

    QVector<MagnetostaticSource> m_sources;
};
