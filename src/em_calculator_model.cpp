#include "em_calculator_model.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace EmCalculator
{
namespace
{
bool positiveFinite(double x) { return std::isfinite(x) && x > 0.0; }

bool propagationSpeed(double er, double mur, double &speed, std::string &error)
{
    if (!positiveFinite(er) || !positiveFinite(mur))
    {
        error = "Relative permittivity and permeability must be finite and > 0.";
        return false;
    }
    speed = c0 / std::sqrt(er * mur);
    if (!positiveFinite(speed))
    {
        error = "Invalid propagation speed.";
        return false;
    }
    return true;
}

void addLength(std::vector<ResultItem> &items, const char *label, double metres, const char *note = "")
{
    items.push_back({label, metres, "m", note});
}

void addScalar(std::vector<ResultItem> &items, const char *label, double value, const char *unit, const char *note = "")
{
    items.push_back({label, value, unit, note});
}

double vf(const AntennaInputs &in)
{
    return std::clamp(in.velocityFactor, 1.0e-6, 1.0);
}

bool patchDimensions(double frequencyHz, double er, double h, double &width, double &length,
                     double &effectivePermittivity, double &deltaLength, std::string &error)
{
    if (!positiveFinite(frequencyHz) || !positiveFinite(er) || er <= 1.0 || !positiveFinite(h))
    {
        error = "Patch model requires f > 0, epsilon_r > 1 and substrate height > 0.";
        return false;
    }
    width = c0 / (2.0 * frequencyHz) * std::sqrt(2.0 / (er + 1.0));
    if (!positiveFinite(width) || width / h <= 0.0)
    {
        error = "Invalid patch width/substrate ratio.";
        return false;
    }
    effectivePermittivity = (er + 1.0) / 2.0 + (er - 1.0) / 2.0 / std::sqrt(1.0 + 12.0 * h / width);
    const double wh = width / h;
    deltaLength = 0.412 * h * ((effectivePermittivity + 0.3) * (wh + 0.264)) /
                  ((effectivePermittivity - 0.258) * (wh + 0.8));
    const double effectiveLength = c0 / (2.0 * frequencyHz * std::sqrt(effectivePermittivity));
    length = effectiveLength - 2.0 * deltaLength;
    if (!positiveFinite(length) || !std::isfinite(effectivePermittivity) || !std::isfinite(deltaLength))
    {
        error = "Patch closed-form model produced a non-physical dimension.";
        return false;
    }
    return true;
}

bool inversePatchFrequency(double targetLength, double er, double h, double &frequencyHz, std::string &error)
{
    if (!positiveFinite(targetLength) || !positiveFinite(er) || er <= 1.0 || !positiveFinite(h))
    {
        error = "Patch inverse model requires L > 0, epsilon_r > 1 and h > 0.";
        return false;
    }
    double lo = 1.0e3;
    double hi = 1.0e12;
    double w = 0.0, l = 0.0, ee = 0.0, dl = 0.0;
    for (int i = 0; i < 180; ++i)
    {
        const double mid = std::sqrt(lo * hi);
        if (!patchDimensions(mid, er, h, w, l, ee, dl, error))
            return false;
        if (l > targetLength)
            lo = mid;
        else
            hi = mid;
    }
    frequencyHz = std::sqrt(lo * hi);
    return positiveFinite(frequencyHz);
}

class Parser
{
public:
    explicit Parser(const std::string &text) : m_text(text) {}

    ExpressionResult run()
    {
        ExpressionResult result;
        try
        {
            skip();
            if (m_pos >= m_text.size())
                fail("Expression is empty.");
            result.value = expression();
            skip();
            if (m_pos != m_text.size())
                fail("Unexpected token.");
            if (!std::isfinite(result.value))
                fail("Result is NaN or Inf.");
            result.ok = true;
        }
        catch (const std::runtime_error &e)
        {
            result.ok = false;
            result.error = e.what();
            result.errorPosition = m_errorPos;
        }
        return result;
    }

private:
    double expression()
    {
        double value = term();
        for (;;)
        {
            skip();
            if (accept('+')) value += term();
            else if (accept('-')) value -= term();
            else break;
        }
        return value;
    }

    double term()
    {
        double value = unary();
        for (;;)
        {
            skip();
            if (accept('*')) value *= unary();
            else if (accept('/'))
            {
                const double d = unary();
                if (d == 0.0) fail("Division by zero.");
                value /= d;
            }
            else break;
        }
        return value;
    }

    double unary()
    {
        skip();
        if (accept('+')) return unary();
        if (accept('-')) return -unary();
        return power();
    }

    double power()
    {
        double left = primary();
        skip();
        if (accept('^'))
            left = std::pow(left, unary());
        return left;
    }

    double primary()
    {
        skip();
        if (accept('('))
        {
            const double v = expression();
            skip();
            if (!accept(')')) fail("Missing ')'.");
            return v;
        }

        if (m_pos < m_text.size() && (std::isdigit(static_cast<unsigned char>(m_text[m_pos])) || m_text[m_pos] == '.'))
            return number();

        if (m_pos < m_text.size() && (std::isalpha(static_cast<unsigned char>(m_text[m_pos])) || m_text[m_pos] == '_'))
        {
            const std::string id = identifier();
            skip();
            if (accept('('))
                return function(id);
            return constant(id);
        }

        fail("Expected a number, constant, function or '('.");
        return 0.0;
    }

    double number()
    {
        const char *start = m_text.c_str() + m_pos;
        char *end = nullptr;
        errno = 0;
        const double v = std::strtod(start, &end);
        if (end == start || errno == ERANGE)
            fail("Invalid numeric literal.");
        m_pos += static_cast<std::size_t>(end - start);
        return v;
    }

    std::string identifier()
    {
        const std::size_t start = m_pos;
        while (m_pos < m_text.size())
        {
            const unsigned char c = static_cast<unsigned char>(m_text[m_pos]);
            if (!std::isalnum(c) && c != '_') break;
            ++m_pos;
        }
        std::string id = m_text.substr(start, m_pos - start);
        std::transform(id.begin(), id.end(), id.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return id;
    }

    double constant(const std::string &id)
    {
        static const std::unordered_map<std::string, double> constants = {
            {"pi", pi}, {"e", euler}, {"eps0", epsilon0}, {"epsilon0", epsilon0},
            {"mu0", mu0}, {"c", c0}, {"c0", c0}, {"ke", coulombK}, {"k", coulombK},
            {"eta0", eta0}, {"z0", eta0}, {"h", planckH}, {"hbar", hbar},
            {"qe", elementaryCharge}, {"echarge", elementaryCharge}, {"me", electronMass}
        };
        const auto it = constants.find(id);
        if (it == constants.end()) fail("Unknown constant: " + id);
        return it->second;
    }

    double function(const std::string &id)
    {
        const double a = expression();
        skip();
        bool hasSecond = false;
        double b = 0.0;
        if (accept(','))
        {
            hasSecond = true;
            b = expression();
        }
        skip();
        if (!accept(')')) fail("Missing ')' after function arguments.");

        if (id == "sin") return std::sin(a);
        if (id == "cos") return std::cos(a);
        if (id == "tan") return std::tan(a);
        if (id == "asin") return std::asin(a);
        if (id == "acos") return std::acos(a);
        if (id == "atan") return std::atan(a);
        if (id == "sind") return std::sin(a * pi / 180.0);
        if (id == "cosd") return std::cos(a * pi / 180.0);
        if (id == "tand") return std::tan(a * pi / 180.0);
        if (id == "sqrt") return std::sqrt(a);
        if (id == "abs") return std::fabs(a);
        if (id == "exp") return std::exp(a);
        if (id == "ln" || id == "log") return std::log(a);
        if (id == "log10") return std::log10(a);
        if (id == "floor") return std::floor(a);
        if (id == "ceil") return std::ceil(a);
        if (id == "deg2rad") return a * pi / 180.0;
        if (id == "rad2deg") return a * 180.0 / pi;
        if (id == "pow" && hasSecond) return std::pow(a, b);
        if (id == "min" && hasSecond) return std::min(a, b);
        if (id == "max" && hasSecond) return std::max(a, b);
        fail(hasSecond ? "Unknown two-argument function: " + id : "Unknown function: " + id);
        return 0.0;
    }

    bool accept(char c)
    {
        skip();
        if (m_pos < m_text.size() && m_text[m_pos] == c)
        {
            ++m_pos;
            return true;
        }
        return false;
    }

    void skip()
    {
        while (m_pos < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[m_pos]))) ++m_pos;
    }

    [[noreturn]] void fail(const std::string &message)
    {
        m_errorPos = m_pos;
        throw std::runtime_error(message);
    }

    const std::string &m_text;
    std::size_t m_pos = 0;
    std::size_t m_errorPos = 0;
};

} // namespace

