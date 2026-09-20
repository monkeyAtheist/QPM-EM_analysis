from pathlib import Path
import json, subprocess, tempfile, xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1]
project=json.loads((ROOT/'QTsignalApp.qtproject.json').read_text())
main=(ROOT/'src/main.cpp').read_text()
designer=(ROOT/'src/antenna_designer_widget.cpp').read_text()
header=(ROOT/'include/widgets/antenna_designer_widget.h').read_text()
refs=(ROOT/'src/electromagnetism_reference_widget.cpp').read_text()
change=(ROOT/'CHANGELOG_APP.md').read_text()
roadmap=(ROOT/'SIMULATION_DESIGN_V1_ROADMAP.md').read_text()

hyb_start=designer.index('void AntennaDesignerWidget::solveHybridMom()')
sweep_start=designer.index('void AntennaDesignerWidget::solveFrequencySweep()')
opt_start=designer.index('void AntennaDesignerWidget::optimizeGeometry()',sweep_start)
hyb_chunk=designer[hyb_start:sweep_start]
sweep_chunk=designer[sweep_start:opt_start]
checks={
    'version': project.get('version')=='5.40.0' and project.get('packaging',{}).get('productVersion')=='5.40.0' and 'QStringLiteral("5.40.0")' in main,
    'shared builder api': all(x in header for x in ['buildHybridInput(double frequencyHz, bool computeFarField','hybridResultFeedIndex','m_sweepSolver']),
    'single uses shared builder': 'buildHybridInput(m_frequencyMHz->value() * 1e6, true, in, inputError)' in hyb_chunk,
    'sweep uses shared builder': all(x in sweep_chunk for x in ['buildHybridInput(m_frequencyMHz->value()*1e6, false, hybridTemplate, hybridInputError)','auto in = hybridTemplate','in.wire.frequencyHz = fm * 1e6','HybridWireSurfaceMom::solve(in)','hybridResultFeedIndex(result, observedFeed)']),
    'auto hybrid ui': all(x in designer for x in ['Auto — full geometry when needed','Hybrid wire + PEC/RWG + dielectric','Thin-wire MoM (legacy / fast)','Same Hybrid input builder/ports/mesh/material settings as the single-frequency solve']),
    'fallback diagnostics': 'layeredSommerfeldComplexTransitionFallbackUsed' in sweep_chunk and 'passive guard fell back' in sweep_chunk,
    'plots export': all(x in sweep_chunk for x in ['m_sweepZPlot->setSeries','m_sweepS11Plot->setSeries','m_sweepVswrPlot->setSeries','m_sweepSmith->setSweep','emit antennaSweepAvailable']),
    'persistence': all(x in designer for x in ['sweepSolverIndex','root.insert(QStringLiteral("version"), 19)']),
    'refs': all(x in refs for x in ['Since 5.40','Hybrid broadband sweep (5.40)','exact Hybrid port configuration','sequence of independent full coupled solves']),
    'roadmap': all(x in roadmap for x in ['Current baseline — 5.40','5.40 — Hybrid broadband sweep — COMPLETE','Closed in 5.40','5.41 — Validation closure']),
    'changelog': change.startswith('# QTsignalApp 5.40.0') and 'Hybrid broadband sweep and shared single-frequency input path' in change,
}
failed=[k for k,v in checks.items() if not v]
if failed: raise SystemExit('5.40 static verification FAILED: '+', '.join(failed))
for ui in (ROOT/'forms').glob('*.ui'): ET.parse(ui)

