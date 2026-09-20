from pathlib import Path
import json, subprocess, tempfile, xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1]
project=json.loads((ROOT/'QTsignalApp.qtproject.json').read_text())
main=(ROOT/'src/main.cpp').read_text(); hybrid=(ROOT/'src/hybrid_wire_surface_mom.cpp').read_text(); hh=(ROOT/'include/hybrid_wire_surface_mom.h').read_text(); designer=(ROOT/'src/antenna_designer_widget.cpp').read_text(); refs=(ROOT/'src/electromagnetism_reference_widget.cpp').read_text(); change=(ROOT/'CHANGELOG_APP.md').read_text(); bench=(ROOT/'src/antenna_validation_bench.cpp').read_text()
checks={
'version':project.get('version')=='5.36.0' and project.get('packaging',{}).get('productVersion')=='5.36.0' and 'QStringLiteral("5.36.0")' in main,
'api':all(x in hh for x in ['LayeredSlabSommerfeldComplexPowerGuard = 11','layeredSommerfeldComplexTransitionAttempted','layeredSommerfeldComplexTransitionFallbackUsed','powerClosureResidualW']),
'complex lift':all(x in hybrid for x in ['LayeredSlabSommerfeldComplexPowerGuard','complexLayeredCandidatePassive','populatePowerClosureAudit','full-complex wire/RWG HED/VED']),
'fallback':all(x in hybrid for x in ['safe.dielectricKernelModel=DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal','layeredSommerfeldComplexTransitionFallbackUsed=true']),
'ui':all(x in designer for x in ['Sommerfeld complex wire/RWG + passive-port guard (5.36)','DielectricKernelModel::LayeredSlabSommerfeldComplexPowerGuard','5.36 complex wire↔RWG transition','5.36 power audit']) and designer.count('m_hybridDielectricKernel->setCurrentIndex(11)')>=2,
'bench':'DielectricKernelModel::LayeredSlabSommerfeldComplexPowerGuard' in bench and 'Version 5.36' in bench,
'refs':all(x in refs for x in ['Monitored complex wire/RWG transition (5.36)','P<sub>closure,preview</sub>','automatically re-solves with the 5.35']),
'changelog':change.startswith('# QTsignalApp 5.36.0') and 'Monitored full-complex wire/RWG Sommerfeld transition' in change,
'qta':'root.insert(QStringLiteral("version"), 19)' in designer}
failed=[k for k,v in checks.items() if not v]
if failed: raise SystemExit('5.36 static verification FAILED: '+', '.join(failed))
for ui in (ROOT/'forms').glob('*.ui'): ET.parse(ui)
program=r'''
#include "hybrid_wire_surface_mom.h"
#include "antenna_surface_geometry.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>
using M=HybridWireSurfaceMom::DielectricKernelModel;
HybridWireSurfaceMom::Input via(M model,double er=3.2,double voltage=1.0,double frequency=300e6){
 HybridWireSurfaceMom::Input in;in.wire.frequencyHz=frequency;in.wire.computeFarField=true;in.computeFarField=true;in.wire.currentBasisTreatment=NumericalEM::WireCurrentBasisTreatment::LinearRooftopCharge;in.wire.segmentsPerWavelength=10;in.wire.maxUnknowns=50;in.maxTotalUnknowns=120;in.maxSurfaceUnknowns=80;double x=.07;
 in.wire.wires.push_back({"V",{x,0,-.009},{x,0,-.001},.0005});in.wire.feeds.push_back({"F",{x,0,-.005},{voltage,0},50});
 AntennaSurface::SurfaceSpec s;s.kind=AntennaSurface::SurfaceKind::Rectangle;s.center={0,0,0};s.widthM=.18;s.heightM=.14;s.meshHintM=.07;auto sm=AntennaSurface::triangulate(s,28);for(auto&t:sm)in.triangles.push_back({t.a,t.b,t.c,0});
 HybridWireSurfaceMom::DielectricRegion d;d.name="slab";d.centerM={0,0,-.005};d.widthM=.18;d.heightM=.14;d.thicknessM=.01;d.relativePermittivity=er;d.lossTangent=er>1.0?.01:0.0;d.fieldFillFactor=er>1.0?.6:0.0;in.dielectrics.push_back(d);in.useEffectiveDielectricRegions=true;in.dielectricKernelModel=model;return in;}
HybridWireSurfaceMom::Input hed(M model,double z=-.002,double voltage=1.0,double er=3.2){
 HybridWireSurfaceMom::Input in;in.wire.frequencyHz=300e6;in.wire.computeFarField=true;in.computeFarField=true;in.wire.currentBasisTreatment=NumericalEM::WireCurrentBasisTreatment::LinearRooftopCharge;in.wire.segmentsPerWavelength=18;in.wire.maxUnknowns=100;in.maxTotalUnknowns=220;in.maxSurfaceUnknowns=140;
 in.wire.wires.push_back({"W",{-0.10,0,z},{0.10,0,z},.0008});in.wire.feeds.push_back({"F",{0,0,z},{voltage,0},50});
 AntennaSurface::SurfaceSpec s;s.kind=AntennaSurface::SurfaceKind::Rectangle;s.center={0,0,0};s.widthM=.22;s.heightM=.18;s.meshHintM=.055;auto sm=AntennaSurface::triangulate(s,64);for(auto&t:sm)in.triangles.push_back({t.a,t.b,t.c,0});
 HybridWireSurfaceMom::DielectricRegion d;d.name="slab";d.centerM={0,0,-.005};d.widthM=.22;d.heightM=.18;d.thicknessM=.01;d.relativePermittivity=er;d.lossTangent=er>1.0?.01:0.0;d.fieldFillFactor=er>1.0?.6:0.0;in.dielectrics.push_back(d);in.useEffectiveDielectricRegions=true;in.dielectricKernelModel=model;return in;}
int main(){
 auto safe=HybridWireSurfaceMom::solve(via(M::LayeredSlabSommerfeldVedOffDiagonal));auto full=HybridWireSurfaceMom::solve(via(M::LayeredSlabSommerfeldComplexPowerGuard));auto full2=HybridWireSurfaceMom::solve(via(M::LayeredSlabSommerfeldComplexPowerGuard,3.2,2.0));
 if(!safe.valid||!full.valid||!full2.valid||full.feeds.empty())return 2;
 auto zs=safe.feeds[0].inputImpedanceOhm,zf=full.feeds[0].inputImpedanceOhm;
 if(!full.layeredSommerfeldComplexTransitionAttempted||!full.layeredSommerfeldComplexTransitionUsed||full.layeredSommerfeldComplexTransitionFallbackUsed)return 3;
 if(!(zf.real()>0.0&&full.acceptedPowerW>0.0&&std::abs(zf-zs)>1e-4&&std::abs(zf-zs)<1.0))return 4;
 if(std::abs(full2.feeds[0].inputImpedanceOhm-zf)>1e-8*std::max(1.0,std::abs(zf)))return 5;
 if(std::abs(full2.acceptedPowerW/full.acceptedPowerW-4.0)>1e-7)return 6;
 if(!full.layeredSommerfeldPowerAuditUsed||!full.layeredSommerfeldPowerAuditFarFieldIncomplete||!std::isfinite(full.powerClosureRelative))return 7;
 auto hs=HybridWireSurfaceMom::solve(hed(M::LayeredSlabSommerfeldVedOffDiagonal));auto hg=HybridWireSurfaceMom::solve(hed(M::LayeredSlabSommerfeldComplexPowerGuard));
 if(!hs.valid||!hg.valid||hg.feeds.empty())return 8;
 if(!hg.layeredSommerfeldComplexTransitionAttempted||hg.layeredSommerfeldComplexTransitionUsed||!hg.layeredSommerfeldComplexTransitionFallbackUsed)return 9;
 if(!(hg.layeredSommerfeldComplexCandidateInputImpedanceOhm.real()<0.0))return 10;
 if(std::abs(hg.feeds[0].inputImpedanceOhm-hs.feeds[0].inputImpedanceOhm)>1e-8*std::max(1.0,std::abs(hs.feeds[0].inputImpedanceOhm)))return 11;
 auto h0=HybridWireSurfaceMom::solve(via(M::LayeredSlabSommerfeldVedOffDiagonal,1.0));auto h1=HybridWireSurfaceMom::solve(via(M::LayeredSlabSommerfeldComplexPowerGuard,1.0));
 if(!h0.valid||!h1.valid||std::abs(h0.feeds[0].inputImpedanceOhm-h1.feeds[0].inputImpedanceOhm)>0.01)return 12;
 std::cout<<"5.36 VED safe/full="<<zs<<" / "<<zf<<" dZ="<<std::abs(zf-zs)<<" closure="<<full.powerClosureRelative<<"\n";
 std::cout<<"5.36 HED candidate="<<hg.layeredSommerfeldComplexCandidateInputImpedanceOhm<<" fallback="<<hg.feeds[0].inputImpedanceOhm<<"\n";
 std::cout<<"5.36 homogeneous delta="<<std::abs(h0.feeds[0].inputImpedanceOhm-h1.feeds[0].inputImpedanceOhm)<<"\n";
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    td=Path(td); cpp=td/'verify536.cpp'; exe=td/'verify536'; cpp.write_text(program)
    cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-I'+str(ROOT/'include'),str(ROOT/'src/numerical_em_solvers.cpp'),str(ROOT/'src/pec_surface_mom.cpp'),str(ROOT/'src/antenna_surface_geometry.cpp'),str(ROOT/'src/hybrid_wire_surface_mom.cpp'),str(cpp),'-o',str(exe)]
    subprocess.run(cmd,check=True); subprocess.run([str(exe)],check=True,timeout=480)
print('QTsignalApp 5.36.0 complex-power-guard verification: PASS')
