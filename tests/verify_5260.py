from pathlib import Path
import json, re, subprocess, tempfile, hashlib, xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[1]
BASE=Path('/mnt/data/qtsignal_525/QTsignalApp')
project=json.loads((ROOT/'QTsignalApp.qtproject.json').read_text(encoding='utf-8'))
main=(ROOT/'src/main.cpp').read_text(encoding='utf-8')
hybrid=(ROOT/'src/hybrid_wire_surface_mom.cpp').read_text(encoding='utf-8')
hybrid_h=(ROOT/'include/hybrid_wire_surface_mom.h').read_text(encoding='utf-8')
designer=(ROOT/'src/antenna_designer_widget.cpp').read_text(encoding='utf-8')
refs=(ROOT/'src/electromagnetism_reference_widget.cpp').read_text(encoding='utf-8')
bench=(ROOT/'src/antenna_validation_bench.cpp').read_text(encoding='utf-8')
change=(ROOT/'CHANGELOG_APP.md').read_text(encoding='utf-8')
checks={
 'version': project.get('version')=='5.26.0' and project.get('packaging',{}).get('productVersion')=='5.26.0' and 'QStringLiteral("5.26.0")' in main,
 'api': all(x in hybrid_h for x in ['LayeredSlabSommerfeldScalar = 3','layeredSlabSommerfeldScalarUsed','layeredSommerfeldReactiveProjectionUsed','layeredSommerfeldRadialSamples','layeredSommerfeldQuadratureOrder']),
 'spectrum': all(x in hybrid for x in ['SommerfeldRadialSampleCount=80','SommerfeldQuadratureOrder=16','SommerfeldLimitingLossTangent=1e-5','sommerfeldSpectrum','std::cyl_bessel_j','reflectionInfinity','outgoingVerticalGamma']),
 'finite slab': all(x in hybrid for x in ['r*(C{1.0,0.0}-roundTrip)/den','(C{1.0,0.0}-r*r)*std::exp(-g2*h)/den']),
 'reactive guard': 'spectralFull-quasiStaticFull).real()' in hybrid and 'passivity' in hybrid,
 'ui': all(x in designer for x in ['Sommerfeld TM scalar (dynamic, 5.26)','DielectricKernelModel::LayeredSlabSommerfeldScalar','Sommerfeld TM scalar correction: %1 slab region(s)']) and designer.count('m_hybridDielectricKernel->setCurrentIndex(3)')>=2,
 'benchmark': 'DielectricKernelModel::LayeredSlabSommerfeldScalar' in bench and '5.26 k_rho-dependent Sommerfeld TM scalar correction' in bench and 'LayeredSlabQuasiStatic' in bench,
 'refs': all(x in refs for x in ['Dynamic Sommerfeld TM scalar correction (5.26)','gamma_i(k_rho)','inverse-transforms it with a Bessel/Hankel Sommerfeld integral','5.26 is not yet the complete layered-media MPIE']),
 'changelog': change.startswith('# QTsignalApp 5.26.0') and 'Dynamic Sommerfeld TM scalar transition' in change and 'approximately `5.13%`' in change,
 'qta': 'root.insert(QStringLiteral("version"), 19)' in designer,
 '524 port retained': all(x in hybrid for x in ['+=0.5*t.integratedDivergenceM','-=0.5*t.integratedDivergenceM','ip=-0.5*(ipos-ineg)']),
}
failed=[k for k,v in checks.items() if not v]
if failed: raise SystemExit('5.26 static verification FAILED: '+', '.join(failed))

