from pathlib import Path
import json, subprocess, tempfile, xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
project = json.loads((ROOT/'QTsignalApp.qtproject.json').read_text(encoding='utf-8'))
main = (ROOT/'src/main.cpp').read_text(encoding='utf-8')
hybrid = (ROOT/'src/hybrid_wire_surface_mom.cpp').read_text(encoding='utf-8')
hybrid_h = (ROOT/'include/hybrid_wire_surface_mom.h').read_text(encoding='utf-8')
designer = (ROOT/'src/antenna_designer_widget.cpp').read_text(encoding='utf-8')
refs = (ROOT/'src/electromagnetism_reference_widget.cpp').read_text(encoding='utf-8')
bench = (ROOT/'src/antenna_validation_bench.cpp').read_text(encoding='utf-8')
change = (ROOT/'CHANGELOG_APP.md').read_text(encoding='utf-8')

checks = {
    'version': project.get('version') == '5.31.0' and project.get('packaging',{}).get('productVersion') == '5.31.0' and 'QStringLiteral("5.31.0")' in main,
    'api': all(x in hybrid_h for x in [
        'LayeredSlabSommerfeldLongitudinalHed = 6',
        'layeredSlabSommerfeldLongitudinalHedUsed',
        'layeredSommerfeldLongitudinalHedScalarUsed',
        'layeredSommerfeldCrossFaceHedScalarUsed']),
    'hed scalar': all(x in hybrid for x in [
        'crossFaceHedLayeredOverEpsilon', 'hedScalarTransmission',
        'sommerfeldHedScalarGreenOverEpsilon', 'longitudinalHed']),
    'reactive guard': all(x in hybrid for x in [
        'layeredSommerfeldFullComplexScalarUsed=false',
        '(spectralFull-quasiStaticFull).real()']),
    'ui': all(x in designer for x in [
        'Sommerfeld HED longitudinal scalar + transmitted dyadic (5.31)',
        'DielectricKernelModel::LayeredSlabSommerfeldLongitudinalHed',
        '5.31 HED longitudinal scalar']) and designer.count('m_hybridDielectricKernel->setCurrentIndex(6)') >= 2,
    'benchmark': 'DielectricKernelModel::LayeredSlabSommerfeldLongitudinalHed' in bench and 'Version 5.31' in bench,
    'refs': all(x in refs for x in [
        'Coupled HED longitudinal scalar reflection / transmission (5.31)',
        'H_trans = T_TE', '5.31 scalar activation remains passivity-guarded']),
    'changelog': change.startswith('# QTsignalApp 5.31.0') and 'Coupled HED longitudinal scalar reflection / transmission' in change and 'approximately `4.35%`' in change,
    'qta': 'root.insert(QStringLiteral("version"), 19)' in designer,
}
failed = [k for k,v in checks.items() if not v]
if failed:
    raise SystemExit('5.31 static verification FAILED: ' + ', '.join(failed))

for ui in (ROOT/'forms').glob('*.ui'):
    ET.parse(ui)

