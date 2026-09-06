#pragma once

#include <string>
#include <vector>

namespace EmCalculator
{
constexpr double pi = 3.141592653589793238462643383279502884;
constexpr double c0 = 299792458.0;
constexpr double epsilon0 = 8.8541878128e-12;
constexpr double mu0 = 1.25663706212e-6;
constexpr double coulombK = 8.9875517923e9;
constexpr double eta0 = 376.730313668;
constexpr double euler = 2.71828182845904523536;
constexpr double planckH = 6.62607015e-34;
constexpr double hbar = 1.054571817e-34;
constexpr double elementaryCharge = 1.602176634e-19;
constexpr double electronMass = 9.1093837139e-31;

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
std::vector<std::pair<std::string, double>> constantTable();

} // namespace EmCalculator
