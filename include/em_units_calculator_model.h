#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

namespace EmCalculator
{
// Base dimensions in the order: kg, m, s, A, K, mol, cd.
// Exponents may be fractional after sqrt or an arbitrary dimensionless power.
struct Quantity
{
    double siValue = 0.0;
    std::array<double, 7> dimensions{};
};

using VariableTable = std::map<std::string, Quantity>;

struct QuantityResult
{
    Quantity quantity;
    bool ok = false;
    std::string error;
    std::size_t errorPosition = 0;
};

struct WorksheetRow
{
    int line = 0;
    std::string name;
    std::string expression;
    Quantity quantity;
};

struct WorksheetResult
{
    bool ok = false;
    int errorLine = 0;
    std::size_t errorPosition = 0;
    std::string error;
    VariableTable variables;
    std::vector<WorksheetRow> rows;
};

// Units are interpreted as multiplication by the appropriate SI conversion factor.
// Variable names are case-sensitive (E differs from the mathematical constant e).
// Allowed examples: 2 nC, 10 cm, 144 MHz, 3 m/s, 9 m^2, (10 cm)^2.
// Exponentiation binds to unit suffixes before multiplying by their coefficient:
// 9 m^2 = 9*(m^2), while (9 m)^2 = 81*m^2. Explicit `2 * cm` is supported.
// Variable names are not allowed to collide with reserved SI unit symbols.
QuantityResult evaluateQuantity(const std::string &expression,
                                const VariableTable &variables = {});
// All lines evaluate in sequence; a failed line never yields a successful worksheet.
WorksheetResult evaluateWorksheet(const std::string &multilineText);
// Unit name for the resulting SI quantity. Never includes a magnitude prefix.
std::string quantityUnit(const Quantity &quantity);
// Lists selectable input unit symbols, for UI help and tests.
std::vector<std::string> supportedUnitSymbols();
} // namespace EmCalculator
