#pragma once

#include "numerical_em_solvers.h"

#include <complex>
#include <string>
#include <vector>

namespace PecSurfaceMom
{

enum class ExcitationKind
{
    PlaneWave,
    LumpedEdgePort
};

struct Triangle3D
{
    NumericalEM::Vec3 a{};
    NumericalEM::Vec3 b{};
    NumericalEM::Vec3 c{};
    int surfaceIndex = -1;
};

struct PlaneWaveExcitation
{
    // Unit vector in the direction in which the incident wave propagates.
    NumericalEM::Vec3 propagationDirection{0.0, 0.0, -1.0};
    // Desired electric-field direction. The solver projects it onto the plane
    // perpendicular to propagationDirection and normalizes it.
    NumericalEM::Vec3 electricFieldDirection{1.0, 0.0, 0.0};
    std::complex<double> electricFieldAmplitudeVpm{1.0, 0.0};
};

struct LumpedEdgePort
{
    NumericalEM::Vec3 positionM{};
    std::complex<double> voltageV{1.0, 0.0};
    double referenceOhm = 50.0;
};

struct Input
{
    double frequencyHz = 300e6;
    std::vector<Triangle3D> triangles;
    ExcitationKind excitationKind = ExcitationKind::PlaneWave;
    PlaneWaveExcitation excitation{};
    LumpedEdgePort port{};
    int maxUnknowns = 450;
    double vertexMergeToleranceM = 1e-8;
    // Legacy project/API parameter retained for compatibility. Since 5.23, true
    // same-triangle RWG interactions use Duffy singularity extraction and no
    // equivalent-radius self regularization.
    double selfRegularizationFactor = 0.22;
    bool computeRcsCuts = true;
    double rcsCutStepDeg = 2.0;
};

struct RwgBasisResult
{
    int plusTriangle = -1;
    int minusTriangle = -1;
    NumericalEM::Vec3 edgeA{};
    NumericalEM::Vec3 edgeB{};
    NumericalEM::Vec3 edgeCenter{};
    double edgeLengthM = 0.0;
    std::complex<double> coefficientApm{0.0, 0.0};
    std::complex<double> integratedEdgeCurrentA{0.0, 0.0};
};

struct TriangleCurrentResult
{
    NumericalEM::Vec3 centroid{};
    double areaM2 = 0.0;
    std::complex<double> jxApm{0.0, 0.0};
    std::complex<double> jyApm{0.0, 0.0};
    std::complex<double> jzApm{0.0, 0.0};
    double magnitudeApm = 0.0;
};

struct Result
{
    bool valid = false;
    std::string error;
    std::string note;
    double wavelengthM = 0.0;
    int triangleCount = 0;
    int uniqueVertexCount = 0;
    int boundaryEdgeCount = 0;
    int rwgUnknownCount = 0;
    double residualRelative = 0.0;
    double minPivotAbs = 0.0;
    double maxPivotAbs = 0.0;
    double peakSurfaceCurrentApm = 0.0;
    double maxRcsM2 = 0.0;
    double maxRcsAzimuthDeg = 0.0;
    bool drivenPort = false;
    int drivenPortBasisIndex = -1;
    double drivenPortDistanceM = 0.0;
    NumericalEM::Vec3 drivenPortEdgeCenter{};
    std::complex<double> drivenPortVoltageV{0.0,0.0};
    std::complex<double> drivenPortCurrentA{0.0,0.0};
    std::complex<double> inputImpedanceOhm{0.0,0.0};
    std::complex<double> reflectionCoefficient{0.0,0.0};
    double returnLossDb = 0.0;
    double vswr = 1.0;
    double acceptedPowerW = 0.0;
    double radiatedPowerW = 0.0;
    double directivityLinear = 0.0;
    double directivityDbi = 0.0;
    std::vector<RwgBasisResult> bases;
    std::vector<TriangleCurrentResult> triangleCurrents;
    std::vector<double> azimuthDeg;
    std::vector<double> azimuthRcsM2;
    std::vector<double> elevationDeg;
    std::vector<double> elevationRcsM2;
    std::vector<double> azimuthNormalizedFarField;
    std::vector<double> elevationNormalizedFarField;
};

Result solve(const Input &input);

} // namespace PecSurfaceMom
