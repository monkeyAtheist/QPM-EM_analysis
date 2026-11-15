#include "em_calculator_model.h"
#include "em_units_calculator_model.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

using namespace EmCalculator;
namespace
{
void require(bool condition,const std::string &description)
{
    if (!condition) {std::cerr << "FAIL " << description << '\n'; std::exit(EXIT_FAILURE);}
}
bool near(double actual,double expected,double rel=5e-12)
{
    return std::isfinite(actual) && std::isfinite(expected) &&
           std::fabs(actual-expected)<= rel*std::max({std::fabs(actual),std::fabs(expected),1e-99});
}
void check(const std::string &text,double expected,const std::string &unit)
{
    const auto r=evaluateQuantity(text);
    if (!r.ok || !near(r.quantity.siValue,expected) || quantityUnit(r.quantity)!=unit)
    {
        std::cerr << "FAIL: " << text << " => " << r.error << " / " << r.quantity.siValue
                  << " [" << quantityUnit(r.quantity) << "] expected " << expected << " [" << unit << "]\n";
        std::exit(EXIT_FAILURE);
    }
}
}
int main()
{
    const auto workbook=evaluateWorksheet(
        "# electrostatics\nq = 2 nC\nr = 10 cm\nE = ke*q/r^2\nforce = q*E\n");
    require(workbook.ok,"worksheet valid: "+workbook.error);
    require(workbook.rows.size()==4 && workbook.variables.size()==4,"worksheet rows and symbols");
    require(near(workbook.variables.at("q").siValue,2e-9),"q SI");
    require(near(workbook.variables.at("r").siValue,0.1),"r SI");
    const double expectedE=coulombK*2e-9/(0.1*0.1);
    require(near(workbook.variables.at("E").siValue,expectedE),"electric field");
    require(quantityUnit(workbook.variables.at("E"))=="V/m","electric field unit");
    require(near(workbook.variables.at("force").siValue,expectedE*2e-9),"force value");
    require(quantityUnit(workbook.variables.at("force"))=="N","force unit");
    require(near(evaluateQuantity("E*2",workbook.variables).quantity.siValue,2*expectedE),"variables reused");
    check("2nC",2e-9,"C");
    check("10 cm",0.1,"m");
    check("1 mm^2",1e-6,"m^2");
    check("(10 cm)^2",0.01,"m^2");
    check("144 MHz",144e6,"Hz");
    check("3 m/s",3,"m/s");
    check("1 μF",1e-6,"F");
    check("2 Ω",2,"Ohm");
    check("pi",pi,"1");
    check("eps0",epsilon0,"F/m");
    check("mu0",mu0,"H/m");
    check("sqrt(mu0/eps0)",std::sqrt(mu0/epsilon0),"Ohm");
    check("(2 V)+(500 mV)",2.5,"V");
    check("1/(2*pi*sqrt(10 uH*100 nF))",1/(2*pi*std::sqrt(10e-6*100e-9)),"Hz");
    check("sind(30)",0.5,"1");
    check("exp(1)",euler,"1");
    check("-2^2",-4,"1");
    check("2^3^2",512,"1");
    check("sin(90 deg)",1,"1");
    const auto invalid1=evaluateQuantity("1 m + 1 s");
    require(!invalid1.ok && invalid1.error.find("Incompatible units")!=std::string::npos,"unit mismatch rejected");
    require(!evaluateQuantity("exp(2 m)").ok,"exp dimensional arg rejected");
    require(!evaluateQuantity("1/(0 s)").ok,"div zero rejected");
    require(!evaluateQuantity("sqrt(-1 m)").ok,"domain check");
    require(!evaluateQuantity("nan").ok,"nan rejected");
    require(!evaluateQuantity("unknown").ok,"unknown var rejected");
    require(!evaluateQuantity("ans").ok,"undefined previous result rejected");
    require(!evaluateQuantity("1e309").ok,"overflow rejected");
    require(!evaluateQuantity("1 m^s").ok,"dimensioned exponent rejected");
    const auto invalid2=evaluateWorksheet("q=1 nC\nE=q + 1 m\n");
    require(!invalid2.ok && invalid2.errorLine==2,"worksheet error line and no partial success");
    const auto invalid3=evaluateWorksheet("q=2 nC\nq=3 nC");
    require(!invalid3.ok && invalid3.errorLine==2,"duplicate variable rejected");
    require(!evaluateWorksheet("eps0=10").ok,"constant cannot be overwritten");
    require(evaluateWorksheet("E=3 V/m").ok,"uppercase variable E accepted");
    require(!evaluateWorksheet("q=2 nC\nr=10 cm\nq*r").rows.empty(),"standalone calculation row");
    require(!supportedUnitSymbols().empty(),"unit catalog");
    require(evaluateExpression("ke*2e-9/(0.1^2)").ok,"legacy parser untouched");
    std::cout << "Worksheet q=" << workbook.variables.at("q").siValue
              << " C, r=" << workbook.variables.at("r").siValue
              << " m, E=" << expectedE << " V/m, F=" << expectedE*2e-9 << " N\n";
    std::cout << "QTsignalApp 6.5.0 dimensional worksheet regression: PASS\n";
}
