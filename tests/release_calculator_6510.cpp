#include "em_calculator_model.h"
#include "em_units_calculator_model.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace EmCalculator;
namespace
{
void require(bool ok, const std::string &name)
{
    if (!ok) { std::cerr << "FAIL: " << name << '\n'; std::exit(EXIT_FAILURE); }
}
bool near(double a, double b, double rel = 2e-11)
{
    return std::isfinite(a) && std::isfinite(b) &&
           std::fabs(a-b) <= rel*std::max({std::fabs(a),std::fabs(b),1e-99});
}
void check(const std::string &expression, double expected, const std::string &unit)
{
    const auto r = evaluateQuantity(expression);
    if (!r.ok || !near(r.quantity.siValue,expected) || quantityUnit(r.quantity) != unit)
    {
        std::cerr << "FAIL: " << expression << " => " << r.error << " "
                  << r.quantity.siValue << ' ' << quantityUnit(r.quantity)
                  << "; expected " << expected << ' ' << unit << '\n';
        std::exit(EXIT_FAILURE);
    }
}
}
int main()
{
    check("2 m^2", 2.0, "m^2");
    check("9 m²", 9.0, "m^2");
    check("9 m³", 9.0, "m^3");
    check("3 cm^2", 3e-4, "m^2");
    check("5 mm^3", 5e-9, "m^3");
    check("sqrt(9 m²)", 3.0, "m");
    check("(2 m)^2", 4.0, "m^2");
    check("(10 cm)^2", 0.01, "m^2");
    check("2 * cm", 0.02, "m");
    check("2*cm^2", 2e-4, "m^2");
    check("3 m/s^2", 3.0, "m/s^2");
    check("2 m^2 + 3 cm^2", 2.0003, "m^2");
    check("1 m^-2", 1.0, "m^-2");
    check("(2 cm)^2", 4e-4, "m^2");
    check("1/(2 cm)^2", 2500.0, "m^-2");
    check("-2^2", -4.0, "1");
    check("2^3^2", 512.0, "1");
    require(!evaluateQuantity("2 m^2 + 3 s^2").ok, "incompatible squared unit addition");
    require(!evaluateQuantity("3 m^s").ok, "invalid unit exponent");
    require(!evaluateQuantity("3 m^(1 s)").ok, "dimensional exponent rejected");
    require(!evaluateQuantity("sqrt(-9 m^2)").ok, "negative square root rejected");

    for (const auto &name : {"m", "F", "V", "Hz", "cm", "mu0", "PI", "ans"})
    {
        const auto r = evaluateWorksheet(std::string(name)+" = 2\n");
        require(!r.ok && r.errorLine == 1, "reserved name: " + std::string(name));
    }
    const auto good = evaluateWorksheet("q = 2 nC\nr = 10 cm\nE = ke*q/r^2\nforce = q*E\n");
    require(good.ok && good.variables.size()==4, "worked electrostatic worksheet: " + good.error);
    require(near(good.variables.at("E").siValue, coulombK*2e-9/0.01), "electric field sample");
    require(near(good.variables.at("force").siValue, coulombK*4e-18/0.01), "electrostatic force sample");
    require(!evaluateWorksheet("m = 2\nx = 3*m\n").ok, "ambiguous unit variable rejected");
    require(!evaluateWorksheet("F = 2 N\n").ok, "farad reserved from assignment");
    require(evaluateQuantity("5 * F",good.variables).ok, "explicit SI farad multiplicand remains supported");

    AntennaInputs antenna;
    AntennaResult result;
    std::string error;
    antenna.primaryDimensionM=0.5;
    require(antennaFromPrimaryDimension(AntennaKind::ExactHalfWaveDipole,antenna,result,error),
            "valid inverse antenna sizing");
    require(near(result.frequencyHz, c0*0.25/0.5), "inverse frequency exact");
    antenna.primaryDimensionM=0.0;
    require(!antennaFromPrimaryDimension(AntennaKind::ExactHalfWaveDipole,antenna,result,error),
            "invalid inverse input returns failure");
    // Caller contract: after failure, GUI must not commit result or create
    // geometry, even if the result object held a previous valid frequency.
    require(!error.empty(), "inverse error available for UI");

    std::cout << "QTsignalApp 6.5.1 parser/reserved-variable/inverse contracts: PASS\n";
}
