from pathlib import Path
import json, re, subprocess, tempfile, hashlib, xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
BASE = Path('/mnt/data/qtsignal_528')
project = json.loads((ROOT/'QTsignalApp.qtproject.json').read_text(encoding='utf-8'))
main = (ROOT/'src/main.cpp').read_text(encoding='utf-8')
hybrid = (ROOT/'src/hybrid_wire_surface_mom.cpp').read_text(encoding='utf-8')
hybrid_h = (ROOT/'include/hybrid_wire_surface_mom.h').read_text(encoding='utf-8')
designer = (ROOT/'src/antenna_designer_widget.cpp').read_text(encoding='utf-8')
refs = (ROOT/'src/electromagnetism_reference_widget.cpp').read_text(encoding='utf-8')
bench = (ROOT/'src/antenna_validation_bench.cpp').read_text(encoding='utf-8')
change = (ROOT/'CHANGELOG_APP.md').read_text(encoding='utf-8')

checks = {
    'version': project.get('version') == '5.29.0' and project.get('packaging',{}).get('productVersion') == '5.29.0' and 'QStringLiteral("5.29.0")' in main,
    'api': all(x in hybrid_h for x in [
        'LayeredSlabSommerfeldTeVector = 4', 'layeredSlabSommerfeldTeVectorUsed',
        'layeredSommerfeldVectorPotentialUsed', 'layeredSommerfeldHedScalarDiagnosticEvaluated',
        'layeredSommerfeldFullComplexScalarUsed', 'layeredSommerfeldReactiveProjectionUsed']),
    'te spectrum': all(x in hybrid for x in ['teReflection', 'teTransmission', 'rTe=(g1-g2)/denTeInterface', 'sameFaceVectorLongitudinal', 'surfaceVectorGreenDot']),
    'hed scalar diagnostic': all(x in hybrid for x in ['sameFaceHedReflectedOverEpsilon', 'hedScalarReflection', 'sommerfeldHedSameFaceScalarGreenOverEpsilon']),
    'passivity guard': all(x in hybrid for x in ['full-complex injection is not yet released', 'reactive scalar projection', '(spectralFull-quasiStaticFull).real()']),
    'ui': all(x in designer for x in ['Sommerfeld TE vector + guarded scalar (5.29)', 'DielectricKernelModel::LayeredSlabSommerfeldTeVector', '5.29 HED transition']) and designer.count('m_hybridDielectricKernel->setCurrentIndex(4)') >= 2,
    'benchmark': 'DielectricKernelModel::LayeredSlabSommerfeldTeVector' in bench and '5.29 passivity-guarded HED transition' in bench,
    'refs': all(x in refs for x in ['Passivity-guarded HED TE vector-potential transition (5.29)', 'G_A,t^ref', 'R_TE+R_q', '5.29 production matrix is deliberately passivity-guarded']),
    'changelog': change.startswith('# QTsignalApp 5.29.0') and 'Passivity-guarded Sommerfeld HED TE-vector transition' in change and 'approximately `5.18%`' in change,
    'qta': 'root.insert(QStringLiteral("version"), 19)' in designer,
}
failed = [k for k,v in checks.items() if not v]
if failed:
    raise SystemExit('5.29 static verification FAILED: ' + ', '.join(failed))

