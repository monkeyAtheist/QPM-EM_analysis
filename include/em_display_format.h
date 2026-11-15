#pragma once

#include <cmath>
#include <string>

// UI-only engineering notation. Numerical values and their SI dimensions remain
// unchanged in EmCalculator::Quantity. Never apply prefixes to powers of a unit
// (m^2), dimensionless results, kilograms or a generic composite basis string.
namespace EmDisplay
{
struct ScaledResult
{
    double value = 0.0;
    std::string unit;
    double factor = 1.0;
};

inline ScaledResult scaled(double siValue, const std::string &unit, bool autoPrefix = true)
{
    ScaledResult result{siValue, unit, 1.0};
    if (!autoPrefix || !std::isfinite(siValue) || siValue == 0.0) return result;
    const bool prefixable = unit == "m" || unit == "s" || unit == "A" || unit == "C"
        || unit == "Hz" || unit == "V" || unit == "F" || unit == "H" || unit == "Ohm"
        || unit == "N" || unit == "J" || unit == "W" || unit == "T" || unit == "S"
        || unit == "V/m" || unit == "F/m" || unit == "H/m" || unit == "m/s"
        || unit == "m/s^2" || unit == "W/m^2";
    if (!prefixable) return result;
    struct Prefix { double factor; const char *symbol; };
    static constexpr Prefix prefixes[] = {
        {1e12,"T"},{1e9,"G"},{1e6,"M"},{1e3,"k"},{1.0,""},
        {1e-3,"m"},{1e-6,"u"},{1e-9,"n"},{1e-12,"p"},{1e-15,"f"}
    };
    const double magnitude = std::fabs(siValue);
    for (const auto &prefix : prefixes)
    {
        const double candidate = magnitude / prefix.factor;
        if (candidate >= 1.0 && candidate < 1000.0)
        {
            result.value = siValue / prefix.factor;
            result.factor = prefix.factor;
            result.unit = std::string(prefix.symbol) + unit;
            return result;
        }
    }
    return result;
}
} // namespace EmDisplay
