#include "em_calculator_model.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <set>
#include <string>

namespace
{
void require(bool ok, const char *message)
{
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
bool close(double x, double y, double rel = 2e-10)
{
    return std::isfinite(x) && std::isfinite(y) &&
           std::fabs(x-y) <= rel * std::max({std::fabs(x), std::fabs(y), 1.e-99});
}
void expressionNear(const std::string &text, double expected)
{
    const auto result = EmCalculator::evaluateExpression(text);
    if (!result.ok || !close(result.value, expected))
    {
        std::cerr << "FAIL: " << text << " => " << result.error << " / " << result.value
                  << ", expected " << expected << '\n';
        std::exit(1);
    }
}
}

int main()
{
    using namespace EmCalculator;
    const auto &list = physicalConstants();
    require(list.size() >= 18, "expanded catalogue");
    std::set<std::string> names;
    for (const auto &constant : list)
    {
        require(!constant.symbol.empty() && !constant.name.empty() && !constant.category.empty(), "metadata filled");
        require(std::isfinite(constant.value) && constant.value > 0.0, "constant positive finite");
        require(names.insert(constant.symbol).second, "unique canonical symbols");
        expressionNear(constant.symbol, constant.value);
    }
    // NIST CODATA 2022, with rounding at the precision of listed reference values.
    require(close(epsilon0, 8.8541878188e-12, 1e-13), "CODATA 2022 epsilon0");
    require(close(mu0, 1.25663706127e-6, 1e-13), "CODATA 2022 mu0");
    require(close(c0, 299792458.0, 1e-15), "c exact SI");
    require(close(coulombK, 1.0/(4*pi*epsilon0), 1e-14), "Coulomb constant relation");
    require(close(eta0, mu0*c0, 1e-14), "free-space impedance relation");
    require(close(boltzmannKB, 1.380649e-23), "Boltzmann exact SI");
    require(close(avogadroNA, 6.02214076e23), "Avogadro exact SI");
    require(close(molarGasR, avogadroNA*boltzmannKB), "molar gas constant relation");
    require(close(faradayF, avogadroNA*elementaryCharge), "Faraday constant relation");
    expressionNear("epsilon0", epsilon0);
    expressionNear("epsilon_0", epsilon0);
    expressionNear("eps_0", epsilon0);
    expressionNear("mu_0", mu0);
    expressionNear("c", c0);
    expressionNear("k", coulombK);
    expressionNear("kb", boltzmannKB);
    expressionNear("k_b", boltzmannKB);
    expressionNear("na", avogadroNA);
    expressionNear("sqrt(mu0/eps0)", std::sqrt(mu0/epsilon0));
    expressionNear("1/(4*pi*eps0)", coulombK);
    expressionNear("exp(1)", euler);
    expressionNear("e^(-2)", std::exp(-2.0));
    expressionNear("eps0*4.2*0.01/1e-3", epsilon0*42.0);
    expressionNear("4*pi*eps0*0.1", 4*pi*epsilon0*0.1);
    expressionNear("0.5*eps0*(1000^2)", 0.5*epsilon0*1e6);
    expressionNear("(0.01^2)/(2*mu0)", 0.0001/(2*mu0));
    expressionNear("-2^2", -4.0);
    expressionNear("sind(30)+cosd(60)", 1.0);
    const auto notYet = evaluateExpression("ans+1");
    require(!notYet.ok && notYet.error.find("not defined") != std::string::npos, "ans unavailable before first success");
    auto ans = evaluateExpression("2*ans+1", 3.0);
    require(ans.ok && close(ans.value, 7.0), "ans chains previous result");
    ans = evaluateExpression("ans^2", 7.0);
    require(ans.ok && close(ans.value, 49.0), "ans power");
    ans = evaluateExpression("ans", std::numeric_limits<double>::quiet_NaN());
    require(!ans.ok, "nan previous result rejected");
    require(!evaluateExpression("sqrt(-1)").ok, "non-finite domain error");
    require(!evaluateExpression("1/0").ok, "divide by zero error");
    require(!evaluateExpression("unknown_constant").ok, "unknown symbol error");

    WaveResult wave; std::string error;
    require(waveFromFrequency(144e6, 1.0, 1.0, wave, error), "forward wave");
    require(close(wave.wavelengthM, 2.0818920694444443, 1e-10), "lambda 144 MHz");
    WaveResult back;
    require(waveFromWavelength(wave.wavelengthM, 1.0, 1.0, back, error), "inverse wave");
    require(close(back.frequencyHz, 144e6, 1e-12), "inverse f 144 MHz");
    AntennaInputs antenna; antenna.frequencyHz=144e6;
    AntennaResult sizing;
    require(antennaFromFrequency(AntennaKind::ResonantDipole0475, antenna, sizing, error), "antenna sizing unchanged");
    require(close(sizing.primaryDimensionM, 0.2375*c0/144e6), "dipole 144 MHz");
    std::cout << "CODATA eps0=" << epsilon0 << " mu0=" << mu0 << " eta0=" << eta0 << '\n';
    std::cout << "Constants: " << list.size() << " tested, parser/aliases/ans/invalid: PASS\n";
    std::cout << "QTsignalApp 6.4.0 calculator usability and constants regression: PASS\n";
}
