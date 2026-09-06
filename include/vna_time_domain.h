#pragma once

#include <complex>
#include <cstddef>
#include <string>
#include <vector>

namespace VnaTimeDomain
{
constexpr double C0 = 299792458.0;
constexpr double Pi = 3.1415926535897932384626433832795;

enum class Window
{
    Rectangular,
    Hann,
    Hamming,
    Blackman
};

enum class GateMode
{
    KeepInside,
    RejectInside
};

enum class GateEdge
{
    Rectangular,
    RaisedCosine
};

struct TransformResult
{
    bool valid = false;
    std::string error;
    std::vector<double> frequencyHz;
    std::vector<double> timeS;
    std::vector<std::complex<double>> timeResponse;
    std::vector<double> window;
    double frequencyStepHz = 0.0;
    double frequencySpanHz = 0.0;
    double timeStepS = 0.0;
    double unambiguousTimeS = 0.0;
};

struct Gate
{
    GateMode mode = GateMode::KeepInside;
    GateEdge edge = GateEdge::RaisedCosine;
    double startS = 0.0;
    double stopS = 0.0;
    double transitionS = 0.0;
};

struct Peak
{
    std::size_t index = 0;
    double timeS = 0.0;
    double distanceM = 0.0;
    double magnitude = 0.0;
    double magnitudeDb = 0.0;
};

TransformResult toTimeDomain(const std::vector<double> &frequencyHz,
                             const std::vector<std::complex<double>> &samples,
                             Window window = Window::Hann,
                             double uniformityTolerance = 1e-6);

std::vector<double> gateWeights(const std::vector<double> &timeS, const Gate &gate);
std::vector<std::complex<double>> applyGate(const std::vector<double> &timeS,
                                            const std::vector<std::complex<double>> &timeResponse,
                                            const Gate &gate);

std::vector<std::complex<double>> toFrequencyDomain(const std::vector<std::complex<double>> &timeResponse);

std::vector<Peak> findPeaks(const TransformResult &result,
                            double propagationSpeedMps,
                            bool reflection,
                            std::size_t maxPeaks = 5,
                            std::size_t minimumSeparationSamples = 2);

double propagationSpeedFromVelocityFactor(double velocityFactor);
double propagationSpeedFromEffectiveMedium(double epsilonEffective, double muEffective = 1.0);
double distanceFromDelay(double delayS, double propagationSpeedMps, bool reflection);
double nominalDistanceResolution(double frequencySpanHz, double propagationSpeedMps, bool reflection);
double unambiguousDistance(double frequencyStepHz, double propagationSpeedMps, bool reflection);

} // namespace VnaTimeDomain
