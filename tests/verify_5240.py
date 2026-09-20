from pathlib import Path
import json, math, re, subprocess, tempfile, hashlib

ROOT=Path(__file__).resolve().parents[1]
project=json.loads((ROOT/'QTsignalApp.qtproject.json').read_text(encoding='utf-8'))
main=(ROOT/'src/main.cpp').read_text(encoding='utf-8')
hybrid=(ROOT/'src/hybrid_wire_surface_mom.cpp').read_text(encoding='utf-8')
hybrid_h=(ROOT/'include/hybrid_wire_surface_mom.h').read_text(encoding='utf-8')
designer=(ROOT/'src/antenna_designer_widget.cpp').read_text(encoding='utf-8')
refs=(ROOT/'src/electromagnetism_reference_widget.cpp').read_text(encoding='utf-8')
bench=(ROOT/'src/antenna_validation_bench.cpp').read_text(encoding='utf-8')
change=(ROOT/'CHANGELOG_APP.md').read_text(encoding='utf-8')

checks={
    'version': project.get('version')=='5.24.0' and project.get('packaging',{}).get('productVersion')=='5.24.0' and 'QStringLiteral("5.24.0")' in main,
    'api footprint': 'footprintRadiusM = 0.0' in hybrid_h and 'minDifferentialPortFootprintRadiusM' in hybrid_h and 'differentialPortSymmetricVoltageSplitUsed' in hybrid_h,
    'smooth functional': all(x in hybrid for x in ['buildSmoothSurfaceTerminalStencil','std::exp(-0.5*r2/(out.patchRadiusM*out.patchRadiusM))','triangleQuadratureNear(t)']),
    'symmetric duality': all(x in hybrid for x in ['+=0.5*t.integratedDivergenceM','-=0.5*t.integratedDivergenceM','ip=-0.5*(ipos-ineg)','factor-of-four Zin normalization error']),
    'ui radius': all(x in designer for x in ['Differential port σ','hybridDifferentialPortRadiusMm','Auto (mesh-local)','sf.footprintRadiusM']),
    'persistence': 'root.insert(QStringLiteral("hybridDifferentialPortRadiusMm")' in designer and 'root.contains(QStringLiteral("hybridDifferentialPortRadiusMm"))' in designer,
    'benchmark fixed footprint': 'sf.footprintRadiusM=0.08*lambda0' in bench and 'Gaussian differential surface port' in bench,
    'references': 'Differential RWG port normalization / footprint (5.24)' in refs and 'power-dual' in refs,
    'changelog': change.startswith('# QTsignalApp 5.24.0') and 'Mesh-stable, power-dual differential RWG ports' in change and '.qta` remains version 19' in change,
    'qta unchanged': 'root.insert(QStringLiteral("version"), 19)' in designer,
}
failed=[k for k,v in checks.items() if not v]
if failed:
    raise SystemExit('QTsignalApp 5.24.0 static verification FAILED: '+', '.join(failed))

