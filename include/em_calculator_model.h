#pragma once

#include <string>
#include <vector>

namespace EmCalculator
{
constexpr double pi = 3.141592653589793238462643383279502884;
constexpr double c0 = 299792458.0;
constexpr double epsilon0 = 8.8541878188e-12; // CODATA 2022; measured
constexpr double mu0 = 1.25663706127e-6; // CODATA 2022; measured
constexpr double coulombK = 1.0 / (4.0 * pi * epsilon0);
constexpr double eta0 = mu0 * c0;
constexpr double euler = 2.71828182845904523536;
constexpr double planckH = 6.62607015e-34;
constexpr double hbar = planckH / (2.0 * pi);
constexpr double elementaryCharge = 1.602176634e-19;
constexpr double electronMass = 9.1093837139e-31; // CODATA 2022
constexpr double protonMass = 1.67262192595e-27; // CODATA 2022
constexpr double boltzmannKB = 1.380649e-23; // Exact SI
constexpr double avogadroNA = 6.02214076e23; // Exact SI
constexpr double molarGasR = boltzmannKB * avogadroNA;
constexpr double faradayF = elementaryCharge * avogadroNA;
constexpr double gravitationalG = 6.67430e-11;
constexpr double evJoule = elementaryCharge; // 1 eV in joules
constexpr double stefanBoltzmann = 5.670374419e-8;
constexpr double copperConductivity = 5.8e7; // Typical engineering value, not universal

struct ConstantInfo
{
    std::string symbol;       // ASCII identifier accepted by the expression parser
    double value = 0.0;       // coherent SI units
    std::string unit;
    std::string name;
    std::string category;
    std::string aliases;      // optional comma-separated ASCII identifiers
    std::string description;  // uncertainty/assumptions and applications
};

// One registry used by the expression parser, catalogue, and tests.
const std::vector<ConstantInfo> &physicalConstants();

struct WaveResult
{
    double frequencyHz = 0.0;
    double wavelengthM = 0.0;
    double phaseVelocityMS = 0.0;
    double periodS = 0.0;
    double angularFrequencyRadS = 0.0;
    double waveNumberRadM = 0.0;
};

bool waveFromFrequency(double frequencyHz, double relativePermittivity, double relativePermeability,
                       WaveResult &result, std::string &error);
bool waveFromWavelength(double wavelengthM, double relativePermittivity, double relativePermeability,
                        WaveResult &result, std::string &error);

enum class AntennaKind
{
    ExactHalfWaveDipole,
    ResonantDipole0475,
    QuarterWaveMonopole,
    GroundPlaneQuarterWave,
    InvertedVee,
    FoldedDipole,
    FiveEighthMonopole,
    FullWaveLoop,
    Jpole,
    SlimJim,
    AxialModeHelix,
    Yagi3Element,
    RectangularPatch,
    ParabolicDish
};

struct AntennaInputs
{
    double frequencyHz = 144.0e6;
    double primaryDimensionM = 0.0;
    double velocityFactor = 1.0;
    double relativePermittivity = 4.2;
    double substrateHeightM = 1.6e-3;
    double dishDiameterM = 1.0;
    double dishEfficiency = 0.60;
    double focalRatio = 0.40;
    double desiredGainDbi = 20.0;
};

struct ResultItem
{
    std::string label;
    double value = 0.0;
    std::string unit;
    std::string note;
};

struct AntennaResult
{
    double frequencyHz = 0.0;
    double wavelengthM = 0.0;
    std::string primaryDimensionLabel;
    double primaryDimensionM = 0.0;
    std::vector<ResultItem> items;
    std::string assumption;
};

const char *antennaName(AntennaKind kind);
const char *primaryDimensionName(AntennaKind kind);
bool antennaFromFrequency(AntennaKind kind, const AntennaInputs &inputs, AntennaResult &result, std::string &error);
bool antennaFromPrimaryDimension(AntennaKind kind, const AntennaInputs &inputs, AntennaResult &result, std::string &error);

struct ExpressionResult
{
    double value = 0.0;
    std::string error;
    std::size_t errorPosition = 0;
    bool ok = false;
};

ExpressionResult evaluateExpression(const std::string &expression);
// ans is only enabled when previousResult is finite; otherwise ans gives a clear error.
ExpressionResult evaluateExpression(const std::string &expression, double previousResult);
std::vector<std::pair<std::string, double>> constantTable();

} // namespace EmCalculator