bool waveFromFrequency(double frequencyHz, double er, double mur, WaveResult &result, std::string &error)
{
    double speed = 0.0;
    if (!positiveFinite(frequencyHz))
    {
        error = "Frequency must be finite and > 0.";
        return false;
    }
    if (!propagationSpeed(er, mur, speed, error)) return false;
    result.frequencyHz = frequencyHz;
    result.phaseVelocityMS = speed;
    result.wavelengthM = speed / frequencyHz;
    result.periodS = 1.0 / frequencyHz;
    result.angularFrequencyRadS = 2.0 * pi * frequencyHz;
    result.waveNumberRadM = 2.0 * pi / result.wavelengthM;
    return true;
}

bool waveFromWavelength(double wavelengthM, double er, double mur, WaveResult &result, std::string &error)
{
    double speed = 0.0;
    if (!positiveFinite(wavelengthM))
    {
        error = "Wavelength must be finite and > 0.";
        return false;
    }
    if (!propagationSpeed(er, mur, speed, error)) return false;
    return waveFromFrequency(speed / wavelengthM, er, mur, result, error);
}

const char *antennaName(AntennaKind kind)
{
    switch (kind)
    {
    case AntennaKind::ExactHalfWaveDipole: return "Dipole - exact geometric lambda/2";
    case AntennaKind::ResonantDipole0475: return "Dipole - practical 0.475 lambda start";
    case AntennaKind::QuarterWaveMonopole: return "Quarter-wave monopole";
    case AntennaKind::GroundPlaneQuarterWave: return "1/4-wave ground-plane antenna";
    case AntennaKind::InvertedVee: return "Inverted-V dipole";
    case AntennaKind::FoldedDipole: return "Folded dipole";
    case AntennaKind::FiveEighthMonopole: return "5/8-wave monopole";
    case AntennaKind::FullWaveLoop: return "Full-wave loop";
    case AntennaKind::Jpole: return "J-pole";
    case AntennaKind::SlimJim: return "Slim Jim";
    case AntennaKind::AxialModeHelix: return "Axial-mode helical antenna";
    case AntennaKind::Yagi3Element: return "3-element Yagi starting geometry";
    case AntennaKind::RectangularPatch: return "Rectangular microstrip patch";
    case AntennaKind::ParabolicDish: return "Parabolic reflector";
    }
    return "Unknown";
}

