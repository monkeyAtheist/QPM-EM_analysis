#pragma once

#include <array>
#include <complex>
#include <string>
#include <vector>

namespace NumericalEM
{

constexpr double Pi = 3.1415926535897932384626433832795;
constexpr double Mu0 = 1.2566370614359173e-6;
constexpr double Epsilon0 = 8.8541878128e-12;
constexpr double C0 = 299792458.0;

struct Vec2
{
    double x = 0.0;
    double y = 0.0;
};

struct Vec3
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

Vec3 operator+(const Vec3 &a, const Vec3 &b);
Vec3 operator-(const Vec3 &a, const Vec3 &b);
Vec3 operator*(const Vec3 &a, double s);
double dot(const Vec3 &a, const Vec3 &b);
double norm(const Vec3 &a);
Vec3 normalized(const Vec3 &a);
Vec3 axisFromAzimuthElevationDeg(double azimuthDeg, double elevationDeg);

enum class ConductorShape
{
    Circle,
    Rectangle
};

struct BemConductor2D
{
    ConductorShape shape = ConductorShape::Circle;
    Vec2 center{};
    double radiusM = 0.01;
    double widthM = 0.02;
    double heightM = 0.02;
    double potentialV = 0.5;
};

struct BemPanel2D
{
    Vec2 p0{};
    Vec2 p1{};
    Vec2 midpoint{};
    double lengthM = 0.0;
    int conductorIndex = 0;
    double surfaceChargeDensityCpm2 = 0.0;
};

struct BemInput2D
{
    BemConductor2D conductorA{};
    BemConductor2D conductorB{};
    double epsilonR = 1.0;
    int panelsPerConductor = 48;
};

struct BemResult2D
{
    bool valid = false;
    std::string error;
    std::vector<BemPanel2D> panels;
    std::array<double, 2> totalChargePerLengthCpm{0.0, 0.0};
    double capacitancePerLengthFpm = 0.0;
    double voltageDifferenceV = 0.0;
    double commonPotentialOffsetV = 0.0;
    double maxBoundaryResidualV = 0.0;
    double minPivotAbs = 0.0;
    double maxPivotAbs = 0.0;
    std::string note;
};

BemResult2D solveTwoConductorBem2D(const BemInput2D &input);
double bemPotentialV(const BemResult2D &result, double epsilonR, const Vec2 &point);
Vec2 bemElectricFieldVpm(const BemResult2D &result, double epsilonR, const Vec2 &point);
double analyticTwoWireCapacitancePerLength(double radiusM, double centerSpacingM, double epsilonR = 1.0);

struct ThinWireMomInput
{
    double frequencyHz = 100e6;
    double lengthM = 1.425;
    double radiusM = 0.001;
    int segments = 41;
    double feedVoltageV = 1.0;
};

struct ThinWireMomResult
{
    bool valid = false;
    std::string error;
    double wavelengthM = 0.0;
    double electricalLengthLambda = 0.0;
    double segmentLengthM = 0.0;
    std::complex<double> inputImpedanceOhm{0.0, 0.0};
    double feedCurrentMagnitudeA = 0.0;
    double residualRelative = 0.0;
    double directivityLinear = 0.0;
    double directivityDbi = 0.0;
    double minPivotAbs = 0.0;
    double maxPivotAbs = 0.0;
    std::vector<double> zM;
    std::vector<std::complex<double>> currentA;
    std::vector<double> thetaDeg;
    std::vector<double> normalizedFarField;
    std::string note;
};

ThinWireMomResult solveThinWireDipolePocklington(const ThinWireMomInput &input);

// Generalized thin-wire network solver used by the antenna geometry designer.
// Geometry is represented by straight PEC wire spans in 3D. Connected spans are
// adaptively meshed and solved with pulse-current basis functions, tangential point
// matching and the generalized dyadic free-space Pocklington/EFIE kernel. T/Y/X
// centerline junctions are supported through localized KCL constraints introduced
// with Lagrange multipliers; disconnected parasitic elements remain supported.
struct WireSpan3D
{
    std::string name;
    Vec3 aM{};
    Vec3 bM{};
    double radiusM = 0.001;
};

struct WireFeed3D
{
    std::string name;
    Vec3 positionM{};
    std::complex<double> voltageV{1.0, 0.0};
    double referenceOhm = 50.0;
};

enum class WireJunctionTreatment
{
    ReducedBasis = 0,
    LagrangeKcl = 1
};

// Trial/test current model for the generalized wire solver. PulsePointMatching is
// the historical constant-current pulse formulation. LinearRooftopCharge creates
// node-centred piecewise-linear current functions: current vanishes naturally at
// open ends, is continuous through ordinary degree-2 nodes and spans d-1 KCL-safe
// modes at a degree-d T/Y/X junction.
enum class WireCurrentBasisTreatment
{
    PulsePointMatching = 0,
    LinearRooftopCharge = 1
};

struct WireNetworkMomInput
{
    double frequencyHz = 100e6;
    std::vector<WireSpan3D> wires;
    std::vector<WireFeed3D> feeds;
    int segmentsPerWavelength = 80;
    int maxUnknowns = 450;
    double nodeMergeToleranceM = 1e-6;
    double maxSegmentLengthRadiusFactor = 3.5;
    int junctionLocalSubdivisions = 3; // refine only the first nominal pulse on every T/Y/X arm
    // ReducedBasis eliminates the independent KCL relations from the algebraic unknowns and
    // solves in a current subspace that satisfies branch continuity by construction. LagrangeKcl
    // preserves the historical saddle-point formulation for comparison/validation.
    WireJunctionTreatment junctionTreatment = WireJunctionTreatment::ReducedBasis;
    WireCurrentBasisTreatment currentBasisTreatment = WireCurrentBasisTreatment::PulsePointMatching;
    bool computeFarField = true;
    double farFieldCutStepDeg = 1.0;
    double farFieldIntegrationStepDeg = 5.0;
};

struct WireNetworkMeshSegment
{
    Vec3 p0M{};
    Vec3 p1M{};
    Vec3 centerM{};
    Vec3 tangent{};
    double lengthM = 0.0;
    double radiusM = 0.0;
    int componentIndex = -1;
    int sourceWireIndex = -1;
    double pathCenterM = 0.0;
    double sourceWirePathCenterM = 0.0;
    std::complex<double> currentA{0.0, 0.0};
    std::complex<double> currentAtP0A{0.0, 0.0};
    std::complex<double> currentAtP1A{0.0, 0.0};
    std::complex<double> lineChargeDensityCpm{0.0, 0.0};
};

struct WireNetworkChargeNode
{
    Vec3 positionM{};
    int componentIndex = -1;
    int degree = 0;
    bool openEndpoint = false;
    bool branchedJunction = false;
    std::complex<double> lumpedContinuityChargeC{0.0, 0.0};
    std::complex<double> equivalentLineChargeCpm{0.0, 0.0};
};

struct WireNetworkFeedResult
{
    std::string name;
    Vec3 positionM{};
    int componentIndex = -1;
    int meshNodeIndex = -1;
    std::complex<double> voltageV{0.0, 0.0};
    std::complex<double> currentA{0.0, 0.0};
    std::complex<double> activeImpedanceOhm{0.0, 0.0};
    double referenceOhm = 50.0;
    std::complex<double> reflectionCoefficient{0.0, 0.0};
    double returnLossDb = 0.0;
    double vswr = 1.0;
};

struct WireNetworkMomResult
{
    bool valid = false;
    std::string error;
    std::string note;
    double wavelengthM = 0.0;
    int meshSegmentCount = 0;
    int wireUnknownCount = 0;
    int solvedWireDofCount = 0;
    int junctionConstraintCount = 0; // independent KCL relations (rank)
    int redundantJunctionConstraintCount = 0;
    int unknownCount = 0; // algebraic current DOFs (+ legacy KCL multipliers for pulse mode)
    bool reducedJunctionBasisUsed = false;
    bool linearRooftopBasisUsed = false;
    int currentConstraintCount = 0;
    int ordinaryContinuityConstraintCount = 0;
    int openEndConstraintCount = 0;
    int componentCount = 0;
    int branchedJunctionCount = 0;
    int branchedArmCount = 0;
    int junctionRefinedExtraPulseCount = 0;
    double minMeshSegmentM = 0.0;
    double maxMeshSegmentM = 0.0;
    double maxMeshSegmentToRadius = 0.0;
    double residualRelative = 0.0;
    double maxJunctionCurrentDiscontinuity = 0.0;
    double maxBranchKclResidualA = 0.0;
    double maxBranchKclRelative = 0.0;
    double maxOpenEndCurrentA = 0.0;
    double maxOpenEndCurrentRelative = 0.0;
    double maxLineChargeDensityCpm = 0.0;
    std::complex<double> netContinuityChargeC{0.0, 0.0};
    double minPivotAbs = 0.0;
    double maxPivotAbs = 0.0;
    double directivityLinear = 0.0;
    double directivityDbi = 0.0;
    double radiatedPowerW = 0.0;
    double acceptedPowerW = 0.0;
    std::vector<WireNetworkMeshSegment> meshSegments;
    std::vector<WireNetworkChargeNode> chargeNodes;
    std::vector<WireNetworkFeedResult> feeds;
    // Radiation convention shared by wire/hybrid results since 5.28:
    // theta=0 deg -> +Z, theta=90 deg -> XY plane, phi=0 deg -> +X.
    // Both 2D cuts and the full-sphere samples are normalized to the same global peak.
    std::vector<double> azimuthDeg; // phi in degrees at theta=90 deg
    std::vector<double> azimuthNormalizedFarField;
    std::vector<double> elevationDeg; // spherical theta in degrees at phi=0 deg
    std::vector<double> elevationNormalizedFarField;
    // Full-sphere normalized far-field grid used by the interactive radiation-pattern viewer.
    // farFieldNormalized is flattened in theta-major order: index = thetaIndex * farFieldPhiDeg.size() + phiIndex.
    std::vector<double> farFieldThetaDeg;
    std::vector<double> farFieldPhiDeg;
    std::vector<double> farFieldNormalized;
    double maxRadiationThetaDeg = 0.0;
    double maxRadiationPhiDeg = 0.0;
};

WireNetworkMomResult solveWireNetworkMom(const WireNetworkMomInput &input);

struct CircularLoopGeometry
{
    Vec3 center{};
    Vec3 axis{0.0, 0.0, 1.0};
    double radiusM = 0.05;
    double wireRadiusM = 0.0005;
    int turns = 1;
};

struct LoopCouplingInput
{
    CircularLoopGeometry primary{};
    CircularLoopGeometry secondary{};
    int integrationSegments = 120;
    // First-order uniform/effective magnetic-medium approximation. A localized
    // ferrite core with fringing requires a magnetostatic/FEM solution.
    double relativePermeability = 1.0;
};

struct LoopCouplingResult
{
    bool valid = false;
    std::string error;
    double mutualInductanceH = 0.0;
    double primarySelfInductanceH = 0.0;
    double secondarySelfInductanceH = 0.0;
    double couplingCoefficient = 0.0;
    double rawCouplingCoefficient = 0.0;
    double minimumSegmentDistanceM = 0.0;
    std::string note;
};

LoopCouplingResult solveCircularLoopCoupling(const LoopCouplingInput &input);
double thinCircularLoopSelfInductance(double radiusM, double wireRadiusM, int turns = 1);

} // namespace NumericalEM