program=r'''
#include "hybrid_wire_surface_mom.h"
#include "antenna_surface_geometry.h"
#include "microstrip_models.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>
#include <vector>

HybridWireSurfaceMom::Input patchBase(){
    constexpr double C0=299792458.0;
    const double f0=300e6, la=C0/f0, h=.0016, er=4.2;
    const auto e=MicrostripModels::rectangularPatch(f0,h,er);
    if(!e.valid) return {};
    const double w=e.widthM,l=e.physicalLengthM,ee=e.effectivePermittivity;
    const double gw=std::max(1.35*w,w+12*h), gl=std::max(1.35*l,l+12*h), fy=-.22*l;
    HybridWireSurfaceMom::Input in;
    in.wire.frequencyHz=f0; in.wire.computeFarField=false; in.computeFarField=false;
    AntennaSurface::SurfaceSpec g; g.kind=AntennaSurface::SurfaceKind::Rectangle; g.center={0,0,0}; g.widthM=gw; g.heightM=gl; g.meshHintM=la/12;
    auto p=g; p.center={0,0,h}; p.widthM=w; p.heightM=l; p.meshHintM=la/16;
    const auto gm=AntennaSurface::triangulate(g,64), pm=AntennaSurface::triangulate(p,64);
    for(const auto&t:gm) in.triangles.push_back({t.a,t.b,t.c,0});
    for(const auto&t:pm) in.triangles.push_back({t.a,t.b,t.c,1});
    HybridWireSurfaceMom::DifferentialSurfaceFeed sf; sf.name="PATCH"; sf.positivePositionM={0,fy,h}; sf.negativePositionM={0,fy,0}; sf.voltageV={1,0}; sf.referenceOhm=50; sf.positiveSurfaceIndex=1; sf.negativeSurfaceIndex=0; sf.mappingToleranceM=std::max(w,l)/3; sf.footprintRadiusM=.08*la; in.differentialSurfaceFeeds.push_back(sf);
    HybridWireSurfaceMom::DielectricRegion d; d.name="substrate"; d.centerM={0,0,.5*h}; d.widthM=gw; d.heightM=gl; d.thicknessM=h; d.relativePermittivity=er; d.lossTangent=.02; d.fieldFillFactor=std::clamp((ee-1)/(er-1),0.,1.); in.dielectrics.push_back(d);
    in.maxTotalUnknowns=520; in.maxSurfaceUnknowns=420; in.enableTerminalHalfRwg=false; in.useEffectiveDielectricRegions=true;
    in.dielectricKernelModel=HybridWireSurfaceMom::DielectricKernelModel::LayeredSlabSommerfeldGroundedPecModeAudit;
    return in;
}

int main(){
    auto base=patchBase(); if(base.triangles.empty()) return 2;
    const std::vector<double> freq{270e6,300e6,330e6};
    std::vector<std::complex<double>> zin;
    for(double f:freq){
        auto in=base; in.wire.frequencyHz=f; in.computeFarField=false; in.wire.computeFarField=false;
        auto r=HybridWireSurfaceMom::solve(in); if(!r.valid||r.feeds.size()!=1) return 3;
        const auto &pf=r.feeds[0]; const auto expected=(pf.inputImpedanceOhm-std::complex<double>(pf.referenceOhm,0))/(pf.inputImpedanceOhm+std::complex<double>(pf.referenceOhm,0));
        if(std::abs(expected-pf.reflectionCoefficient)>1e-10) return 4;
        if(!std::isfinite(pf.inputImpedanceOhm.real())||!std::isfinite(pf.inputImpedanceOhm.imag())||pf.inputImpedanceOhm.real()<0.0) return 5;
        if(!r.farFieldNormalized.empty()||!r.azimuthNormalizedFarField.empty()||!r.elevationNormalizedFarField.empty()) return 6;
        zin.push_back(pf.inputImpedanceOhm);
    }
    auto direct=base; direct.wire.frequencyHz=300e6; direct.computeFarField=false; direct.wire.computeFarField=false;
    const auto rd=HybridWireSurfaceMom::solve(direct); if(!rd.valid||rd.feeds.size()!=1) return 7;
    if(std::abs(rd.feeds[0].inputImpedanceOhm-zin[1])>1e-11*std::max(1.0,std::abs(zin[1]))) return 8;
    if(std::abs(zin.front()-zin.back())<0.1) return 9;
    std::cout<<"5.40 hybrid sweep Zin 270/300/330 MHz: "<<zin[0]<<" | "<<zin[1]<<" | "<<zin[2]<<"\n";
    std::cout<<"5.40 center-point direct parity delta="<<std::abs(rd.feeds[0].inputImpedanceOhm-zin[1])<<" ohm\n";
    return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    td=Path(td); cpp=td/'verify540.cpp'; exe=td/'verify540'; cpp.write_text(program)
    cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-I'+str(ROOT/'include'),
         str(ROOT/'src/numerical_em_solvers.cpp'),str(ROOT/'src/pec_surface_mom.cpp'),str(ROOT/'src/hybrid_wire_surface_mom.cpp'),str(ROOT/'src/antenna_surface_geometry.cpp'),str(ROOT/'src/microstrip_models.cpp'),str(cpp),'-o',str(exe)]
    subprocess.run(cmd,check=True)
    subprocess.run([str(exe)],check=True,timeout=480)
print('QTsignalApp 5.40.0 hybrid broadband sweep verification: PASS')