const char *primaryDimensionName(AntennaKind kind)
{
    switch (kind)
    {
    case AntennaKind::ExactHalfWaveDipole:
    case AntennaKind::ResonantDipole0475:
    case AntennaKind::InvertedVee: return "One leg / arm length";
    case AntennaKind::QuarterWaveMonopole:
    case AntennaKind::GroundPlaneQuarterWave:
    case AntennaKind::FiveEighthMonopole: return "Radiating element length";
    case AntennaKind::FoldedDipole: return "Overall resonant span";
    case AntennaKind::FullWaveLoop: return "Loop circumference";
    case AntennaKind::Jpole: return "Long J element length";
    case AntennaKind::SlimJim: return "Overall vertical length";
    case AntennaKind::AxialModeHelix: return "One-turn circumference";
    case AntennaKind::Yagi3Element: return "Driven-element total length";
    case AntennaKind::RectangularPatch: return "Patch resonant length L";
    case AntennaKind::ParabolicDish: return "Dish diameter";
    }
    return "Primary dimension";
}

bool antennaFromFrequency(AntennaKind kind, const AntennaInputs &in, AntennaResult &result, std::string &error)
{
    if (!positiveFinite(in.frequencyHz))
    {
        error = "Frequency must be finite and > 0.";
        return false;
    }
    result = {};
    result.frequencyHz = in.frequencyHz;
    result.wavelengthM = c0 / in.frequencyHz;
    result.primaryDimensionLabel = primaryDimensionName(kind);
    const double lambdaWire = result.wavelengthM * vf(in);

    switch (kind)
    {
    case AntennaKind::ExactHalfWaveDipole:
        result.primaryDimensionM = 0.25 * lambdaWire;
        addLength(result.items, "One arm", result.primaryDimensionM);
        addLength(result.items, "Total tip-to-tip conductor", 0.50 * lambdaWire);
        result.assumption = "Ideal geometric half-wave start. End effects and conductor diameter shift the resonant frequency.";
        break;
    case AntennaKind::ResonantDipole0475:
        result.primaryDimensionM = 0.2375 * lambdaWire;
        addLength(result.items, "One arm", result.primaryDimensionM);
        addLength(result.items, "Total tip-to-tip conductor", 0.475 * lambdaWire);
        result.assumption = "0.475 lambda is a practical thin-wire resonant starting estimate, not an exact universal resonance.";
        break;
    case AntennaKind::QuarterWaveMonopole:
        result.primaryDimensionM = 0.25 * lambdaWire;
        addLength(result.items, "Radiator", result.primaryDimensionM);
        result.assumption = "Quarter-wave radiator over an electrically large ground reference. Matching and end effects are not included.";
        break;
    case AntennaKind::GroundPlaneQuarterWave:
        result.primaryDimensionM = 0.25 * lambdaWire;
        addLength(result.items, "Vertical radiator", result.primaryDimensionM);
        addLength(result.items, "Each radial (start)", 0.25 * lambdaWire, "Usually 3 or 4 radials; droop angle changes feed impedance.");
        result.assumption = "Quarter-wave ground-plane starting dimensions; radial angle and conductor diameter require tuning.";
        break;
    case AntennaKind::InvertedVee:
        result.primaryDimensionM = 0.2375 * lambdaWire;
        addLength(result.items, "Each sloping leg", result.primaryDimensionM);
        addLength(result.items, "Total wire", 0.475 * lambdaWire);
        result.assumption = "Inverted-V starting point based on a 0.475 lambda dipole. Apex angle/height change resonance and feed impedance; tune after geometry is fixed.";
        break;
    case AntennaKind::FoldedDipole:
        result.primaryDimensionM = 0.475 * lambdaWire;
        addLength(result.items, "Overall resonant span", result.primaryDimensionM);
        addLength(result.items, "Approx. conductor path (two parallel sides)", 2.0 * result.primaryDimensionM);
        result.assumption = "Equal-diameter two-wire folded dipole start; nominal feed resistance near 4x a simple dipole only under ideal symmetry.";
        break;
    case AntennaKind::FiveEighthMonopole:
        result.primaryDimensionM = 0.625 * lambdaWire;
        addLength(result.items, "Radiating element", result.primaryDimensionM);
        result.assumption = "5/8-wave physical radiator. A matching/loading network is normally required; this is not a direct 50-ohm resonance formula.";
        break;
    case AntennaKind::FullWaveLoop:
        result.primaryDimensionM = lambdaWire;
        addLength(result.items, "Loop circumference", result.primaryDimensionM);
        addLength(result.items, "Circular-loop diameter", result.primaryDimensionM / pi);
        addLength(result.items, "Square-loop side", result.primaryDimensionM / 4.0);
        result.assumption = "One-wavelength loop starting perimeter. Shape, conductor diameter and feed location shift impedance/resonance.";
        break;
    case AntennaKind::Jpole:
        result.primaryDimensionM = 0.75 * lambdaWire;
        addLength(result.items, "Long J element", result.primaryDimensionM);
        addLength(result.items, "Short matching-stub element", 0.25 * lambdaWire);
        addLength(result.items, "Radiating half-wave section", 0.50 * lambdaWire);
        result.assumption = "Canonical 3/4 lambda + 1/4 lambda J-pole geometry. Feed tap, spacing and end effects must be tuned for matching.";
        break;
    case AntennaKind::SlimJim:
        result.primaryDimensionM = 0.75 * lambdaWire;
        addLength(result.items, "Overall vertical section", result.primaryDimensionM);
        addLength(result.items, "Quarter-wave matching section", 0.25 * lambdaWire);
        addLength(result.items, "Half-wave radiator section", 0.50 * lambdaWire);
        result.assumption = "Slim-Jim starting geometry. Parallel-line spacing, feed tap and end effects determine the actual match and must be tuned.";
        break;
    case AntennaKind::AxialModeHelix:
    {
        result.primaryDimensionM = lambdaWire;
        const double spacing = 0.23 * lambdaWire;
        addLength(result.items, "One-turn circumference C", result.primaryDimensionM);
        addLength(result.items, "Helix diameter D=C/pi", result.primaryDimensionM / pi);
        addLength(result.items, "Turn spacing S (start)", spacing);
        addScalar(result.items, "Pitch angle atan(S/C)", std::atan(spacing / result.primaryDimensionM) * 180.0 / pi, "deg");
        addLength(result.items, "Wire length per turn", std::hypot(result.primaryDimensionM, spacing));
        result.assumption = "Axial-mode start with C approximately lambda and S approximately 0.23 lambda. Number of turns and ground plane determine gain, beamwidth and impedance.";
        break;
    }
    case AntennaKind::Yagi3Element:
        result.primaryDimensionM = 0.475 * lambdaWire;
        addLength(result.items, "Driven element", result.primaryDimensionM);
        addLength(result.items, "Reflector start", 0.50 * lambdaWire);
        addLength(result.items, "Director start", 0.45 * lambdaWire);
        addLength(result.items, "Reflector -> driven spacing", 0.20 * lambdaWire);
        addLength(result.items, "Driven -> director spacing", 0.15 * lambdaWire);
        result.assumption = "Coarse 3-element Yagi starting geometry only. Final element lengths/spacings require optimization or validated design data.";
        break;
    case AntennaKind::RectangularPatch:
    {
        double width = 0.0, length = 0.0, ee = 0.0, dl = 0.0;
        if (!patchDimensions(in.frequencyHz, in.relativePermittivity, in.substrateHeightM, width, length, ee, dl, error)) return false;
        result.primaryDimensionM = length;
        addLength(result.items, "Patch length L", length);
        addLength(result.items, "Patch width W", width);
        addLength(result.items, "Fringing extension DeltaL", dl);
        addScalar(result.items, "Effective epsilon_r", ee, "");
        result.assumption = "Hammerstad/cavity-style rectangular-patch starting estimate on a single non-magnetic substrate. Feed and finite-ground effects require EM validation.";
        break;
    }
    case AntennaKind::ParabolicDish:
    {
        if (!positiveFinite(in.dishDiameterM) || !positiveFinite(in.dishEfficiency) || in.dishEfficiency > 1.0 || !positiveFinite(in.focalRatio))
        {
            error = "Dish model requires diameter > 0, 0 < efficiency <= 1 and f/D > 0.";
            return false;
        }
        result.primaryDimensionM = in.dishDiameterM;
        const double ratio = pi * in.dishDiameterM / result.wavelengthM;
        const double gainLinear = in.dishEfficiency * ratio * ratio;
        const double gainDbi = 10.0 * std::log10(gainLinear);
        const double beamwidth = 70.0 * result.wavelengthM / in.dishDiameterM;
        const double focal = in.focalRatio * in.dishDiameterM;
        const double desiredLinear = std::pow(10.0, in.desiredGainDbi / 10.0);
        const double diameterForDesiredGain = result.wavelengthM / pi * std::sqrt(desiredLinear / in.dishEfficiency);
        addLength(result.items, "Dish diameter D", in.dishDiameterM);
        addLength(result.items, "Focal distance f", focal);
        addScalar(result.items, "Estimated gain", gainDbi, "dBi");
        addScalar(result.items, "Approx. HPBW", beamwidth, "deg");
        addLength(result.items, "Diameter for requested gain", diameterForDesiredGain);
        result.assumption = "Ideal-aperture engineering estimate G=eta(pi D/lambda)^2 and HPBW~70 lambda/D. Illumination, blockage, surface accuracy and feed losses are not modeled.";
        break;
    }
    }
    return true;
}

