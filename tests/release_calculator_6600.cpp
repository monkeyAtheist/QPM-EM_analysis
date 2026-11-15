#include "em_display_format.h"
#include "em_units_calculator_model.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

static void expect(bool ok, const char *test)
{
    if (!ok) throw std::runtime_error(test);
}
static bool close(double a, double b, double tol = 1e-10)
{
    return std::abs(a-b) <= tol * std::max(1.0, std::abs(b));
}
int main()
{
    const auto calculation = EmCalculator::evaluateQuantity("ke*(2 nC)/(10 cm)^2");
    expect(calculation.ok, "electric field formula");
    expect(EmCalculator::quantityUnit(calculation.quantity)=="V/m", "physical dimensions");
    expect(close(calculation.quantity.siValue, 1797.51, 1e-5), "exact SI value");
    const auto prefixed = EmDisplay::scaled(calculation.quantity.siValue, "V/m");
    expect(prefixed.unit=="kV/m", "engineering prefix in V/m");
    expect(close(prefixed.value, calculation.quantity.siValue/1000.0), "display scaling");
    expect(close(prefixed.value*prefixed.factor, calculation.quantity.siValue), "lossless presentation contract");
    expect(EmDisplay::scaled(8.854e-12, "F/m").unit == "pF/m", "epsilon prefix");
    expect(EmDisplay::scaled(144e6,"Hz").unit == "MHz", "frequency prefix");
    expect(EmDisplay::scaled(-0.00035,"A").unit == "uA", "negative prefix");
    expect(EmDisplay::scaled(9.0,"m^2").unit == "m^2", "do not rescale square metres");
    expect(EmDisplay::scaled(8.0,"kg").unit == "kg", "do not prefix kilogram");
    expect(EmDisplay::scaled(1000.0,"1").unit == "1", "dimensionless exact");
    expect(EmDisplay::scaled(0.0,"V").unit == "V", "zero stable");
    expect(EmDisplay::scaled(1797.5,"V/m",false).unit == "V/m", "manual SI display");
    expect(EmDisplay::scaled(1e20,"V").factor == 1.0, "outside prefixes falls back to SI");
    auto square = EmCalculator::evaluateQuantity("9 m^2");
    auto parens = EmCalculator::evaluateQuantity("(9 m)^2");
    expect(square.ok && parens.ok && square.quantity.siValue == 9.0 && parens.quantity.siValue == 81.0,
           "6.5.1 square-unit regression");
    std::cout << "QTsignalApp 6.6.0 engineering display/model contracts: PASS\n";
}
