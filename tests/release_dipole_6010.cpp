#include "numerical_em_solvers.h"

#include <cmath>
#include <complex>
#include <iostream>

namespace
{
constexpr double C0 = 299792458.0;

NumericalEM::WireNetworkMomInput base(double frequencyHz)
{
    NumericalEM::WireNetworkMomInput in;
    in.frequencyHz = frequencyHz;
    in.segmentsPerWavelength = 80;
    in.maxUnknowns = 450;
    in.nodeMergeToleranceM = 1e-6;
    in.maxSegmentLengthRadiusFactor = 3.5;
    in.junctionLocalSubdivisions = 1;
    in.junctionTreatment = NumericalEM::WireJunctionTreatment::LagrangeKcl;
    in.currentBasisTreatment = NumericalEM::WireCurrentBasisTreatment::PulsePointMatching;
    in.computeFarField = true;
    in.farFieldCutStepDeg = 2.5;
    in.farFieldIntegrationStepDeg = 5.0;
    return in;
}

bool near(double a, double b, double tol) { return std::abs(a-b) <= tol; }
}

int main()
{
    // Exact geometry from the reported antenna.qta. This intentionally verifies that
    // the old 2.629 dBi result is the physical consequence of a two-component,
    // two-source topology rather than an accidental change in the radiation renderer.
    auto split = base(100e6);
    split.wires = {{"W1", {-1.025,0,0}, {-0.025,0,0}, 0.005},
                   {"W2", { 0.025,0,0}, { 1.025,0,0}, 0.005}};
    split.feeds = {{"F1", {-0.030,0,0}, {1,0}, 50},
                   {"F2", { 0.030,0,0}, {1,0}, 50}};
    const auto rs = NumericalEM::solveWireNetworkMom(split);
    if (!rs.valid || rs.componentCount != 2 || rs.feeds.size() != 2) return 10;
    if (!near(rs.directivityDbi, 2.62935, 0.01)) return 11;
    if (!near(rs.feeds.front().activeImpedanceOhm.real(), 72.3785, 0.2) ||
        !near(rs.feeds.front().activeImpedanceOhm.imag(), -2246.68, 2.0)) return 12;

    // Correct 100 MHz half-wave topology: total L=lambda/2, two spans share the
    // centre node and ONE delta-gap source is applied at that degree-2 node.
    auto dip = base(100e6);
    const double totalLength = 0.5*C0/100e6;
    dip.wires = {{"LEFT", {-0.5*totalLength,0,0}, {0,0,0}, 0.005},
                 {"RIGHT", {0,0,0}, {0.5*totalLength,0,0}, 0.005}};
    dip.feeds = {{"CENTER", {0,0,0}, {1,0}, 50}};
    const auto rd = NumericalEM::solveWireNetworkMom(dip);
    if (!rd.valid || rd.componentCount != 1 || rd.feeds.size() != 1) return 20;
    if (!(rd.directivityDbi > 2.05 && rd.directivityDbi < 2.30)) return 21;
    if (!(rd.feeds.front().activeImpedanceOhm.real() > 0.0)) return 22;

    // With 2 m total metal length, the exact geometrical lambda/2 frequency is
    // c/(2L) ~= 74.948 MHz. Keep the same one-port centre-feed topology.
    auto twoMetre = base(C0/4.0);
    twoMetre.wires = {{"LEFT", {-1,0,0}, {0,0,0}, 0.005},
                      {"RIGHT", {0,0,0}, {1,0,0}, 0.005}};
    twoMetre.feeds = {{"CENTER", {0,0,0}, {1,0}, 50}};
    const auto r2 = NumericalEM::solveWireNetworkMom(twoMetre);
    if (!r2.valid || r2.componentCount != 1) return 30;
    if (!(r2.directivityDbi > 2.05 && r2.directivityDbi < 2.30)) return 31;

    std::cout << "split topology @100 MHz: D=" << rs.directivityDbi
              << " dBi, Z=" << rs.feeds.front().activeImpedanceOhm
              << ", components=" << rs.componentCount << "\n";
    std::cout << "true lambda/2 @100 MHz: D=" << rd.directivityDbi
              << " dBi, Z=" << rd.feeds.front().activeImpedanceOhm
              << ", components=" << rd.componentCount << "\n";
    std::cout << "2 m true lambda/2 @" << (C0/4.0/1e6) << " MHz: D=" << r2.directivityDbi
              << " dBi, Z=" << r2.feeds.front().activeImpedanceOhm << "\n";
    std::cout << "QTsignalApp 6.0.1 dipole topology regression: PASS\n";
    return 0;
}