bool antennaFromPrimaryDimension(AntennaKind kind, const AntennaInputs &in, AntennaResult &result, std::string &error)
{
    if (!positiveFinite(in.primaryDimensionM))
    {
        error = "Primary dimension must be finite and > 0.";
        return false;
    }
    if (kind == AntennaKind::ParabolicDish)
    {
        error = "A dish diameter alone does not define an operating frequency. Enter frequency + diameter, or use the desired-gain result.";
        return false;
    }

    double coefficient = 0.0; // primary = coefficient * lambda * VF
    switch (kind)
    {
    case AntennaKind::ExactHalfWaveDipole: coefficient = 0.25; break;
    case AntennaKind::ResonantDipole0475: coefficient = 0.2375; break;
    case AntennaKind::QuarterWaveMonopole:
    case AntennaKind::GroundPlaneQuarterWave: coefficient = 0.25; break;
    case AntennaKind::InvertedVee: coefficient = 0.2375; break;
    case AntennaKind::FoldedDipole: coefficient = 0.475; break;
    case AntennaKind::FiveEighthMonopole: coefficient = 0.625; break;
    case AntennaKind::FullWaveLoop: coefficient = 1.0; break;
    case AntennaKind::Jpole: coefficient = 0.75; break;
    case AntennaKind::SlimJim: coefficient = 0.75; break;
    case AntennaKind::AxialModeHelix: coefficient = 1.0; break;
    case AntennaKind::Yagi3Element: coefficient = 0.475; break;
    case AntennaKind::RectangularPatch:
    {
        double f = 0.0;
        if (!inversePatchFrequency(in.primaryDimensionM, in.relativePermittivity, in.substrateHeightM, f, error)) return false;
        AntennaInputs next = in;
        next.frequencyHz = f;
        if (!antennaFromFrequency(kind, next, result, error)) return false;
        result.primaryDimensionM = in.primaryDimensionM;
        return true;
    }
    case AntennaKind::ParabolicDish: break;
    }

    const double velocityFactor = vf(in);
    const double lambdaFree = in.primaryDimensionM / (coefficient * velocityFactor);
    AntennaInputs next = in;
    next.frequencyHz = c0 / lambdaFree;
    if (!antennaFromFrequency(kind, next, result, error)) return false;
    result.primaryDimensionM = in.primaryDimensionM;
    return true;
}

ExpressionResult evaluateExpression(const std::string &expression)
{
    return Parser(expression).run();
}

std::vector<std::pair<std::string, double>> constantTable()
{
    return {
        {"pi", pi}, {"e", euler}, {"eps0", epsilon0}, {"mu0", mu0}, {"c0", c0},
        {"ke", coulombK}, {"eta0", eta0}, {"h", planckH}, {"hbar", hbar},
        {"qe", elementaryCharge}, {"me", electronMass}
    };
}

} // namespace EmCalculator
