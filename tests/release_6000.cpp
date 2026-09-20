#include "antenna_surface_geometry.h"
#include "antenna_validation_bench.h"
#include "hybrid_wire_surface_mom.h"

#include <cmath>
#include <complex>
#include <iostream>
#include <string>
#include <vector>

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

int checkProductionCase(AntennaValidation::CaseKind kind)
{
    std::cout << "[6.0 gate] " << AntennaValidation::caseName(kind) << " ..." << std::endl;
    const auto report = AntennaValidation::runProductionCase(kind, 300e6);
    if (report.rows.size() != 1) return 10;
    const auto &row = report.rows.front();
    if (!row.productionPath || !row.valid || row.verdict != AntennaValidation::Verdict::Pass) return 11;
    if (!finiteComplex(row.inputImpedanceOhm) || row.inputImpedanceOhm.real() < 0.0) return 12;
    if (!std::isfinite(row.meshDeltaPercent) || !std::isfinite(row.patternRmsPercent) ||
        !std::isfinite(row.directivityDbi) || !finiteVector(row.patternCalculated) || !finiteVector(row.patternReference)) return 13;

    switch (kind)
    {
    case AntennaValidation::CaseKind::HalfWaveDipole:
        if (!(row.patternRmsPercent < 2.0)) return 14;
        break;
    case AntennaValidation::CaseKind::SmallCircularLoop:
        if (!(row.patternRmsPercent < 15.0)) return 15;
        break;
    case AntennaValidation::CaseKind::QuarterWaveMonopole:
        if (!(row.meshDeltaPercent < 10.0 && row.patternMeshDeltaRmsPercent < 5.0)) return 16;
        break;
    case AntennaValidation::CaseKind::RectangularPatch:
        if (row.primaryMetricName != "fJ2peak/f0" || !(row.meshDeltaPercent <= 7.5) ||
            !(row.primaryMetricErrorPercent <= 10.0)) return 17;
        break;
    }

    std::cout << "  PASS Zin=" << row.inputImpedanceOhm
              << " mesh=" << row.meshDeltaPercent << "%"
              << " patternRMS=" << row.patternRmsPercent << "%";
    if (!row.primaryMetricName.empty())
        std::cout << " " << row.primaryMetricName << "=" << row.primaryMetricValue
                  << " error=" << row.primaryMetricErrorPercent << "%";
    std::cout << std::endl;
    return 0;
}

int checkHomogeneousVia()
{
    std::cout << "[6.0 gate] homogeneous eps_r -> 1 via ..." << std::endl;
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

    std::cout << "  PASS Pprop/Pfree=" << ratio
              << " Zin=" << r1.feeds.front().inputImpedanceOhm
              << " P2/P1=" << r2.acceptedPowerW / r1.acceptedPowerW << std::endl;
    return 0;
}
} // namespace

int main()
{
    for (const auto kind : {AntennaValidation::CaseKind::HalfWaveDipole,
                            AntennaValidation::CaseKind::SmallCircularLoop,
                            AntennaValidation::CaseKind::QuarterWaveMonopole,
                            AntennaValidation::CaseKind::RectangularPatch})
    {
        const int rc = checkProductionCase(kind);
        if (rc != 0) return rc;
    }
    const int homogeneous = checkHomogeneousVia();
    if (homogeneous != 0) return homogeneous;
    std::cout << "QTsignalApp 6.0.0 Simulation / Antenna Designer 1.0 release gate: PASS" << std::endl;
    return 0;
}
