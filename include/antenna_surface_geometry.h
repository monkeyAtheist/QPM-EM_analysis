#pragma once

#include "numerical_em_solvers.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace AntennaSurface
{

enum class SurfaceKind
{
    Rectangle = 0,
    Disk = 1,
    Cylinder = 2,
    Cone = 3,
    Paraboloid = 4,
    InsetPatch = 5
};

struct SurfaceSpec
{
    std::string name;
    SurfaceKind kind = SurfaceKind::Rectangle;
    NumericalEM::Vec3 center{};
    int baseOrientation = 0; // 0=XY, 1=XZ, 2=YZ
    double yawDeg = 0.0;     // world Z rotation, applied after base orientation
    double pitchDeg = 0.0;   // world Y rotation
    double rollDeg = 0.0;    // world X rotation
    double radiusM = 0.25;
    double innerRadiusM = 0.0;
    double widthM = 0.50;
    double heightM = 0.30;
    double focalLengthM = 0.25;
    // InsetPatch only: patch body uses widthM x heightM. A centered microstrip
    // feed enters from the local -v edge through a notch and contacts the
    // patch at insetDepthM. feedLengthM is the length outside the patch edge.
    double feedWidthM = 0.003;
    double feedLengthM = 0.020;
    double insetDepthM = 0.0;
    double notchGapM = 0.0005;
    double meshHintM = 0.05;
};

struct SurfaceTriangle
{
    NumericalEM::Vec3 a{};
    NumericalEM::Vec3 b{};
    NumericalEM::Vec3 c{};
};

struct SurfaceMeshStats
{
    std::size_t triangleCount = 0;
    double areaM2 = 0.0;
    double maxEdgeM = 0.0;
};

struct SurfaceFrame
{
    NumericalEM::Vec3 u{1.0,0.0,0.0};
    NumericalEM::Vec3 v{0.0,1.0,0.0};
    NumericalEM::Vec3 normal{0.0,0.0,1.0};
};

// Orthonormal local frame after base-orientation + yaw/pitch/roll.
SurfaceFrame frameAxes(const SurfaceSpec &spec);
NumericalEM::Vec3 vectorFromLocal(const SurfaceSpec &spec, double u, double v, double normal = 0.0);
NumericalEM::Vec3 localFromPoint(const SurfaceSpec &spec, const NumericalEM::Vec3 &worldPoint);

NumericalEM::Vec3 pointFromLocal(const SurfaceSpec &spec, double u, double v, double normal = 0.0);
std::vector<SurfaceTriangle> triangulate(const SurfaceSpec &spec, std::size_t maxTriangles = 6000);
SurfaceMeshStats meshStats(const std::vector<SurfaceTriangle> &triangles);
const char *kindName(SurfaceKind kind);

} // namespace AntennaSurface
