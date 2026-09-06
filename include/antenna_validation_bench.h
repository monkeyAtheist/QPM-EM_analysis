#pragma once

#include <complex>
#include <string>
#include <vector>

namespace AntennaValidation
{

enum class CaseKind
{
    HalfWaveDipole = 0,
    SmallCircularLoop = 1,
    QuarterWaveMonopole = 2,
    RectangularPatch = 3
};

enum class Verdict
{
    Pass,
    Warning,
    Fail,
    Error,
    Informational
};

struct SolverRow
{
    CaseKind caseKind = CaseKind::HalfWaveDipole;
    std::string caseName;
    std::string solverName;
    bool valid = false;
    std::string error;
    std::string referenceSummary;
    Verdict verdict = Verdict::Error;
    std::string verdictReason;
    // 5.41: only productionPath rows participate in the frozen Simulation/Designer 1.0 gates.
    // Experimental/legacy rows remain visible as diagnostics and are never silently removed.
    bool productionPath = true;

    int currentUnknowns = 0;
    int fineUnknowns = 0;
    std::complex<double> inputImpedanceOhm{0.0, 0.0};
    std::complex<double> referenceImpedanceOhm{0.0, 0.0};
    bool hasReferenceImpedance = false;
    double meshDeltaPercent = 0.0;
    double impedanceReferenceErrorPercent = 0.0;

    double directivityDbi = 0.0;
    double referenceDirectivityDbi = 0.0;
    bool hasReferenceDirectivity = false;
    double directivityErrorDb = 0.0;
    double patternRmsPercent = 0.0;
    double patternMeshDeltaRmsPercent = 0.0; // coarse-vs-fine normalized-pattern RMS, when both are available

    // Case-specific scalar: small-loop radiation resistance or patch resonant-frequency offset.
    std::string primaryMetricName;
    double primaryMetricValue = 0.0;
    double primaryMetricReference = 0.0;
    double primaryMetricErrorPercent = 0.0;

    double reciprocityRelative = 0.0;
    double reciprocityPreSymmetryRelative = 0.0;
    double boundaryResidualRelative = 0.0;

    std::vector<double> patternAngleDeg;
    std::vector<double> patternCalculated;
    std::vector<double> patternReference;
};

struct CaseReport
{
    CaseKind kind = CaseKind::HalfWaveDipole;
    std::string name;
    std::string note;
    std::vector<SolverRow> rows;
};

struct CampaignReport
{
    double frequencyHz = 0.0;
    std::vector<CaseReport> cases;
    std::string methodologyNote;
};

const char *caseName(CaseKind kind);
const char *verdictName(Verdict verdict);
CaseReport runCase(CaseKind kind, double frequencyHz);
// 6.0.0 release-gate helper: executes only the frozen production path for the requested case.
// Legacy/experimental diagnostics remain available through runCase()/runCampaign().
CaseReport runProductionCase(CaseKind kind, double frequencyHz);
CampaignReport runCampaign(double frequencyHz);

} // namespace AntennaValidation