program = r'''
#include "hybrid_wire_surface_mom.h"
#include "antenna_surface_geometry.h"
#include "microstrip_models.h"
#include "antenna_validation_bench.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>

namespace {
constexpr double C0=299792458.0;
struct PG{double f0=0,h=0,er=4.2,ee=1,w=0,l=0,gw=0,gl=0,fy=0;};
PG pg(double f){PG g;g.f0=f;double la=C0/f;g.h=std::min(.02*la,.0016);auto e=MicrostripModels::rectangularPatch(f,g.h,g.er);if(e.valid){g.w=e.widthM;g.l=e.physicalLengthM;g.ee=e.effectivePermittivity;}g.gw=std::max(1.35*g.w,g.w+12*g.h);g.gl=std::max(1.35*g.l,g.l+12*g.h);g.fy=-.22*g.l;return g;}
HybridWireSurfaceMom::Input patch(const PG&g,double f,bool fine,HybridWireSurfaceMom::DielectricKernelModel model){HybridWireSurfaceMom::Input in;double la=C0/g.f0;in.wire.frequencyHz=f;in.wire.computeFarField=false;in.computeFarField=false;AntennaSurface::SurfaceSpec a;a.kind=AntennaSurface::SurfaceKind::Rectangle;a.center={0,0,0};a.widthM=g.gw;a.heightM=g.gl;a.meshHintM=fine?la/18:la/12;AntennaSurface::SurfaceSpec b;b.kind=AntennaSurface::SurfaceKind::Rectangle;b.center={0,0,g.h};b.widthM=g.w;b.heightM=g.l;b.meshHintM=fine?la/25:la/16;auto am=AntennaSurface::triangulate(a,fine?96:64),bm=AntennaSurface::triangulate(b,fine?96:64);for(auto&t:am)in.triangles.push_back({t.a,t.b,t.c,0});for(auto&t:bm)in.triangles.push_back({t.a,t.b,t.c,1});HybridWireSurfaceMom::DifferentialSurfaceFeed sf;sf.name="P";sf.positivePositionM={0,g.fy,g.h};sf.negativePositionM={0,g.fy,0};sf.voltageV={1,0};sf.referenceOhm=50;sf.positiveSurfaceIndex=1;sf.negativeSurfaceIndex=0;sf.mappingToleranceM=std::max(g.w,g.l)/3;sf.footprintRadiusM=.08*la;in.differentialSurfaceFeeds.push_back(sf);HybridWireSurfaceMom::DielectricRegion d;d.name="D";d.centerM={0,0,.5*g.h};d.widthM=g.gw;d.heightM=g.gl;d.thicknessM=g.h;d.relativePermittivity=g.er;d.lossTangent=.02;d.fieldFillFactor=g.er>1?std::clamp((g.ee-1)/(g.er-1),0.,1.):0;in.dielectrics.push_back(d);in.maxTotalUnknowns=520;in.maxSurfaceUnknowns=420;in.enableTerminalHalfRwg=false;in.useEffectiveDielectricRegions=true;in.dielectricKernelModel=model;return in;}
HybridWireSurfaceMom::Input plates(double f,double v,HybridWireSurfaceMom::DielectricKernelModel model){HybridWireSurfaceMom::Input in;in.wire.frequencyHz=f;in.wire.computeFarField=false;in.computeFarField=false;double s=.12,h=.01;AntennaSurface::SurfaceSpec a;a.kind=AntennaSurface::SurfaceKind::Rectangle;a.widthM=2*s;a.heightM=2*s;a.center={0,0,0};a.meshHintM=.04;auto b=a;b.center={0,0,h};auto am=AntennaSurface::triangulate(a,72),bm=AntennaSurface::triangulate(b,72);for(auto&t:am)in.triangles.push_back({t.a,t.b,t.c,0});for(auto&t:bm)in.triangles.push_back({t.a,t.b,t.c,1});HybridWireSurfaceMom::DifferentialSurfaceFeed p;p.name="P";p.positivePositionM={0,0,h};p.negativePositionM={0,0,0};p.positiveSurfaceIndex=1;p.negativeSurfaceIndex=0;p.voltageV={v,0};p.mappingToleranceM=.03;p.footprintRadiusM=.05;in.differentialSurfaceFeeds.push_back(p);HybridWireSurfaceMom::DielectricRegion d;d.name="D";d.centerM={0,0,h/2};d.widthM=2*s;d.heightM=2*s;d.thicknessM=h;d.relativePermittivity=3;d.lossTangent=.01;d.fieldFillFactor=.65;in.dielectrics.push_back(d);in.maxSurfaceUnknowns=350;in.maxTotalUnknowns=400;in.enableTerminalHalfRwg=false;in.useEffectiveDielectricRegions=true;in.dielectricKernelModel=model;return in;}
double rel(std::complex<double>a,std::complex<double>b){return 100*std::abs(a-b)/std::max(std::abs(b),1e-30);}
}

int main(){
    using M=HybridWireSurfaceMom::DielectricKernelModel;
    auto g=pg(300e6);
    auto c=HybridWireSurfaceMom::solve(patch(g,300e6,false,M::LayeredSlabSommerfeldTeVector));
    auto f=HybridWireSurfaceMom::solve(patch(g,300e6,true,M::LayeredSlabSommerfeldTeVector));
    if(!c.valid||!f.valid||c.feeds.empty()||f.feeds.empty()) return 2;
    auto zc=c.feeds[0].inputImpedanceOhm,zf=f.feeds[0].inputImpedanceOhm; double dz=rel(zc,zf);
    if(!(zf.real()>6&&zf.real()<15&&zf.imag()>-60&&zf.imag()<-25&&dz>3&&dz<8)) return 3;
    if(!f.layeredSlabSommerfeldScalarUsed||!f.layeredSlabSommerfeldTeVectorUsed||!f.layeredSommerfeldVectorPotentialUsed||!f.layeredSommerfeldHedScalarDiagnosticEvaluated||f.layeredSommerfeldFullComplexScalarUsed||!f.layeredSommerfeldReactiveProjectionUsed) return 4;
    if(f.acceptedPowerW<=0.0) return 5;

    auto p1=HybridWireSurfaceMom::solve(plates(30e6,1,M::LayeredSlabSommerfeldTeVector));
    auto p2=HybridWireSurfaceMom::solve(plates(30e6,2,M::LayeredSlabSommerfeldTeVector));
    auto p300=HybridWireSurfaceMom::solve(plates(300e6,1,M::LayeredSlabSommerfeldTeVector));
    auto p300old=HybridWireSurfaceMom::solve(plates(300e6,1,M::LayeredSlabSommerfeldScalar));
    if(!p1.valid||!p2.valid||!p300.valid||!p300old.valid||p1.feeds.empty()||p2.feeds.empty()||p300.feeds.empty()||p300old.feeds.empty()) return 6;
    if(p1.feeds[0].inputImpedanceOhm.real()<=0.0||p300.feeds[0].inputImpedanceOhm.real()<=0.0||p1.acceptedPowerW<=0.0||p300.acceptedPowerW<=0.0) return 7;
    if(std::abs(p1.feeds[0].inputImpedanceOhm-p2.feeds[0].inputImpedanceOhm)>1e-8*std::max(1.,std::abs(p1.feeds[0].inputImpedanceOhm))) return 8;
    if(p1.acceptedPowerW>1e-15 && std::abs(p2.acceptedPowerW/p1.acceptedPowerW-4.0)>1e-7) return 9;
    if(std::abs(p300.feeds[0].inputImpedanceOhm.imag()-p300old.feeds[0].inputImpedanceOhm.imag())<0.2) return 10;

    auto r115=HybridWireSurfaceMom::solve(patch(g,345e6,false,M::LayeredSlabSommerfeldTeVector));
    if(!r115.valid||r115.feeds.empty()||std::abs(r115.feeds[0].inputImpedanceOhm.imag())>=std::abs(c.feeds[0].inputImpedanceOhm.imag())) return 11;

    auto mono=AntennaValidation::runCase(AntennaValidation::CaseKind::QuarterWaveMonopole,300e6);
    if(mono.rows.size()!=2||!mono.rows[1].valid) return 12;
    auto zm=mono.rows[1].inputImpedanceOhm;
    if(!(zm.real()>45&&zm.real()<80&&zm.imag()>20&&zm.imag()<60)) return 13;

    std::cout<<"5.29 patch TE-vector="<<zf<<" dZ="<<dz<<"% coarse="<<zc<<" 1.15f0="<<r115.feeds[0].inputImpedanceOhm<<"\n";
    std::cout<<"5.29 two-plate 30MHz="<<p1.feeds[0].inputImpedanceOhm<<" 300MHz="<<p300.feeds[0].inputImpedanceOhm<<" old5.26="<<p300old.feeds[0].inputImpedanceOhm<<"\n";
    std::cout<<"5.28 monopole regression="<<zm<<"\n";
    return 0;
}
'''

