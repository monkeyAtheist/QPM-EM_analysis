#include "em_calculator_model.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
bool near(double a, double b, double rel = 1.0e-9)
{
    const double scale = std::max({1.0, std::fabs(a), std::fabs(b)});
    return std::fabs(a - b) <= rel * scale;
}

[[noreturn]] void fail(const std::string &what)
{
    std::cerr << "FAIL: " << what << '\n';
    std::exit(1);
}
}

int main()
{
    using namespace EmCalculator;
    std::string error;

    WaveResult wave;
    if (!waveFromFrequency(144.0e6, 1.0, 1.0, wave, error)) fail(error);
    if (!near(wave.wavelengthM, c0 / 144.0e6, 1.0e-12)) fail("144 MHz wavelength");
    WaveResult inverse;
    if (!waveFromWavelength(wave.wavelengthM, 1.0, 1.0, inverse, error)) fail(error);
    if (!near(inverse.frequencyHz, 144.0e6, 1.0e-12)) fail("wavelength inverse");

    AntennaInputs in;
    in.frequencyHz = 144.0e6;
    AntennaResult antenna;
    if (!antennaFromFrequency(AntennaKind::ResonantDipole0475, in, antenna, error)) fail(error);
    const double expectedArm = 0.2375 * c0 / 144.0e6;
    if (!near(antenna.primaryDimensionM, expectedArm, 1.0e-12)) fail("0.475 lambda dipole arm");

    AntennaInputs inv = in;
    inv.primaryDimensionM = antenna.primaryDimensionM;
    AntennaResult antennaInverse;
    if (!antennaFromPrimaryDimension(AntennaKind::ResonantDipole0475, inv, antennaInverse, error)) fail(error);
    if (!near(antennaInverse.frequencyHz, 144.0e6, 1.0e-10)) fail("dipole arm inverse frequency");

    if (!antennaFromFrequency(AntennaKind::Jpole, in, antenna, error)) fail(error);
    if (!near(antenna.primaryDimensionM, 0.75 * c0 / 144.0e6, 1.0e-12)) fail("J-pole long leg");

    AntennaInputs patch;
    patch.frequencyHz = 2.45e9;
    patch.relativePermittivity = 4.4;
    patch.substrateHeightM = 1.6e-3;
    AntennaResult patchResult;
    if (!antennaFromFrequency(AntennaKind::RectangularPatch, patch, patchResult, error)) fail(error);
    AntennaInputs patchInv = patch;
    patchInv.primaryDimensionM = patchResult.primaryDimensionM;
    AntennaResult patchInverse;
    if (!antennaFromPrimaryDimension(AntennaKind::RectangularPatch, patchInv, patchInverse, error)) fail(error);
    if (!near(patchInverse.frequencyHz, patch.frequencyHz, 2.0e-8)) fail("patch L inverse frequency");

    AntennaInputs dish;
    dish.frequencyHz = 10.0e9;
    dish.dishDiameterM = 1.0;
    dish.dishEfficiency = 0.60;
    dish.focalRatio = 0.40;
    dish.desiredGainDbi = 30.0;
    AntennaResult dishResult;
    if (!antennaFromFrequency(AntennaKind::ParabolicDish, dish, dishResult, error)) fail(error);
    if (!(dishResult.items.size() >= 5 && dishResult.items[2].value > 30.0)) fail("dish gain estimate");
    AntennaInputs dishInverse = dish;
    dishInverse.primaryDimensionM = 1.0;
    if (antennaFromPrimaryDimension(AntennaKind::ParabolicDish, dishInverse, dishResult, error)) fail("dish diameter must not imply unique frequency");

    auto expr = evaluateExpression("ke*(1e-9)*(1e-9)/(0.1^2)");
    if (!expr.ok || !near(expr.value, coulombK * 1e-16, 1.0e-12)) fail("Coulomb expression");
    expr = evaluateExpression("sqrt(mu0/eps0)");
    if (!expr.ok || !near(expr.value, std::sqrt(mu0 / epsilon0), 1.0e-12)) fail("wave impedance expression");
    expr = evaluateExpression("-2^2");
    if (!expr.ok || !near(expr.value, -4.0, 1.0e-12)) fail("operator precedence");
    expr = evaluateExpression("2^-2");
    if (!expr.ok || !near(expr.value, 0.25, 1.0e-12)) fail("negative exponent");
    expr = evaluateExpression("sind(30)+cosd(60)");
    if (!expr.ok || !near(expr.value, 1.0, 1.0e-12)) fail("degree trig functions");
    expr = evaluateExpression("exp(1)");
    if (!expr.ok || !near(expr.value, euler, 1.0e-12)) fail("exp(1)");
    expr = evaluateExpression("e^(1)");
    if (!expr.ok || !near(expr.value, euler, 1.0e-12)) fail("e^(1)");
    expr = evaluateExpression("exp(-2.5)");
    if (!expr.ok || !near(expr.value, std::exp(-2.5), 1.0e-12)) fail("negative exponential");

    std::cout << "144 MHz lambda = " << wave.wavelengthM << " m\n";
    std::cout << "0.475 lambda dipole arm = " << expectedArm << " m\n";
    std::cout << "2.45 GHz FR4 patch L = " << patchResult.primaryDimensionM << " m\n";
    std::cout << "QTsignalApp 6.2.0 calculator numerical regression: PASS\n";
    return 0;
}
