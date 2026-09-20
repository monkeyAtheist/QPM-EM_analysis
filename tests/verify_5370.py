from pathlib import Path
import json, subprocess, tempfile, xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1]
project=json.loads((ROOT/'QTsignalApp.qtproject.json').read_text())
main=(ROOT/'src/main.cpp').read_text(); hybrid=(ROOT/'src/hybrid_wire_surface_mom.cpp').read_text(); hh=(ROOT/'include/hybrid_wire_surface_mom.h').read_text(); designer=(ROOT/'src/antenna_designer_widget.cpp').read_text(); refs=(ROOT/'src/electromagnetism_reference_widget.cpp').read_text(); change=(ROOT/'CHANGELOG_APP.md').read_text(); bench=(ROOT/'src/antenna_validation_bench.cpp').read_text()
checks={
'version':project.get('version')=='5.37.0' and project.get('packaging',{}).get('productVersion')=='5.37.0' and 'QStringLiteral("5.37.0")' in main,
'api':all(x in hh for x in ['LayeredSlabSommerfeldPropagatingFarField = 12','layeredRadiatedPowerUpperW','layeredRadiatedPowerLowerW','layeredFarFieldNormalized','layeredSommerfeldPropagatingFarFieldPowerConsistent']),
'farfield':all(x in hybrid for x in ['populateLayeredPropagatingFarFieldAudit','layeredPropagatingPlaneWaveMoment','teReflection','teTransmission','layeredRadiatedPowerUpperW','layeredPropagatingPowerGuardPass']),
'ui':all(x in designer for x in ['Sommerfeld propagating far-field + power guard (5.37)','LayeredSlabSommerfeldPropagatingFarField','5.37 propagating layered far field','useLayeredPattern']) and designer.count('m_hybridDielectricKernel->setCurrentIndex(12)')>=2,
'bench':'LayeredSlabSommerfeldPropagatingFarField' in bench and 'Version 5.37' in bench,
'refs':all(x in refs for x in ['Propagating layered far-field power audit (5.37)','P<sub>prop</sub>','P<sub>upper</sub>','Surface-wave pole residues']),
'changelog':change.startswith('# QTsignalApp 5.37.0') and 'Propagating TE/TM layered far-field and one-sided power guard' in change,
'qta':'root.insert(QStringLiteral("version"), 19)' in designer}
failed=[k for k,v in checks.items() if not v]
if failed: raise SystemExit('5.37 static verification FAILED: '+', '.join(failed))
for ui in (ROOT/'forms').glob('*.ui'): ET.parse(ui)
program=r'''
#include "hybrid_wire_surface_mom.h"
#include "antenna_surface_geometry.h"
#include "microstrip_models.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>
using M=HybridWireSurfaceMom::DielectricKernelModel;
HybridWireSurfaceMom::Input via(M model,double er=3.2,double voltage=1.0){
 HybridWireSurfaceMom::Input in;in.wire.frequencyHz=300e6;in.wire.computeFarField=true;in.computeFarField=true;in.wire.currentBasisTreatment=NumericalEM::WireCurrentBasisTreatment::LinearRooftopCharge;in.wire.segmentsPerWavelength=10;in.wire.maxUnknowns=50;in.maxTotalUnknowns=120;in.maxSurfaceUnknowns=80;double x=.07;
 in.wire.wires.push_back({"V",{x,0,-.009},{x,0,-.001},.0005});in.wire.feeds.push_back({"F",{x,0,-.005},{voltage,0},50});
 AntennaSurface::SurfaceSpec s;s.kind=AntennaSurface::SurfaceKind::Rectangle;s.center={0,0,0};s.widthM=.18;s.heightM=.14;s.meshHintM=.07;auto sm=AntennaSurface::triangulate(s,28);for(auto&t:sm)in.triangles.push_back({t.a,t.b,t.c,0});
 HybridWireSurfaceMom::DielectricRegion d;d.name="slab";d.centerM={0,0,-.005};d.widthM=.18;d.heightM=.14;d.thicknessM=.01;d.relativePermittivity=er;d.lossTangent=er>1.0?.01:0.0;d.fieldFillFactor=er>1.0?.6:0.0;in.dielectrics.push_back(d);in.useEffectiveDielectricRegions=true;in.dielectricKernelModel=model;return in;}
HybridWireSurfaceMom::Input patch(M model,bool far){
 constexpr double C0=299792458.0;double f=300e6,la=C0/f,h=.0016,er=4.2;auto e=MicrostripModels::rectangularPatch(f,h,er);double w=e.widthM,l=e.physicalLengthM,ee=e.effectivePermittivity,gw=std::max(1.35*w,w+12*h),gl=std::max(1.35*l,l+12*h),fy=-.22*l;HybridWireSurfaceMom::Input in;in.wire.frequencyHz=f;in.computeFarField=far;in.wire.computeFarField=far;AntennaSurface::SurfaceSpec a;a.kind=AntennaSurface::SurfaceKind::Rectangle;a.center={0,0,0};a.widthM=gw;a.heightM=gl;a.meshHintM=la/12;auto b=a;b.center={0,0,h};b.widthM=w;b.heightM=l;b.meshHintM=la/16;auto am=AntennaSurface::triangulate(a,64),bm=AntennaSurface::triangulate(b,64);for(auto&t:am)in.triangles.push_back({t.a,t.b,t.c,0});for(auto&t:bm)in.triangles.push_back({t.a,t.b,t.c,1});HybridWireSurfaceMom::DifferentialSurfaceFeed sf;sf.name="P";sf.positivePositionM={0,fy,h};sf.negativePositionM={0,fy,0};sf.voltageV={1,0};sf.referenceOhm=50;sf.positiveSurfaceIndex=1;sf.negativeSurfaceIndex=0;sf.mappingToleranceM=std::max(w,l)/3;sf.footprintRadiusM=.08*la;in.differentialSurfaceFeeds.push_back(sf);HybridWireSurfaceMom::DielectricRegion d;d.name="D";d.centerM={0,0,.5*h};d.widthM=gw;d.heightM=gl;d.thicknessM=h;d.relativePermittivity=er;d.lossTangent=.02;d.fieldFillFactor=std::clamp((ee-1)/(er-1),0.,1.);in.dielectrics.push_back(d);in.maxTotalUnknowns=520;in.maxSurfaceUnknowns=420;in.enableTerminalHalfRwg=false;in.useEffectiveDielectricRegions=true;in.dielectricKernelModel=model;return in;}
int main(){
 auto old=HybridWireSurfaceMom::solve(via(M::LayeredSlabSommerfeldComplexPowerGuard));auto r=HybridWireSurfaceMom::solve(via(M::LayeredSlabSommerfeldPropagatingFarField));auto r2=HybridWireSurfaceMom::solve(via(M::LayeredSlabSommerfeldPropagatingFarField,3.2,2.0));if(!old.valid||!r.valid||!r2.valid||r.feeds.empty())return 2;
 if(!r.layeredSommerfeldPropagatingFarFieldUsed||!r.layeredSommerfeldPropagatingFarFieldPowerConsistent||r.layeredSommerfeldPropagatingFarFieldGuardRejected||!r.layeredSommerfeldComplexTransitionUsed)return 3;
 if(!(r.layeredRadiatedPowerUpperW>0&&r.layeredRadiatedPowerLowerW>0&&r.layeredRadiatedPowerTotalW>0&&std::abs((r.layeredRadiatedPowerUpperW+r.layeredRadiatedPowerLowerW)-r.layeredRadiatedPowerTotalW)<1e-12))return 4;
 if(r.layeredFarFieldNormalized.empty()||r.layeredFarFieldNormalized.size()!=r.farFieldNormalized.size()||!std::isfinite(r.layeredDirectivityDbi))return 5;
 if(std::abs(r.feeds[0].inputImpedanceOhm-old.feeds[0].inputImpedanceOhm)>1e-8*std::max(1.0,std::abs(old.feeds[0].inputImpedanceOhm)))return 6;
 if(std::abs(r2.feeds[0].inputImpedanceOhm-r.feeds[0].inputImpedanceOhm)>1e-8*std::max(1.0,std::abs(r.feeds[0].inputImpedanceOhm))||std::abs(r2.acceptedPowerW/r.acceptedPowerW-4.0)>1e-7)return 7;
 auto h=HybridWireSurfaceMom::solve(via(M::LayeredSlabSommerfeldPropagatingFarField,1.0));if(!h.valid||h.radiatedPowerW<=0||h.layeredRadiatedPowerTotalW<=0)return 8;double hr=h.layeredRadiatedPowerTotalW/h.radiatedPowerW;if(std::abs(hr-1.0)>5e-4)return 9;
 auto p6=HybridWireSurfaceMom::solve(patch(M::LayeredSlabSommerfeldLongitudinalHed,false));auto p7=HybridWireSurfaceMom::solve(patch(M::LayeredSlabSommerfeldPropagatingFarField,true));if(!p6.valid||!p7.valid||p6.feeds.empty()||p7.feeds.empty())return 10;if(std::abs(p6.feeds[0].inputImpedanceOhm-p7.feeds[0].inputImpedanceOhm)>1e-7*std::max(1.0,std::abs(p6.feeds[0].inputImpedanceOhm)))return 11;if(!p7.layeredSommerfeldPropagatingFarFieldUsed||p7.layeredRadiatedPowerTotalW<=0)return 12;
 std::cout<<"5.37 via Zin="<<r.feeds[0].inputImpedanceOhm<<" Pupper/lower="<<r.layeredRadiatedPowerUpperW<<" / "<<r.layeredRadiatedPowerLowerW<<" Pprop="<<r.layeredRadiatedPowerTotalW<<" closure="<<r.layeredPowerClosureRelative<<" D="<<r.layeredDirectivityDbi<<"\n";
 std::cout<<"5.37 homogeneous Pprop/Pfree="<<hr<<"\n";
 std::cout<<"5.37 patch regression="<<p7.feeds[0].inputImpedanceOhm<<" Pprop="<<p7.layeredRadiatedPowerTotalW<<"\n";return 0;}
'''
with tempfile.TemporaryDirectory() as td:
    td=Path(td); cpp=td/'verify537.cpp'; exe=td/'verify537'; cpp.write_text(program)
    cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-I'+str(ROOT/'include'),str(ROOT/'src/numerical_em_solvers.cpp'),str(ROOT/'src/pec_surface_mom.cpp'),str(ROOT/'src/antenna_surface_geometry.cpp'),str(ROOT/'src/hybrid_wire_surface_mom.cpp'),str(ROOT/'src/microstrip_models.cpp'),str(cpp),'-o',str(exe)]
    subprocess.run(cmd,check=True); subprocess.run([str(exe)],check=True,timeout=480)
print('QTsignalApp 5.37.0 propagating-layered-far-field verification: PASS')