with tempfile.TemporaryDirectory() as td:
    td=Path(td); cpp=td/'t.cpp'; exe=td/'t'; cpp.write_text(program)
    cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-I'+str(ROOT/'include'),
         str(ROOT/'src/antenna_validation_bench.cpp'),str(ROOT/'src/numerical_em_solvers.cpp'),
         str(ROOT/'src/antenna_surface_geometry.cpp'),str(ROOT/'src/pec_surface_mom.cpp'),
         str(ROOT/'src/hybrid_wire_surface_mom.cpp'),str(ROOT/'src/microstrip_models.cpp'),str(cpp),'-o',str(exe)]
    subprocess.run(cmd,check=True)
    subprocess.run([str(exe)],check=True,timeout=420)

for src in ['src/hybrid_wire_surface_mom.cpp','src/antenna_validation_bench.cpp','src/pec_surface_mom.cpp']:
    with tempfile.TemporaryDirectory() as td:
        subprocess.run(['g++','-std=c++17','-O0','-Wall','-Wextra','-Wpedantic','-Werror','-I'+str(ROOT/'include'),'-c',str(ROOT/src),'-o',str(Path(td)/'x.o')],check=True)

# Static-field solvers must not move during this antenna-kernel step.
for rel in ['src/electrostatic_model.cpp','include/electrostatic_model.h','src/magnetostatic_model.cpp','include/magnetostatic_model.h']:
    if hashlib.sha256((ROOT/rel).read_bytes()).digest()!=hashlib.sha256((BASE/rel).read_bytes()).digest():
        raise SystemExit('static-field regression '+rel)

# Qt is not linked in this verification container; compare lexical balance with the 5.28 tree.
def strip_cpp(t):
    t=re.sub(r'/\*.*?\*/','',t,flags=re.S); t=re.sub(r'//[^\n]*','',t)
    t=re.sub(r'R"[^\(]*\(.*?\)[^"]*"','""',t,flags=re.S)
    t=re.sub(r'"(?:\\.|[^"\\])*"','""',t); t=re.sub(r"'(?:\\.|[^'\\])*'","''",t)
    return t
for rel in ['src/antenna_designer_widget.cpp','src/electromagnetism_reference_widget.cpp']:
    t=strip_cpp((ROOT/rel).read_text()); b=strip_cpp((BASE/rel).read_text())
    for op,cl in [('(',')'),('[',']'),('{','}')]:
        if (t.count(op)-t.count(cl)) != (b.count(op)-b.count(cl)):
            raise SystemExit(f'lexical regression {rel} {op}{cl}')
for ui in ROOT.rglob('*.ui'): ET.parse(ui)
print('QTsignalApp 5.29.0 passivity-guarded HED TE-vector verification: PASS')
