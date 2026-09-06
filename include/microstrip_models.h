#pragma once

#include <complex>

namespace MicrostripModels
{

struct LineResult
{
    bool valid = false;
    double widthM = 0.0;
    double heightM = 0.0;
    double relativePermittivity = 1.0;
    double effectivePermittivity = 1.0;
    double characteristicImpedanceOhm = 0.0;
    double guidedWavelengthM = 0.0;
    double electricalLengthDeg = 0.0;
    double dielectricAttenuationNpPerM = 0.0;
};

struct PatchResult
{
    bool valid = false;
    double frequencyHz = 0.0;
    double widthM = 0.0;
    double physicalLengthM = 0.0;
    double effectiveLengthM = 0.0;
    double fringingExtensionM = 0.0;
    double effectivePermittivity = 1.0;
};

struct InsetResult
{
    bool valid = false;
    double insetDepthM = 0.0;
    double edgeResistanceOhm = 0.0;
    double targetResistanceOhm = 0.0;
};

// Hammerstad/Jensen-style quasi-TEM engineering approximations for a zero-thickness
// microstrip conductor. Intended for educational design starts, not EM sign-off.
double effectivePermittivity(double widthM, double substrateHeightM, double relativePermittivity);
double characteristicImpedanceOhm(double widthM, double substrateHeightM, double relativePermittivity);
double widthForImpedance(double targetOhm, double substrateHeightM, double relativePermittivity);
LineResult analyzeLine(double frequencyHz, double widthM, double substrateHeightM,
                       double relativePermittivity, double lossTangent = 0.0,
                       double physicalLengthM = 0.0);
PatchResult rectangularPatch(double frequencyHz, double substrateHeightM, double relativePermittivity);
InsetResult insetForResistance(double patchLengthM, double edgeResistanceOhm, double targetResistanceOhm);

// Lossy/unlossy transmission-line transform. gamma is alpha+j*beta.
std::complex<double> transformImpedance(std::complex<double> loadOhm,
                                        std::complex<double> z0Ohm,
                                        std::complex<double> gammaPerM,
                                        double lengthM);

} // namespace MicrostripModels
