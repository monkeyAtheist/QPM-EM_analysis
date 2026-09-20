#include "antenna_surface_geometry.h"
#include "antenna_validation_bench.h"
#include "hybrid_wire_surface_mom.h"

#include <cmath>
#include <complex>
#include <iostream>
#include <string>

namespace
{
using M = HybridWireSurfaceMom::DielectricKernelModel;

bool finiteComplex(const std::complex<double> &z)
{
    return std::isfinite(z.real()) && std::isfinite(z.imag());
}

bool finiteVector(const std::vector<double> &v)
{
    for (double x : v)
        if (!std::isfinite(x)) return false;
    return true;
}

HybridWireSurfaceMom::Input homogeneousVia(double voltage)
{
    HybridWireSurfaceMom::Input in;
    in.wire.frequencyHz = 300e6;
    in.wire.computeFarField = true;
    in.computeFarField = true;
    in.wire.currentBasisTreatment = NumericalEM::WireCurrentBasisTreatment::LinearRooftopCharge;
    in.wire.segmentsPerWavelength = 10;
    in.wire.maxUnknowns = 50;
    in.maxTotalUnknowns = 120;
    in.maxSurfaceUnknowns = 80;

    const double x = 0.07;
    in.wire.wires.push_back({"VIA", {x, 0.0, -0.009}, {x, 0.0, -0.001}, 0.0005});
    in.wire.feeds.push_back({"PORT", {x, 0.0, -0.005}, {voltage, 0.0}, 50.0});

    AntennaSurface::SurfaceSpec surface;
    surface.kind = AntennaSurface::SurfaceKind::Rectangle;
    surface.center = {0.0, 0.0, 0.0};
    surface.widthM = 0.18;
    surface.heightM = 0.14;
    surface.meshHintM = 0.07;
    const auto triangles = AntennaSurface::triangulate(surface, 28);
    for (const auto &t : triangles) in.triangles.push_back({t.a, t.b, t.c, 0});

    HybridWireSurfaceMom::DielectricRegion d;
    d.name = "homogeneous-limit slab";
    d.centerM = {0.0, 0.0, -0.005};
    d.widthM = 0.18;
    d.heightM = 0.14;
    d.thicknessM = 0.01;
    d.relativePermittivity = 1.0;
    d.lossTangent = 0.0;
    d.fieldFillFactor = 0.0;
    in.dielectrics.push_back(d);
    in.useEffectiveDielectricRegions = true;
    in.dielectricKernelModel = M::LayeredSlabSommerfeldPropagatingFarField;
    return in;
}

int checkCanonicalCampaign()
{
    const auto campaign = AntennaValidation::runCampaign(300e6);
    if (campaign.cases.size() != 4) return 10;

    int productionTotal = 0;
    int productionPass = 0;
    bool sawDipole = false;
    bool sawLoop = false;
    bool sawMonopole = false;
    bool sawPatch = false;

    for (const auto &caseReport : campaign.cases)
    {
        for (const auto &row : caseReport.rows)
        {
            if (!row.productionPath) continue;
            ++productionTotal;
            if (row.verdict == AntennaValidation::Verdict::Pass) ++productionPass;

            if (!row.valid || !finiteComplex(row.inputImpedanceOhm) || !(row.inputImpedanceOhm.real() >= 0.0)) return 11;
            if (!std::isfinite(row.meshDeltaPercent) || !std::isfinite(row.patternRmsPercent) ||
                !std::isfinite(row.directivityDbi) || !finiteVector(row.patternCalculated) || !finiteVector(row.patternReference)) return 12;

            switch (row.caseKind)
            {
            case AntennaValidation::CaseKind::HalfWaveDipole:
                sawDipole = true;
                if (!(row.patternRmsPercent < 2.0)) return 13;
                break;
            case AntennaValidation::CaseKind::SmallCircularLoop:
                sawLoop = true;
                if (!(row.patternRmsPercent < 15.0)) return 14;
                break;
            case AntennaValidation::CaseKind::QuarterWaveMonopole:
                sawMonopole = true;
                if (!(row.meshDeltaPercent < 10.0 && row.patternMeshDeltaRmsPercent < 5.0)) return 15;
                break;
            case AntennaValidation::CaseKind::RectangularPatch:
                sawPatch = true;
                if (row.primaryMetricName != "fJ2peak/f0" || !(row.meshDeltaPercent <= 7.5) ||
                    !(row.primaryMetricErrorPercent <= 10.0)) return 16;
                break;
            }
        }
    }

    if (productionTotal != 4 || productionPass != 4 || !sawDipole || !sawLoop || !sawMonopole || !sawPatch) return 17;
    std::cout << "canonical production campaign=" << productionPass << "/" << productionTotal << " PASS\n";
    return 0;
}

int checkHomogeneousVia()
{
    const auto r1 = HybridWireSurfaceMom::solve(homogeneousVia(1.0));
    const auto r2 = HybridWireSurfaceMom::solve(homogeneousVia(2.0));
    if (!r1.valid || !r2.valid || r1.feeds.size() != 1 || r2.feeds.size() != 1) return 20;
    if (!(r1.acceptedPowerW > 0.0) || !(r2.acceptedPowerW > 0.0)) return 21;
    if (!r1.layeredSommerfeldPropagatingFarFieldUsed || !r1.layeredSommerfeldPropagatingFarFieldPowerConsistent) return 22;
    if (!(r1.radiatedPowerW > 0.0) || !(r1.layeredRadiatedPowerTotalW > 0.0)) return 23;

    const double ratio = r1.layeredRadiatedPowerTotalW / r1.radiatedPowerW;
    if (!std::isfinite(ratio) || std::abs(ratio - 1.0) > 5e-4) return 24;
    if (std::abs(r2.feeds.front().inputImpedanceOhm - r1.feeds.front().inputImpedanceOhm) >
        1e-8 * std::max(1.0, std::abs(r1.feeds.front().inputImpedanceOhm))) return 25;
    if (std::abs(r2.acceptedPowerW / r1.acceptedPowerW - 4.0) > 1e-7) return 26;

    std::cout << "homogeneous via Pprop/Pfree=" << ratio
              << " Zin=" << r1.feeds.front().inputImpedanceOhm << "\n";
    return 0;
}
} // namespace

int main()
{
    const int campaign = checkCanonicalCampaign();
    if (campaign != 0) return campaign;
    const int homogeneous = checkHomogeneousVia();
    if (homogeneous != 0) return homogeneous;
    std::cout << "QTsignalApp 5.42-B full release regression: PASS\n";
    return 0;
}