program = r'''
#include "hybrid_wire_surface_mom.h"
#include "antenna_surface_geometry.h"
#include "microstrip_models.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>
namespace {
constexpr double C0=299792458.0;
struct PG{double f0=0,h=0,er=4.2,ee=1,w=0,l=0,gw=0,gl=0,fy=0;};
PG pg(double f){PG g;g.f0=f;double la=C0/f;g.h=std::min(.02*la,.0016);auto e=MicrostripModels::rectangularPatch(f,g.h,g.er);if(e.valid){g.w=e.widthM;g.l=e.physicalLengthM;g.ee=e.effectivePermittivity;}g.gw=std::max(1.35*g.w,g.w+12*g.h);g.gl=std::max(1.35*g.l,g.l+12*g.h);g.fy=-.22*g.l;return g;}
HybridWireSurfaceMom::Input patch(const PG&g,bool fine,HybridWireSurfaceMom::DielectricKernelModel model){HybridWireSurfaceMom::Input in;double la=C0/g.f0;in.wire.frequencyHz=g.f0;in.wire.computeFarField=false;in.computeFarField=false;AntennaSurface::SurfaceSpec a;a.kind=AntennaSurface::SurfaceKind::Rectangle;a.center={0,0,0};a.widthM=g.gw;a.heightM=g.gl;a.meshHintM=fine?la/18:la/12;AntennaSurface::SurfaceSpec b;b.kind=AntennaSurface::SurfaceKind::Rectangle;b.center={0,0,g.h};b.widthM=g.w;b.heightM=g.l;b.meshHintM=fine?la/25:la/16;auto am=AntennaSurface::triangulate(a,fine?96:64),bm=AntennaSurface::triangulate(b,fine?96:64);for(auto&t:am)in.triangles.push_back({t.a,t.b,t.c,0});for(auto&t:bm)in.triangles.push_back({t.a,t.b,t.c,1});HybridWireSurfaceMom::DifferentialSurfaceFeed sf;sf.name="P";sf.positivePositionM={0,g.fy,g.h};sf.negativePositionM={0,g.fy,0};sf.voltageV={1,0};sf.referenceOhm=50;sf.positiveSurfaceIndex=1;sf.negativeSurfaceIndex=0;sf.mappingToleranceM=std::max(g.w,g.l)/3;sf.footprintRadiusM=.08*la;in.differentialSurfaceFeeds.push_back(sf);HybridWireSurfaceMom::DielectricRegion d;d.name="D";d.centerM={0,0,.5*g.h};d.widthM=g.gw;d.heightM=g.gl;d.thicknessM=g.h;d.relativePermittivity=g.er;d.lossTangent=.02;d.fieldFillFactor=g.er>1?std::clamp((g.ee-1)/(g.er-1),0.,1.):0;in.dielectrics.push_back(d);in.maxTotalUnknowns=520;in.maxSurfaceUnknowns=420;in.enableTerminalHalfRwg=false;in.useEffectiveDielectricRegions=true;in.dielectricKernelModel=model;return in;}
HybridWireSurfaceMom::Input plates(double f,double v,HybridWireSurfaceMom::DielectricKernelModel model){HybridWireSurfaceMom::Input in;in.wire.frequencyHz=f;in.wire.computeFarField=false;in.computeFarField=false;double s=.12,h=.01;AntennaSurface::SurfaceSpec a;a.kind=AntennaSurface::SurfaceKind::Rectangle;a.widthM=2*s;a.heightM=2*s;a.center={0,0,0};a.meshHintM=.04;auto b=a;b.center={0,0,h};auto am=AntennaSurface::triangulate(a,72),bm=AntennaSurface::triangulate(b,72);for(auto&t:am)in.triangles.push_back({t.a,t.b,t.c,0});for(auto&t:bm)in.triangles.push_back({t.a,t.b,t.c,1});HybridWireSurfaceMom::DifferentialSurfaceFeed p;p.name="P";p.positivePositionM={0,0,h};p.negativePositionM={0,0,0};p.positiveSurfaceIndex=1;p.negativeSurfaceIndex=0;p.voltageV={v,0};p.mappingToleranceM=.03;p.footprintRadiusM=.05;in.differentialSurfaceFeeds.push_back(p);HybridWireSurfaceMom::DielectricRegion d;d.name="D";d.centerM={0,0,h/2};d.widthM=2*s;d.heightM=2*s;d.thicknessM=h;d.relativePermittivity=3;d.lossTangent=.01;d.fieldFillFactor=.65;in.dielectrics.push_back(d);in.maxSurfaceUnknowns=350;in.maxTotalUnknowns=400;in.enableTerminalHalfRwg=false;in.useEffectiveDielectricRegions=true;in.dielectricKernelModel=model;return in;}
double rel(std::complex<double>a,std::complex<double>b){return 100*std::abs(a-b)/std::max(std::abs(b),1e-30);}
}
int main(){using M=HybridWireSurfaceMom::DielectricKernelModel;auto g=pg(300e6);auto c=HybridWireSurfaceMom::solve(patch(g,false,M::LayeredSlabSommerfeldLongitudinalHed));auto f=HybridWireSurfaceMom::solve(patch(g,true,M::LayeredSlabSommerfeldLongitudinalHed));auto old=HybridWireSurfaceMom::solve(patch(g,true,M::LayeredSlabSommerfeldCrossFace));if(!c.valid||!f.valid||!old.valid||c.feeds.empty()||f.feeds.empty()||old.feeds.empty())return 2;auto zc=c.feeds[0].inputImpedanceOhm,zf=f.feeds[0].inputImpedanceOhm,zo=old.feeds[0].inputImpedanceOhm;double dz=rel(zc,zf);if(!(zf.real()>6&&zf.real()<15&&zf.imag()>-60&&zf.imag()<-25&&dz>3&&dz<8))return 3;if(!f.layeredSlabSommerfeldLongitudinalHedUsed||!f.layeredSommerfeldLongitudinalHedScalarUsed||!f.layeredSommerfeldCrossFaceHedScalarUsed||f.layeredSommerfeldFullComplexScalarUsed||!f.layeredSommerfeldReactiveProjectionUsed)return 4;if(f.acceptedPowerW<=0.0||std::abs(zf-zo)<0.005||std::abs(zf-zo)>2.0)return 5;auto p1=HybridWireSurfaceMom::solve(plates(30e6,1,M::LayeredSlabSommerfeldLongitudinalHed));auto p2=HybridWireSurfaceMom::solve(plates(30e6,2,M::LayeredSlabSommerfeldLongitudinalHed));auto p300=HybridWireSurfaceMom::solve(plates(300e6,1,M::LayeredSlabSommerfeldLongitudinalHed));auto p600=HybridWireSurfaceMom::solve(plates(600e6,1,M::LayeredSlabSommerfeldLongitudinalHed));if(!p1.valid||!p2.valid||!p300.valid||!p600.valid||p1.feeds.empty()||p2.feeds.empty()||p300.feeds.empty()||p600.feeds.empty())return 6;if(p1.feeds[0].inputImpedanceOhm.real()<=0||p300.feeds[0].inputImpedanceOhm.real()<=0||p600.feeds[0].inputImpedanceOhm.real()<=0||p1.acceptedPowerW<=0||p300.acceptedPowerW<=0||p600.acceptedPowerW<=0)return 7;if(std::abs(p1.feeds[0].inputImpedanceOhm-p2.feeds[0].inputImpedanceOhm)>1e-8*std::max(1.,std::abs(p1.feeds[0].inputImpedanceOhm)))return 8;if(p1.acceptedPowerW>1e-15&&std::abs(p2.acceptedPowerW/p1.acceptedPowerW-4.0)>1e-7)return 9;if(f.surfaceReciprocityRelative>1e-10)return 10;std::cout<<"5.31 patch="<<zf<<" old5.30="<<zo<<" dZ="<<dz<<"%\n";std::cout<<"5.31 plates 30/300/600="<<p1.feeds[0].inputImpedanceOhm<<" / "<<p300.feeds[0].inputImpedanceOhm<<" / "<<p600.feeds[0].inputImpedanceOhm<<"\n";return 0;}
'''

with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    cpp = td/'verify531.cpp'; exe = td/'verify531'
    cpp.write_text(program, encoding='utf-8')
    cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-I'+str(ROOT/'include'),
         str(ROOT/'src/numerical_em_solvers.cpp'),str(ROOT/'src/pec_surface_mom.cpp'),str(ROOT/'src/antenna_surface_geometry.cpp'),
         str(ROOT/'src/hybrid_wire_surface_mom.cpp'),str(ROOT/'src/microstrip_models.cpp'),str(cpp),'-o',str(exe)]
    subprocess.run(cmd,check=True)
    subprocess.run([str(exe)],check=True,timeout=420)

print('QTsignalApp 5.31.0 HED-longitudinal verification: PASS')