program=r'''
#include "antenna_validation_bench.h"
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
HybridWireSurfaceMom::Input patch(const PG&g,double f,bool fine,HybridWireSurfaceMom::DielectricKernelModel model){HybridWireSurfaceMom::Input in;double la=C0/g.f0;in.wire.frequencyHz=f;in.wire.computeFarField=false;in.computeFarField=false;AntennaSurface::SurfaceSpec a;a.kind=AntennaSurface::SurfaceKind::Rectangle;a.center={0,0,0};a.widthM=g.gw;a.heightM=g.gl;a.meshHintM=fine?la/18:la/12;AntennaSurface::SurfaceSpec b;b.kind=AntennaSurface::SurfaceKind::Rectangle;b.center={0,0,g.h};b.widthM=g.w;b.heightM=g.l;b.meshHintM=fine?la/25:la/16;auto am=AntennaSurface::triangulate(a,fine?96:64),bm=AntennaSurface::triangulate(b,fine?96:64);for(auto&t:am)in.triangles.push_back({t.a,t.b,t.c,0});for(auto&t:bm)in.triangles.push_back({t.a,t.b,t.c,1});HybridWireSurfaceMom::DifferentialSurfaceFeed sf;sf.name="P";sf.positivePositionM={0,g.fy,g.h};sf.negativePositionM={0,g.fy,0};sf.voltageV={1,0};sf.referenceOhm=50;sf.positiveSurfaceIndex=1;sf.negativeSurfaceIndex=0;sf.mappingToleranceM=std::max(g.w,g.l)/3;sf.footprintRadiusM=.08*la;in.differentialSurfaceFeeds.push_back(sf);HybridWireSurfaceMom::DielectricRegion d;d.name="D";d.centerM={0,0,.5*g.h};d.widthM=g.gw;d.heightM=g.gl;d.thicknessM=g.h;d.relativePermittivity=g.er;d.lossTangent=.02;d.fieldFillFactor=g.er>1?std::clamp((g.ee-1)/(g.er-1),0.,1.):0;in.dielectrics.push_back(d);in.maxTotalUnknowns=520;in.maxSurfaceUnknowns=420;in.enableTerminalHalfRwg=false;in.useEffectiveDielectricRegions=true;in.dielectricKernelModel=model;return in;}
HybridWireSurfaceMom::Input plates(double f,double v,HybridWireSurfaceMom::DielectricKernelModel model){HybridWireSurfaceMom::Input in;in.wire.frequencyHz=f;in.wire.computeFarField=false;in.computeFarField=false;double s=.12,h=.01;AntennaSurface::SurfaceSpec a;a.kind=AntennaSurface::SurfaceKind::Rectangle;a.widthM=2*s;a.heightM=2*s;a.center={0,0,0};a.meshHintM=.04;auto b=a;b.center={0,0,h};auto am=AntennaSurface::triangulate(a,72),bm=AntennaSurface::triangulate(b,72);for(auto&t:am)in.triangles.push_back({t.a,t.b,t.c,0});for(auto&t:bm)in.triangles.push_back({t.a,t.b,t.c,1});HybridWireSurfaceMom::DifferentialSurfaceFeed p;p.name="P";p.positivePositionM={0,0,h};p.negativePositionM={0,0,0};p.positiveSurfaceIndex=1;p.negativeSurfaceIndex=0;p.voltageV={v,0};p.mappingToleranceM=.03;p.footprintRadiusM=.05;in.differentialSurfaceFeeds.push_back(p);HybridWireSurfaceMom::DielectricRegion d;d.name="D";d.centerM={0,0,h/2};d.widthM=2*s;d.heightM=2*s;d.thicknessM=h;d.relativePermittivity=3;d.lossTangent=.01;d.fieldFillFactor=.65;in.dielectrics.push_back(d);in.maxSurfaceUnknowns=350;in.maxTotalUnknowns=400;in.enableTerminalHalfRwg=false;in.useEffectiveDielectricRegions=true;in.dielectricKernelModel=model;return in;}
double rel(std::complex<double>a,std::complex<double>b){return 100*std::abs(a-b)/std::max(std::abs(b),1e-30);}
}
int main(){using M=HybridWireSurfaceMom::DielectricKernelModel;auto g=pg(300e6);auto c=HybridWireSurfaceMom::solve(patch(g,300e6,false,M::LayeredSlabSommerfeldScalar));auto f=HybridWireSurfaceMom::solve(patch(g,300e6,true,M::LayeredSlabSommerfeldScalar));if(!c.valid||!f.valid||c.feeds.empty()||f.feeds.empty())return 2;auto zc=c.feeds[0].inputImpedanceOhm,zf=f.feeds[0].inputImpedanceOhm;double dz=rel(zc,zf);if(!(zf.real()>6&&zf.real()<15&&zf.imag()>-60&&zf.imag()<-25&&dz>3&&dz<8))return 3;if(!f.layeredSlabSommerfeldScalarUsed||!f.layeredSommerfeldReactiveProjectionUsed||f.layeredSlabRegionCount!=1||f.layeredSommerfeldRadialSamples!=80||f.layeredSommerfeldQuadratureOrder!=16)return 4;auto fq=HybridWireSurfaceMom::solve(patch(g,300e6,true,M::LayeredSlabQuasiStatic));if(!fq.valid||fq.feeds.empty()||rel(fq.feeds[0].inputImpedanceOhm,{9.20567,-42.6143})>.1)return 5;
auto p1=HybridWireSurfaceMom::solve(plates(30e6,1,M::LayeredSlabSommerfeldScalar));auto p2=HybridWireSurfaceMom::solve(plates(30e6,2,M::LayeredSlabSommerfeldScalar));if(!p1.valid||!p2.valid||p1.feeds.empty()||p2.feeds.empty()||p1.feeds[0].inputImpedanceOhm.real()<=0)return 6;if(std::abs(p1.feeds[0].inputImpedanceOhm-p2.feeds[0].inputImpedanceOhm)>1e-8*std::max(1.,std::abs(p1.feeds[0].inputImpedanceOhm)))return 7;if(p1.acceptedPowerW>1e-15&&std::abs(p2.acceptedPowerW/p1.acceptedPowerW-4)>1e-7)return 8;auto hiq=HybridWireSurfaceMom::solve(plates(300e6,1,M::LayeredSlabQuasiStatic));auto his=HybridWireSurfaceMom::solve(plates(300e6,1,M::LayeredSlabSommerfeldScalar));if(!hiq.valid||!his.valid||his.feeds.empty()||hiq.feeds.empty()||his.feeds[0].inputImpedanceOhm.real()<=0||std::abs(his.feeds[0].inputImpedanceOhm.imag()-hiq.feeds[0].inputImpedanceOhm.imag())<1.)return 9;
double bf=1,bx=1e300;for(double x:{.85,.90,.95,1.,1.05,1.10,1.15}){auto r=HybridWireSurfaceMom::solve(patch(g,300e6*x,false,M::LayeredSlabSommerfeldScalar));if(!r.valid||r.feeds.empty())return 10;double q=std::abs(r.feeds[0].inputImpedanceOhm.imag());if(q<bx){bx=q;bf=x;}}if(bf<1.10)return 11;auto mono=AntennaValidation::runCase(AntennaValidation::CaseKind::QuarterWaveMonopole,300e6);if(mono.rows.size()!=2||!mono.rows[1].valid)return 12;auto zm=mono.rows[1].inputImpedanceOhm;if(!(zm.real()>45&&zm.real()<80&&zm.imag()>20&&zm.imag()<60))return 13;std::cout<<"5.26 patch Sommerfeld scalar="<<zf<<" dZ="<<dz<<"% fminX/f0="<<bf<<" verdict=FAIL\n"<<"5.25 QS regression="<<fq.feeds[0].inputImpedanceOhm<<"\n"<<"5.26 two-plate 30MHz="<<p1.feeds[0].inputImpedanceOhm<<" 300MHz QS/SP="<<hiq.feeds[0].inputImpedanceOhm<<" / "<<his.feeds[0].inputImpedanceOhm<<"\n"<<"5.26 monopole="<<zm<<"\n";return 0;}
'''
with tempfile.TemporaryDirectory() as td:
 td=Path(td); cpp=td/'t.cpp'; exe=td/'t'; cpp.write_text(program)
 cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-I'+str(ROOT/'include'),str(ROOT/'src/antenna_validation_bench.cpp'),str(ROOT/'src/numerical_em_solvers.cpp'),str(ROOT/'src/antenna_surface_geometry.cpp'),str(ROOT/'src/pec_surface_mom.cpp'),str(ROOT/'src/hybrid_wire_surface_mom.cpp'),str(ROOT/'src/microstrip_models.cpp'),str(cpp),'-o',str(exe)]
 subprocess.run(cmd,check=True); subprocess.run([str(exe)],check=True,timeout=420)