program=r'''
#include "antenna_validation_bench.h"
#include "hybrid_wire_surface_mom.h"
#include "antenna_surface_geometry.h"
#include <cmath>
#include <complex>
#include <iostream>

static HybridWireSurfaceMom::Input twoPlate(double voltage){
    HybridWireSurfaceMom::Input in;in.wire.frequencyHz=30e6;in.wire.computeFarField=false;in.computeFarField=false;
    const double s=.12,h=.01;
    AntennaSurface::SurfaceSpec a;a.kind=AntennaSurface::SurfaceKind::Rectangle;a.widthM=2*s;a.heightM=2*s;a.center={0,0,0};a.meshHintM=.04;
    AntennaSurface::SurfaceSpec b=a;b.center={0,0,h};
    auto am=AntennaSurface::triangulate(a,72),bm=AntennaSurface::triangulate(b,72);
    for(auto&t:am)in.triangles.push_back({t.a,t.b,t.c,0});
    for(auto&t:bm)in.triangles.push_back({t.a,t.b,t.c,1});
    HybridWireSurfaceMom::DifferentialSurfaceFeed f;f.name="P";f.positivePositionM={0,0,h};f.negativePositionM={0,0,0};
    f.positiveSurfaceIndex=1;f.negativeSurfaceIndex=0;f.voltageV={voltage,0};f.mappingToleranceM=.03;f.footprintRadiusM=.05;
    in.differentialSurfaceFeeds.push_back(f);in.maxSurfaceUnknowns=350;in.maxTotalUnknowns=400;in.enableTerminalHalfRwg=false;in.useEffectiveDielectricRegions=false;
    return in;
}

int main(){
    using namespace AntennaValidation;
    const auto patch=runCase(CaseKind::RectangularPatch,300e6);
    if(patch.rows.size()!=2||!patch.rows[1].valid) return 2;
    const auto &surf=patch.rows[1];
    if(!(surf.meshDeltaPercent<8.0)) return 3;
    if(!(surf.primaryMetricValue>=1.10&&surf.primaryMetricValue<=1.15+1e-9)) return 4;
    if(surf.verdict==Verdict::Pass) return 5;
    if(!(surf.inputImpedanceOhm.real()>2.0&&surf.inputImpedanceOhm.real()<30.0&&surf.inputImpedanceOhm.imag()<-10.0&&surf.inputImpedanceOhm.imag()>-100.0)) return 6;

    const auto a=HybridWireSurfaceMom::solve(twoPlate(1.0));
    const auto b=HybridWireSurfaceMom::solve(twoPlate(2.0));
    if(!a.valid||!b.valid||a.feeds.empty()||b.feeds.empty()) return 7;
    if(!a.differentialPortSymmetricVoltageSplitUsed||!b.differentialPortSymmetricVoltageSplitUsed) return 8;
    if(std::abs(a.maxDifferentialPortFootprintRadiusM-.05)>1e-12||std::abs(b.maxDifferentialPortFootprintRadiusM-.05)>1e-12) return 9;
    // A linear lumped-port solve must have voltage-independent Zin and quadratic accepted power.
    if(std::abs(a.feeds.front().inputImpedanceOhm-b.feeds.front().inputImpedanceOhm)>1e-8*std::max(1.0,std::abs(a.feeds.front().inputImpedanceOhm))) return 10;
    if(a.acceptedPowerW>1e-15){const double ratio=b.acceptedPowerW/a.acceptedPowerW;if(std::abs(ratio-4.0)>1e-7) return 11;}

    const auto mono=runCase(CaseKind::QuarterWaveMonopole,300e6);
    if(mono.rows.size()!=2||!mono.rows[1].valid) return 12;
    const auto zm=mono.rows[1].inputImpedanceOhm;
    if(!(zm.real()>45.0&&zm.real()<80.0&&zm.imag()>20.0&&zm.imag()<60.0)) return 13;

    std::cout<<"5.24 patch Gaussian port="<<surf.inputImpedanceOhm<<" dZ="<<surf.meshDeltaPercent
             <<"% fminX/f0="<<surf.primaryMetricValue<<" verdict="<<verdictName(surf.verdict)<<"\n"
             <<"5.24 two-plate Zin="<<a.feeds.front().inputImpedanceOhm<<" sigma="<<a.maxDifferentialPortFootprintRadiusM<<" m\n"
             <<"5.24 monopole rooftop="<<zm<<"\n";
    return 0;
}
'''

with tempfile.TemporaryDirectory() as td:
    td=Path(td);cpp=td/'test5240.cpp';exe=td/'test5240';cpp.write_text(program,encoding='utf-8')
    cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-I'+str(ROOT/'include'),
         str(ROOT/'src/antenna_validation_bench.cpp'),str(ROOT/'src/numerical_em_solvers.cpp'),
         str(ROOT/'src/antenna_surface_geometry.cpp'),str(ROOT/'src/pec_surface_mom.cpp'),
         str(ROOT/'src/hybrid_wire_surface_mom.cpp'),str(ROOT/'src/microstrip_models.cpp'),str(cpp),'-o',str(exe)]
    subprocess.run(cmd,check=True)
    subprocess.run([str(exe)],check=True,timeout=420)

for src in ['src/pec_surface_mom.cpp','src/hybrid_wire_surface_mom.cpp','src/antenna_validation_bench.cpp']:
    with tempfile.TemporaryDirectory() as td:
        obj=Path(td)/'x.o'
        subprocess.run(['g++','-std=c++17','-O0','-Wall','-Wextra','-Wpedantic','-Werror','-I'+str(ROOT/'include'),'-c',str(ROOT/src),'-o',str(obj)],check=True)

base=Path('/mnt/data/QTsignalApp_5.19.1/QTsignalApp')
if base.exists():
    for rel in ['src/electrostatic_model.cpp','include/electrostatic_model.h','src/magnetostatic_model.cpp','include/magnetostatic_model.h']:
        if hashlib.sha256((ROOT/rel).read_bytes()).digest()!=hashlib.sha256((base/rel).read_bytes()).digest():
            raise SystemExit('QTsignalApp 5.24.0 verification FAILED: static-field regression in '+rel)

def strip_cpp(t):
    t=re.sub(r'/\*.*?\*/','',t,flags=re.S);t=re.sub(r'//[^\n]*','',t)
    t=re.sub(r'R"[^\(]*\(.*?\)[^"]*"','""',t,flags=re.S)
    t=re.sub(r'"(?:\\.|[^"\\])*"','""',t);t=re.sub(r"'(?:\\.|[^'\\])*'","''",t)
    return t
baseline=Path('/mnt/data/QTsignalApp_5.23.0/QTsignalApp')
for rel in ['src/antenna_designer_widget.cpp','src/electromagnetism_reference_widget.cpp']:
    t=strip_cpp((ROOT/rel).read_text(encoding='utf-8'));tb=strip_cpp((baseline/rel).read_text(encoding='utf-8'))
    for op,cl in [('(',')'),('[',']'),('{','}')]:
        if (t.count(op)-t.count(cl))!=(tb.count(op)-tb.count(cl)):
            raise SystemExit(f'QTsignalApp 5.24.0 verification FAILED: lexical regression {rel} {op}{cl}')

print('QTsignalApp 5.24.0 differential RWG port verification: PASS')
