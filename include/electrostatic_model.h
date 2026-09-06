#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include <cmath>

struct ElectrostaticVec3
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    ElectrostaticVec3 operator+(const ElectrostaticVec3 &other) const { return {x + other.x, y + other.y, z + other.z}; }
    ElectrostaticVec3 operator-(const ElectrostaticVec3 &other) const { return {x - other.x, y - other.y, z - other.z}; }
    ElectrostaticVec3 operator*(double scalar) const { return {x * scalar, y * scalar, z * scalar}; }
    ElectrostaticVec3 operator/(double scalar) const { return {x / scalar, y / scalar, z / scalar}; }
    ElectrostaticVec3 &operator+=(const ElectrostaticVec3 &other) { x += other.x; y += other.y; z += other.z; return *this; }
    ElectrostaticVec3 &operator-=(const ElectrostaticVec3 &other) { x -= other.x; y -= other.y; z -= other.z; return *this; }

    double normSquared() const { return x * x + y * y + z * z; }
    double norm() const { return std::sqrt(normSquared()); }
};

struct ElectrostaticFieldResult
{
    ElectrostaticVec3 electricField;
    double potential = 0.0;
    bool potentialDefined = true;
    bool singular = false;
    QString note;
};

struct ElectrostaticSource
{
    enum class Type
    {
        // Keep the original first six values stable: older UI/project code used the enum index.
        PointCharge,
        SolidSphere,
        SphericalShell,
        InfiniteCylinderZ,
        FiniteLine,
        RectangularPlate,

        // 5.19 educational source library.
        ThickSphericalShell,
        InfiniteHollowCylinderZ,
        InfiniteLine,
        CircularPlate,
        AnnularPlate,
        TriangularPlate,
        InfinitePlane,

        // 5.19.1 finite-volume rectangular plate/slab. Appended to preserve all previous enum indices.
        RectangularVolume
    };

    QString name;
    Type type = Type::PointCharge;
    ElectrostaticVec3 position;

    // Point/sphere/finite line/finite plate: total charge Q [C].
    // Infinite line/cylinders: linear charge density lambda [C/m].
    // Infinite plane: surface charge density sigma [C/m^2].
    double strength = 1.0e-9;

    double radius = 0.25;       // outer sphere/cylinder/disk radius [m]
    double innerRadius = 0.12;  // hollow core / annulus inner radius [m]
    double length = 1.0;        // finite line [m]
    double width = 1.0;         // rectangular/triangular plate width [m]
    double height = 0.6;        // rectangular/triangular plate height [m]
    double thickness = 0.1;     // finite-thickness rectangular charged slab [m]
    double angleDeg = 0.0;      // line/finite plate rotation in XY plane
};

class ElectrostaticModel final : public QObject
{
    Q_OBJECT

public:
    explicit ElectrostaticModel(QObject *parent = nullptr);

    static constexpr double Epsilon0 = 8.8541878128e-12;
    static constexpr double CoulombK = 8.9875517923e9;

    int sourceCount() const noexcept { return m_sources.size(); }
    const QVector<ElectrostaticSource> &sources() const noexcept { return m_sources; }
    const ElectrostaticSource *source(int index) const;

    int addSource(const ElectrostaticSource &source);
    bool updateSource(int index, const ElectrostaticSource &source);
    bool removeSource(int index);
    void clear();

    ElectrostaticFieldResult fieldAt(const ElectrostaticVec3 &point) const;
    ElectrostaticVec3 forceAt(const ElectrostaticVec3 &point, double testChargeC) const;

    static QString typeName(ElectrostaticSource::Type type);
    static QString strengthSymbol(ElectrostaticSource::Type type);
    static QString strengthUnit(ElectrostaticSource::Type type);

signals:
    void changed();

private:
    static ElectrostaticFieldResult fieldFromSource(const ElectrostaticSource &source,
                                                    const ElectrostaticVec3 &point);

    QVector<ElectrostaticSource> m_sources;
};
