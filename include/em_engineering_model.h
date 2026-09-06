#pragma once

#include <QString>
#include <complex>
#include <cmath>
#include <vector>

struct EmVec3
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    EmVec3 operator+(const EmVec3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
    EmVec3 operator-(const EmVec3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
    EmVec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    EmVec3 operator/(double s) const { return {x / s, y / s, z / s}; }
    double normSquared() const { return x*x + y*y + z*z; }
    double norm() const { return std::sqrt(normSquared()); }
};

inline double emDot(const EmVec3 &a, const EmVec3 &b)
{
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

inline EmVec3 emCross(const EmVec3 &a, const EmVec3 &b)
{
    return {a.y*b.z - a.z*b.y,
            a.z*b.x - a.x*b.z,
            a.x*b.y - a.y*b.x};
}

inline EmVec3 emNormalized(const EmVec3 &v)
{
    const double n = v.norm();
    return n > 1e-18 ? v / n : EmVec3{0.0, 0.0, 1.0};
}

namespace EmEngineering
{
constexpr double Pi = 3.1415926535897932384626433832795;
constexpr double Epsilon0 = 8.8541878128e-12;
constexpr double Mu0 = 1.2566370614359173e-6;
constexpr double C0 = 299792458.0;
constexpr double Eta0 = 376.730313668;
constexpr double CopperResistivity20C = 1.724e-8;

struct MagneticMaterialInput
{
    bool enabled = false;
    double lowFrequencyRelativePermeability = 2000.0;
    double relaxationFrequencyHz = 1.0e6;
    double referenceFrequencyHz = 100.0e3;
    double additionalLossTangentAtReference = 0.0;
    double additionalLossExponent = 0.5;
};

struct MagneticMaterialResult
{
    double muPrime = 1.0;
    double muDoublePrime = 0.0;
    double lossTangent = 0.0;
};

MagneticMaterialResult magneticMaterial(const MagneticMaterialInput &input, double frequencyHz);


struct CoreLossDatasheetPoint
{
    double frequencyHz = 0.0;
    double fluxDensityPeakT = 0.0;
    double powerDensityWPerM3 = 0.0;
};

struct CoreLossInput
{
    double frequencyHz = 100.0e3;
    double fluxDensityPeakT = 0.1;
    double coreVolumeM3 = 1.0e-6;
    double currentRmsA = 1.0;
    bool useSteinmetz = true;
    // Classical Steinmetz form for a sinusoidal excitation:
    // Pv[W/m^3] = k * f[Hz]^alpha * Bpk[T]^beta.
    // Coefficients are therefore unit-system dependent and should come from
    // the selected material datasheet using exactly these SI units.
    double steinmetzK = 1.0;
    double steinmetzAlpha = 1.5;
    double steinmetzBeta = 2.5;
    bool useDatasheetCurve = false;
    std::vector<CoreLossDatasheetPoint> datasheetCurve;
};

struct CoreLossResult
{
    bool valid = false;
    bool usedDatasheetCurve = false;
    double powerDensityWPerM3 = 0.0;
    double totalCoreLossW = 0.0;
    double equivalentSeriesResistanceOhm = 0.0;
    QString note;
};

CoreLossResult coreLoss(const CoreLossInput &input);

struct WindingLossInput
{
    double frequencyHz = 100.0e6;
    double dcResistanceOhm = 0.1;
    double wireDiameterM = 0.5e-3;
    double conductorResistivityOhmM = CopperResistivity20C;
    double proximityExtraAtReference = 0.0; // fractional extra loss, e.g. 0.5 = +50%
    double proximityReferenceFrequencyHz = 100.0e6;
    double proximityExponent = 1.0;
    // Optional Dowell-like multilayer estimate.  It is exact for foil-like
    // layers under the 1-D field assumptions of Dowell and is used here as an
    // engineering approximation for tightly packed round-wire windings.
    bool useDowellMultilayerEstimate = false;
    int layerCount = 1;
    double effectiveLayerConductorThicknessM = 0.0; // <=0 -> use wire diameter
};

struct WindingLossResult
{
    double skinDepthM = 0.0;
    double skinEffectFactor = 1.0;
    double proximityEffectFactor = 1.0;
    double dowellMultilayerFactor = 1.0;
    double acResistanceOhm = 0.0;
};

WindingLossResult windingLoss(const WindingLossInput &input);

struct ReactiveDatasheetPoint
{
    double frequencyHz = 0.0;
    double resistanceOhm = 0.0;
    double reactanceOhm = 0.0;
};

struct PlaneWaveInput
{
    double frequencyHz = 100.0e6;
    double electricAmplitudeVpm = 1.0;
    double phaseDeg = 0.0;
    double epsilonR = 1.0;
    double muR = 1.0;
    EmVec3 propagation{1.0, 0.0, 0.0};
    EmVec3 polarization{0.0, 1.0, 0.0};
    EmVec3 point{0.0, 0.0, 0.0};
    double timeSeconds = 0.0;
};

struct PlaneWaveResult
{
    double waveSpeed = C0;
    double wavelength = 0.0;
    double impedanceOhm = Eta0;
    double betaRadPerMeter = 0.0;
    double omegaRadPerSecond = 0.0;
    EmVec3 electricField;
    EmVec3 magneticField;
    EmVec3 magneticFluxDensity;
    EmVec3 poyntingInstant;
    EmVec3 poyntingAverage;
    QString note;
};

PlaneWaveResult planeWave(const PlaneWaveInput &input);

struct HertzianDipoleInput
{
    double frequencyHz = 100.0e6;
    double currentAmplitudeA = 1.0;
    double elementLengthM = 0.05;
    double phaseDeg = 0.0;
    double epsilonR = 1.0;
    double muR = 1.0;
    EmVec3 center{0.0, 0.0, 0.0};
    EmVec3 axis{0.0, 0.0, 1.0};
    EmVec3 point{5.0, 0.0, 0.0};
    double timeSeconds = 0.0;
};

struct HertzianDipoleResult
{
    double wavelength = 0.0;
    double distanceM = 0.0;
    double thetaDeg = 0.0;
    double radiationResistanceOhm = 0.0;
    EmVec3 electricFieldFar;
    EmVec3 magneticFieldFar;
    EmVec3 magneticFluxDensityFar;
    EmVec3 poyntingAverage;
    bool farFieldLikely = false;
    QString note;
};

HertzianDipoleResult hertzianDipoleFarField(const HertzianDipoleInput &input);

enum class AntennaType
{
    HertzianDipole,
    ShortCenterFedDipole,
    ThinHalfWaveDipole,
    QuarterWaveMonopole,
    FoldedHalfWaveDipole,
    SmallCircularLoop
};

struct AntennaInput
{
    AntennaType type = AntennaType::ThinHalfWaveDipole;
    double frequencyHz = 100.0e6;
    double lengthM = 1.43;
    double wireRadiusM = 0.001;
    int turns = 1;
    double loopRadiusM = 0.1;
    double feedLineOhm = 50.0;
};

struct AntennaResult
{
    double wavelengthM = 0.0;
    double electricalLengthLambda = 0.0;
    double radiationResistanceOhm = 0.0;
    double resistanceOhm = 0.0;
    double reactanceOhm = 0.0;
    bool reactanceDefined = false;
    double reflectionMagnitude = 0.0;
    double returnLossDb = 0.0;
    double vswr = 1.0;
    double recommendedLengthM = 0.0;
    QString validity;
};

AntennaResult antennaEstimate(const AntennaInput &input);

struct SolenoidInput
{
    int turns = 100;
    double radiusM = 0.02;
    double lengthM = 0.08;
    double wireDiameterM = 0.0008;
    double radialBuildM = 0.0008;
    double relativePermeability = 1.0;
    double currentA = 1.0;
    double conductorResistivityOhmM = CopperResistivity20C;
};

struct SolenoidResult
{
    double idealInductanceH = 0.0;
    double wheelerAirCoreInductanceH = 0.0;
    double recommendedInductanceH = 0.0;
    double wireLengthM = 0.0;
    double wireResistanceOhm = 0.0;
    double centerFieldT = 0.0;
    double storedEnergyJ = 0.0;
    double windingPitchM = 0.0;
    QString note;
};

SolenoidResult solenoid(const SolenoidInput &input);

double circularLoopInductance(double loopRadiusM, double wireRadiusM, int turns = 1);

enum class CapacitanceGeometry
{
    ParallelPlates,
    IsolatedSphere,
    ConcentricSpheres,
    CoaxialCylinders,
    TwoWireLine
};

struct CapacitanceInput
{
    CapacitanceGeometry geometry = CapacitanceGeometry::ParallelPlates;
    double epsilonR = 1.0;
    double lossTangent = 0.0;
    double frequencyHz = 1.0e6;
    double areaM2 = 0.01;
    double separationM = 0.001;
    double innerRadiusM = 0.01;
    double outerRadiusM = 0.02;
    double lengthM = 1.0;
    double wireRadiusM = 0.001;
    double wireCenterSpacingM = 0.01;
};

struct CapacitanceResult
{
    double capacitanceF = 0.0;
    double capacitancePerMeterFpm = 0.0;
    double dielectricConductanceS = 0.0;
    double dielectricQualityFactor = 0.0;
    std::complex<double> admittanceS{0.0, 0.0};
    std::complex<double> impedanceOhm{0.0, 0.0};
    bool valid = false;
    QString formula;
    QString note;
};

CapacitanceResult capacitance(const CapacitanceInput &input);

enum class CoupledWindingApplication
{
    TransformerOrCoupledInductor,
    CommonModeChoke,
    DifferentialModeCoupledInductor
};

struct CoupledCoilsInput
{
    double frequencyHz = 50.0;
    double primaryInductanceH = 1.0;
    double secondaryInductanceH = 0.25;
    double couplingCoefficient = 0.98;
    int primaryTurns = 1000;
    int secondaryTurns = 500;
    double primaryResistanceOhm = 2.0;
    double secondaryResistanceOhm = 0.5;
    std::complex<double> loadOhm{10.0, 0.0};
    CoupledWindingApplication application = CoupledWindingApplication::TransformerOrCoupledInductor;

    // Optional shared magnetic-core model. When enabled, L1/L2 are derived from
    // the magnetic reluctance instead of using the manually entered inductances.
    bool useSharedCore = false;
    double coreRelativePermeability = 2000.0;
    double coreEffectiveAreaM2 = 100e-6;
    double corePathLengthM = 0.10;
    double airGapM = 0.0;
    double saturationFluxDensityT = 0.30;

    // Optional frequency-dependent magnetic and winding-loss models.
    bool useFrequencyDependentCore = false;
    MagneticMaterialInput coreMaterial;
    bool useWindingAcLoss = false;
    double primaryWireDiameterM = 0.5e-3;
    double secondaryWireDiameterM = 0.5e-3;
    double windingProximityExtraAtReference = 0.0;
    double windingProximityReferenceFrequencyHz = 100.0e3;
    double windingProximityExponent = 1.0;
    bool useDowellMultilayerEstimate = false;
    int primaryWindingLayers = 1;
    int secondaryWindingLayers = 1;

    // Optional operating-point core-loss estimate (Steinmetz or datasheet).
    // Unlike complex permeability, this is amplitude dependent.
    bool useOperatingPointCoreLoss = false;
    CoreLossInput operatingPointCoreLoss;
};

struct CoupledCoilsResult
{
    double mutualInductanceH = 0.0;
    double turnsRatioN2OverN1 = 0.0;
    double primaryLeakageH = 0.0;
    double secondaryLeakageH = 0.0;
    double effectivePrimaryInductanceH = 0.0;
    double effectiveSecondaryInductanceH = 0.0;
    double magneticReluctanceAtPerH = 0.0;
    double alValueHPerTurn2 = 0.0;
    double estimatedPrimarySaturationCurrentA = 0.0;
    double coreMuPrime = 1.0;
    double coreMuDoublePrime = 0.0;
    double coreLossTangent = 0.0;
    double primaryAcResistanceOhm = 0.0;
    double secondaryAcResistanceOhm = 0.0;
    double primaryCoreLossSeriesResistanceOhm = 0.0;
    double secondaryCoreLossSeriesResistanceOhm = 0.0;
    double operatingPointCoreLossW = 0.0;
    double operatingPointEquivalentPrimaryResistanceOhm = 0.0;
    double modalHighInductanceH = 0.0;
    double modalLowInductanceH = 0.0;
    std::complex<double> commonModeImpedanceOhm{0.0, 0.0};
    std::complex<double> differentialModeImpedanceOhm{0.0, 0.0};
    std::complex<double> reflectedImpedanceOhm{0.0, 0.0};
    std::complex<double> inputImpedanceOhm{0.0, 0.0};
    std::complex<double> secondaryCurrentPerPrimaryAperA{0.0, 0.0};
    QString note;
};

CoupledCoilsResult coupledCoils(const CoupledCoilsInput &input);

enum class BalunTopology
{
    CurrentBalun1to1,
    Guanella4to1,
    Ruthroff4to1,
    TransformerBalunOrUnun
};

struct BalunInput
{
    BalunTopology topology = BalunTopology::CurrentBalun1to1;
    double frequencyHz = 10e6;
    double sourceResistanceOhm = 50.0;
    std::complex<double> loadOhm{50.0, 0.0};
    double secondaryToPrimaryTurnsRatio = 1.0;
    double magnetizingInductanceH = 100e-6;
    double leakageInductanceH = 0.5e-6;
    double windingResistanceOhm = 0.2;
    double parasiticCapacitanceF = 5e-12;
    bool useFrequencyDependentCore = false;
    MagneticMaterialInput coreMaterial;
    bool useWindingAcLoss = false;
    double wireDiameterM = 0.5e-3;
    double windingProximityExtraAtReference = 0.0;
    double windingProximityReferenceFrequencyHz = 10.0e6;
    double windingProximityExponent = 1.0;
    bool useDowellMultilayerEstimate = false;
    int windingLayers = 1;
    bool useOperatingPointCoreLoss = false;
    CoreLossInput operatingPointCoreLoss;
};

struct BalunResult
{
    double voltageRatioSecondaryOverPrimary = 1.0;
    double impedanceRatioSecondaryOverPrimary = 1.0;
    std::complex<double> idealReferredLoadOhm{0.0, 0.0};
    std::complex<double> inputImpedanceOhm{0.0, 0.0};
    std::complex<double> reflectionCoefficient{0.0, 0.0};
    double returnLossDb = 0.0;
    double vswr = 1.0;
    std::complex<double> commonModeChokingImpedanceOhm{0.0, 0.0};
    double approximateSelfResonanceHz = 0.0;
    double effectiveMagnetizingInductanceH = 0.0;
    double windingAcResistanceOhm = 0.0;
    double coreLossSeriesResistanceOhm = 0.0;
    double operatingPointCoreLossW = 0.0;
    double operatingPointEquivalentSeriesResistanceOhm = 0.0;
    double coreMuPrime = 1.0;
    double coreMuDoublePrime = 0.0;
    double coreLossTangent = 0.0;
    QString note;
};

BalunResult balun(const BalunInput &input);

enum class InductiveAntennaGeometry
{
    CircularLoop,
    SolenoidalCoil,
    PlanarCircularSpiral
};

struct InductiveAntennaInput
{
    InductiveAntennaGeometry geometry = InductiveAntennaGeometry::CircularLoop;
    double frequencyHz = 13.56e6;
    int turns = 3;
    double radiusM = 0.025;
    double solenoidLengthM = 0.03;
    double wireDiameterM = 0.001;
    double innerDiameterM = 0.02;
    double traceWidthM = 0.001;
    double traceSpacingM = 0.0005;
    double copperThicknessM = 35e-6;
    double currentA = 1.0;
    double axialObservationM = 0.02;
    double parasiticCapacitanceF = 5e-12;
    double feedLineOhm = 50.0;
    double conductorResistivityOhmM = CopperResistivity20C;
};

struct InductiveAntennaResult
{
    double wavelengthM = 0.0;
    double outerDiameterM = 0.0;
    double effectiveAreaM2Turns = 0.0;
    double magneticMomentAm2 = 0.0;
    double inductanceH = 0.0;
    double wireLengthM = 0.0;
    double dcResistanceOhm = 0.0;
    double acResistanceOhm = 0.0;
    double skinDepthM = 0.0;
    double radiationResistanceOhm = 0.0;
    std::complex<double> inputImpedanceOhm{0.0,0.0};
    double unloadedQ = 0.0;
    double selfResonanceHz = 0.0;
    double axialFieldT = 0.0;
    double reactiveNearFieldScaleM = 0.0;
    double circumferenceLambda = 0.0;
    QString note;
};

InductiveAntennaResult inductiveAntenna(const InductiveAntennaInput &input);

enum class TransmissionLineGeometry
{
    Microstrip,
    SymmetricStripline
};

struct TransmissionLineInput
{
    TransmissionLineGeometry geometry = TransmissionLineGeometry::Microstrip;
    double frequencyHz = 1.0e9;
    double traceWidthM = 1.5e-3;
    double copperThicknessM = 35e-6;
    double dielectricHeightM = 0.8e-3;
    double epsilonR = 4.2;
    double lossTangent = 0.018;
    double lineLengthM = 0.05;
    double conductorResistivityOhmM = CopperResistivity20C;
    std::complex<double> loadOhm{50.0,0.0};
};

struct TransmissionLineResult
{
    bool valid = false;
    double effectiveWidthM = 0.0;
    double effectivePermittivity = 1.0;
    double characteristicImpedanceOhm = 0.0;
    double guidedWavelengthM = 0.0;
    double phaseVelocityMps = 0.0;
    double phaseConstantRadPerM = 0.0;
    double electricalLengthDeg = 0.0;
    double skinDepthM = 0.0;
    double surfaceResistanceOhm = 0.0;
    double conductorLossDbPerM = 0.0;
    double dielectricLossDbPerM = 0.0;
    double totalLossDb = 0.0;
    std::complex<double> inputImpedanceOhm{0.0,0.0};
    QString note;
};

TransmissionLineResult transmissionLine(const TransmissionLineInput &input);

enum class FilterTopology
{
    LowPassRlc,
    HighPassRlc,
    BandPassSeriesRlc,
    NotchSeriesRlc,
    ShuntSeriesLcTrap
};

struct FilterDesignInput
{
    FilterTopology topology = FilterTopology::ShuntSeriesLcTrap;
    double centerFrequencyHz = 100.0e6;
    double qualityFactor = 10.0;
    double referenceResistanceOhm = 50.0;
    double preferredInductanceH = 100e-9;
    double sourceResistanceOhm = 50.0;
    double loadResistanceOhm = 50.0;
};

struct FilterDesignResult
{
    bool valid = false;
    double inductanceH = 0.0;
    double capacitanceF = 0.0;
    double dampingOrLossResistanceOhm = 0.0;
    double resonantFrequencyHz = 0.0;
    double nominalBandwidthHz = 0.0;
    QString topologyDescription;
    QString note;
};

FilterDesignResult designFilter(const FilterDesignInput &input);
std::complex<double> filterTransfer(const FilterDesignInput &input, double frequencyHz,
                                    FilterDesignResult *design = nullptr);


enum class ReactiveComponentType
{
    None,
    Inductor,
    Capacitor
};

// First-order RF parasitic model shared by the component explorer and the
// cascaded RF-chain calculator.  Parameters that are not relevant to the
// selected component type are ignored.
struct ReactiveComponentParasitics
{
    bool enabled = false;
    double seriesResistanceOhm = 0.0;          // winding/contact ESR at low frequency
    double qualityFactorAtReference = 0.0;     // optional extra frequency-dependent loss model
    double referenceFrequencyHz = 100.0e6;
    double resistanceFrequencyExponent = 0.5;  // ~sqrt(f) skin/proximity trend by default
    double parasiticCapacitanceF = 0.0;        // inductor self-capacitance
    double parasiticInductanceH = 0.0;         // capacitor ESL
    double lossTangent = 0.0;                  // capacitor dielectric loss
    double parallelResistanceOhm = 0.0;        // inductor/core or package parallel loss

    // Optional physical round-wire / proximity estimate for an inductor.
    bool usePhysicalWindingLoss = false;
    double wireDiameterM = 0.5e-3;
    double conductorResistivityOhmM = CopperResistivity20C;
    double proximityExtraAtReference = 0.0;
    double proximityReferenceFrequencyHz = 100.0e6;
    double proximityExponent = 1.0;
    bool useDowellMultilayerEstimate = false;
    int windingLayers = 1;

    // Optional amplitude-dependent Steinmetz / datasheet core-loss operating
    // point.  The equivalent resistance is derived from Pcore / Irms^2.
    bool useOperatingPointCoreLoss = false;
    CoreLossInput operatingPointCoreLoss;

    // Optional complex-permeability approximation. Nominal L is interpreted
    // at magneticMaterial.referenceFrequencyHz and is scaled by mu'(f).
    bool useMagneticMaterialModel = false;
    MagneticMaterialInput magneticMaterial;

    // Optional directly measured/datasheet complex impedance curve. When
    // enabled with >=2 points it takes precedence over the lumped equivalent.
    bool useDatasheetCurve = false;
    std::vector<ReactiveDatasheetPoint> datasheetCurve;
};

struct ReactiveComponentResult
{
    std::complex<double> impedanceOhm{0.0,0.0};
    double effectiveQualityFactor = 0.0;
    double approximateSelfResonanceHz = 0.0;
    double effectiveSeriesResistanceOhm = 0.0;
    double effectiveInductanceH = 0.0;
    double magneticMuPrime = 1.0;
    double magneticMuDoublePrime = 0.0;
    double magneticLossTangent = 0.0;
    double skinDepthM = 0.0;
    double skinEffectFactor = 1.0;
    double proximityEffectFactor = 1.0;
    double dowellMultilayerFactor = 1.0;
    double operatingPointCoreLossW = 0.0;
    bool usedDatasheetCurve = false;
    QString note;
};

ReactiveComponentResult reactiveComponent(ReactiveComponentType type,
                                          double value,
                                          double frequencyHz,
                                          const ReactiveComponentParasitics &parasitics = {});

enum class LMatchOrder
{
    SeriesThenShunt,
    ShuntThenSeries
};

struct LMatchSolution
{
    bool valid = false;
    LMatchOrder order = LMatchOrder::SeriesThenShunt;
    double seriesReactanceOhm = 0.0;       // at the synthesis frequency
    double shuntSusceptanceSiemens = 0.0;  // at the synthesis frequency
    ReactiveComponentType seriesComponent = ReactiveComponentType::None;
    ReactiveComponentType shuntComponent = ReactiveComponentType::None;
    double seriesValue = 0.0;              // H for L, F for C
    double shuntValue = 0.0;               // H for L, F for C
    std::complex<double> matchedInputOhm{0.0,0.0};
    QString description;
};

std::vector<LMatchSolution> synthesizeLMatch(const std::complex<double> &loadOhm,
                                             double referenceOhm,
                                             double frequencyHz);

struct RfChainInput
{
    double frequencyHz = 100.0e6;
    double referenceOhm = 50.0;
    std::complex<double> antennaImpedanceOhm{50.0,0.0};

    bool trapEnabled = false;
    double trapInductanceH = 100e-9;
    double trapCapacitanceF = 25.33029591e-12;
    double trapQualityFactor = 50.0;
    double trapReferenceFrequencyHz = 100.0e6;

    bool matchingEnabled = false;
    LMatchSolution matching;
    bool nonIdealMatchingComponents = false;
    ReactiveComponentParasitics matchingInductorParasitics;
    ReactiveComponentParasitics matchingCapacitorParasitics;

    bool nonIdealTrapComponents = false;
    ReactiveComponentParasitics trapInductorParasitics;
    ReactiveComponentParasitics trapCapacitorParasitics;

    bool lineEnabled = false;
    TransmissionLineInput line;
};

struct RfChainResult
{
    bool valid = false;
    std::complex<double> rawAntennaImpedanceOhm{0.0,0.0};
    std::complex<double> antennaWithTrapOhm{0.0,0.0};
    std::complex<double> afterMatchingOhm{0.0,0.0};
    std::complex<double> sourceInputOhm{0.0,0.0};
    std::complex<double> reflectionCoefficient{0.0,0.0};
    double returnLossDb = 0.0;
    double vswr = 1.0;
    double lineCharacteristicImpedanceOhm = 0.0;
    double lineLossDb = 0.0;
    QString note;
};

std::complex<double> reactiveSeriesImpedance(ReactiveComponentType type,
                                             double value,
                                             double frequencyHz);
std::complex<double> reactiveShuntAdmittance(ReactiveComponentType type,
                                              double value,
                                              double frequencyHz);
std::complex<double> shuntSeriesLcTrapImpedance(double inductanceH,
                                                double capacitanceF,
                                                double qualityFactor,
                                                double referenceFrequencyHz,
                                                double frequencyHz);
RfChainResult rfChain(const RfChainInput &input);

enum class WidebandMatchObjective
{
    MeanReflectionMagnitude,
    WorstReflectionMagnitude
};

struct WidebandMatchSample
{
    double frequencyHz = 0.0;
    std::complex<double> antennaImpedanceOhm{50.0,0.0};
};

struct WidebandMatchOptimizationInput
{
    RfChainInput chainTemplate;
    std::vector<WidebandMatchSample> samples;
    WidebandMatchObjective objective = WidebandMatchObjective::WorstReflectionMagnitude;
    double initialRelativeSpan = 0.50; // +/- 50 % around the current values
    int samplesPerCoordinate = 9;
    int passes = 4;
};

struct WidebandMatchOptimizationResult
{
    bool valid = false;
    LMatchSolution initialMatching;
    LMatchSolution optimizedMatching;
    double initialObjective = 0.0;
    double optimizedObjective = 0.0;
    std::vector<double> objectiveHistory;
    QString note;
};

WidebandMatchOptimizationResult optimizeWidebandLMatch(const WidebandMatchOptimizationInput &input);

} // namespace EmEngineering
