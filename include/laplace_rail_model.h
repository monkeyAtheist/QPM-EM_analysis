#pragma once

#include <string>
#include <vector>

namespace LaplaceRail
{

enum class Mode
{
    ConstantCurrent = 0,
    VoltageDriven = 1,
    GeneratorBraking = 2
};

struct Inputs
{
    Mode mode = Mode::ConstantCurrent;
    double magneticFieldT = 0.50;
    double railSeparationM = 0.10;
    double fieldAngleDeg = 90.0;
    double massKg = 0.050;
    double currentA = 2.0;
    double sourceVoltageV = 6.0;
    double totalResistanceOhm = 2.0;
    double initialVelocityMps = 0.0;
    double initialPositionM = 0.0;
    double durationS = 1.0;
    int samples = 401;
};

struct State
{
    double timeS = 0.0;
    double positionM = 0.0;
    double velocityMps = 0.0;
    double currentA = 0.0;
    double motionalEmfV = 0.0;
    double forceN = 0.0;
    double accelerationMps2 = 0.0;
    double mechanicalPowerW = 0.0;
    double joulePowerW = 0.0;
    double sourcePowerW = 0.0;
};

struct Result
{
    bool valid = false;
    std::string error;
    double couplingNPerA = 0.0; // k = B L sin(theta), also V/(m/s)
    double terminalVelocityMps = 0.0;
    double timeConstantS = 0.0;
    std::vector<State> states;
};

Result solve(const Inputs &in);
State stateAt(const Inputs &in, double timeS, std::string *error = nullptr);
const char *modeName(Mode mode);

} // namespace LaplaceRail
