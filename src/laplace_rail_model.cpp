#include "laplace_rail_model.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace LaplaceRail
{
namespace
{
constexpr double Pi = 3.141592653589793238462643383279502884;
constexpr double Tiny = 1.0e-15;

bool finite(double v) { return std::isfinite(v); }

double coupling(const Inputs &in)
{
    return in.magneticFieldT * in.railSeparationM * std::sin(in.fieldAngleDeg * Pi / 180.0);
}

bool validate(const Inputs &in, std::string &error)
{
    const double values[] = {in.magneticFieldT, in.railSeparationM, in.fieldAngleDeg, in.massKg,
                             in.currentA, in.sourceVoltageV, in.totalResistanceOhm,
                             in.initialVelocityMps, in.initialPositionM, in.durationS};
    for (double v : values)
    {
        if (!finite(v)) { error = "Laplace rail input contains NaN or Inf."; return false; }
    }
    if (in.magneticFieldT < 0.0) { error = "Magnetic field magnitude B must be >= 0."; return false; }
    if (!(in.railSeparationM > 0.0)) { error = "Rail separation / rod active length L must be > 0."; return false; }
    if (!(in.massKg > 0.0)) { error = "Moving rod mass m must be > 0."; return false; }
    if (!(in.durationS >= 0.0)) { error = "Simulation duration must be >= 0."; return false; }
    if ((in.mode == Mode::VoltageDriven || in.mode == Mode::GeneratorBraking) && !(in.totalResistanceOhm > 0.0))
    {
        error = "Total circuit resistance R must be > 0 in voltage-driven/generator modes.";
        return false;
    }
    return true;
}

State computeState(const Inputs &in, double t)
{
    State s;
    s.timeS = std::max(0.0, t);
    const double k = coupling(in);

    if (in.mode == Mode::ConstantCurrent)
    {
        s.currentA = in.currentA;
        s.forceN = k * s.currentA;
        s.accelerationMps2 = s.forceN / in.massKg;
        s.velocityMps = in.initialVelocityMps + s.accelerationMps2 * s.timeS;
        s.positionM = in.initialPositionM + in.initialVelocityMps * s.timeS
                    + 0.5 * s.accelerationMps2 * s.timeS * s.timeS;
        s.motionalEmfV = k * s.velocityMps;
        s.mechanicalPowerW = s.forceN * s.velocityMps;
        s.joulePowerW = (in.totalResistanceOhm > 0.0) ? s.currentA * s.currentA * in.totalResistanceOhm : 0.0;
        s.sourcePowerW = s.mechanicalPowerW + s.joulePowerW;
        return s;
    }

    if (std::abs(k) <= Tiny)
    {
        s.velocityMps = in.initialVelocityMps;
        s.positionM = in.initialPositionM + in.initialVelocityMps * s.timeS;
        s.motionalEmfV = 0.0;
        if (in.mode == Mode::VoltageDriven)
        {
            s.currentA = in.sourceVoltageV / in.totalResistanceOhm;
            s.joulePowerW = s.currentA * s.currentA * in.totalResistanceOhm;
            s.sourcePowerW = in.sourceVoltageV * s.currentA;
        }
        return s;
    }

    const double tau = in.massKg * in.totalResistanceOhm / (k * k);
    const double decay = std::exp(-s.timeS / tau);

    if (in.mode == Mode::VoltageDriven)
    {
        const double vInf = in.sourceVoltageV / k;
        s.velocityMps = vInf + (in.initialVelocityMps - vInf) * decay;
        s.positionM = in.initialPositionM + vInf * s.timeS
                    + (in.initialVelocityMps - vInf) * tau * (1.0 - decay);
        s.motionalEmfV = k * s.velocityMps;
        s.currentA = (in.sourceVoltageV - s.motionalEmfV) / in.totalResistanceOhm;
        s.forceN = k * s.currentA;
        s.accelerationMps2 = s.forceN / in.massKg;
        s.mechanicalPowerW = s.forceN * s.velocityMps;
        s.joulePowerW = s.currentA * s.currentA * in.totalResistanceOhm;
        s.sourcePowerW = in.sourceVoltageV * s.currentA;
        return s;
    }

    // Generator/braking mode: no applied source. Lenz's law is represented by
    // a current and force opposing the initial motion.
    s.velocityMps = in.initialVelocityMps * decay;
    s.positionM = in.initialPositionM + in.initialVelocityMps * tau * (1.0 - decay);
    s.motionalEmfV = k * s.velocityMps;
    s.currentA = -s.motionalEmfV / in.totalResistanceOhm;
    s.forceN = k * s.currentA;
    s.accelerationMps2 = s.forceN / in.massKg;
    s.mechanicalPowerW = s.forceN * s.velocityMps;
    s.joulePowerW = s.currentA * s.currentA * in.totalResistanceOhm;
    s.sourcePowerW = 0.0;
    return s;
}

bool finiteState(const State &s)
{
    return finite(s.timeS) && finite(s.positionM) && finite(s.velocityMps) && finite(s.currentA)
        && finite(s.motionalEmfV) && finite(s.forceN) && finite(s.accelerationMps2)
        && finite(s.mechanicalPowerW) && finite(s.joulePowerW) && finite(s.sourcePowerW);
}
} // namespace

const char *modeName(Mode mode)
{
    switch (mode)
    {
    case Mode::ConstantCurrent: return "Constant current";
    case Mode::VoltageDriven: return "Voltage source + motional EMF";
    case Mode::GeneratorBraking: return "Generator / magnetic braking";
    }
    return "Unknown";
}

State stateAt(const Inputs &in, double timeS, std::string *error)
{
    std::string local;
    if (!validate(in, local))
    {
        if (error) *error = local;
        return {};
    }
    const State s = computeState(in, timeS);
    if (!finiteState(s) && error) *error = "Laplace rail calculation produced a non-finite state.";
    return s;
}

Result solve(const Inputs &in)
{
    Result out;
    if (!validate(in, out.error)) return out;

    out.couplingNPerA = coupling(in);
    const double k = out.couplingNPerA;
    if ((in.mode == Mode::VoltageDriven || in.mode == Mode::GeneratorBraking) && std::abs(k) > Tiny)
    {
        out.timeConstantS = in.massKg * in.totalResistanceOhm / (k * k);
        if (in.mode == Mode::VoltageDriven) out.terminalVelocityMps = in.sourceVoltageV / k;
    }

    const int n = std::clamp(in.samples, 2, 5001);
    out.states.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        const double t = (n <= 1) ? 0.0 : in.durationS * static_cast<double>(i) / static_cast<double>(n - 1);
        State s = computeState(in, t);
        if (!finiteState(s))
        {
            out.error = "Laplace rail calculation produced NaN/Inf. Check the parameter scale.";
            out.states.clear();
            return out;
        }
        out.states.push_back(s);
    }
    out.valid = true;
    return out;
}

} // namespace LaplaceRail