for src in ['src/pec_surface_mom.cpp','src/hybrid_wire_surface_mom.cpp','src/antenna_validation_bench.cpp']:
 with tempfile.TemporaryDirectory() as td: subprocess.run(['g++','-std=c++17','-O0','-Wall','-Wextra','-Wpedantic','-Werror','-I'+str(ROOT/'include'),'-c',str(ROOT/src),'-o',str(Path(td)/'x.o')],check=True)
for rel in ['src/electrostatic_model.cpp','include/electrostatic_model.h','src/magnetostatic_model.cpp','include/magnetostatic_model.h']:
 if hashlib.sha256((ROOT/rel).read_bytes()).digest()!=hashlib.sha256((BASE/rel).read_bytes()).digest(): raise SystemExit('static-field regression '+rel)
def strip_cpp(t):
 t=re.sub(r'/\*.*?\*/','',t,flags=re.S);t=re.sub(r'//[^\n]*','',t);t=re.sub(r'R"[^\(]*\(.*?\)[^"]*"','""',t,flags=re.S);t=re.sub(r'"(?:\\.|[^"\\])*"','""',t);t=re.sub(r"'(?:\\.|[^'\\])*'","''",t);return t
for rel in ['src/antenna_designer_widget.cpp','src/electromagnetism_reference_widget.cpp']:
 t,tb=strip_cpp((ROOT/rel).read_text()),strip_cpp((BASE/rel).read_text())
 for op,cl in [('(',')'),('[',']'),('{','}')]:
  if t.count(op)-t.count(cl)!=tb.count(op)-tb.count(cl): raise SystemExit(f'lexical regression {rel} {op}{cl}')
for ui in ROOT.rglob('*.ui'): ET.parse(ui)
print('QTsignalApp 5.26.0 Sommerfeld-scalar verification: PASS')
