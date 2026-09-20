from pathlib import Path
import json, subprocess, tempfile, xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1]
project=json.loads((ROOT/'QTsignalApp.qtproject.json').read_text())
main=(ROOT/'src/main.cpp').read_text(); hybrid=(ROOT/'src/hybrid_wire_surface_mom.cpp').read_text(); hh=(ROOT/'include/hybrid_wire_surface_mom.h').read_text(); designer=(ROOT/'src/antenna_designer_widget.cpp').read_text(); refs=(ROOT/'src/electromagnetism_reference_widget.cpp').read_text(); change=(ROOT/'CHANGELOG_APP.md').read_text(); bench=(ROOT/'src/antenna_validation_bench.cpp').read_text()
checks={
'version':project.get('version')=='5.38.0' and project.get('packaging',{}).get('productVersion')=='5.38.0' and 'QStringLiteral("5.38.0")' in main,
'api':all(x in hh for x in ['LayeredSlabSommerfeldSurfaceWavePoleAudit = 13','LayeredSommerfeldPoleResult','layeredSommerfeldSurfaceWavePoleAuditUsed','layeredSommerfeldSurfaceWavePowerGuarded','layeredSurfaceWavePowerW']),
'poles':all(x in hybrid for x in ['findSlabGuidedPoleCandidates','refineSlabPole','slabPoleSpectralData','slabPoleDerivative','populateLayeredSurfaceWavePoleAudit','P_surface-wave']),
'ui':all(x in designer for x in ['Sommerfeld guided-pole audit + guarded surface-wave power (5.38)','LayeredSlabSommerfeldSurfaceWavePoleAudit','5.38 guided-pole audit']) and designer.count('m_hybridDielectricKernel->setCurrentIndex(13)')>=2,
'bench':'LayeredSlabSommerfeldSurfaceWavePoleAudit' in bench and 'Version 5.38' in bench,
'refs':all(x in refs for x in ['Guided-mode Sommerfeld pole audit (5.38)','D<sub>p</sub>','P<sub>surface-wave</sub> is intentionally guarded']),
'changelog':change.startswith('# QTsignalApp 5.38.0') and 'Guided-mode Sommerfeld pole extraction with guarded surface-wave power' in change,
'qta':'root.insert(QStringLiteral("version"), 19)' in designer}
failed=[k for k,v in checks.items() if not v]
if failed: raise SystemExit('5.38 static verification FAILED: '+', '.join(failed))
for ui in (ROOT/'forms').glob('*.ui'): ET.parse(ui)
program=r'''
#include "hybrid_wire_surface_mom.h"
#include "antenna_surface_geometry.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>
using M=HybridWireSurfaceMom::DielectricKernelModel;
HybridWireSurfaceMom::Input via(M model,double er=3.2,double loss=.01,double voltage=1.0){
 HybridWireSurfaceMom::Input in;in.wire.frequencyHz=300e6;in.wire.computeFarField=true;in.computeFarField=true;in.wire.currentBasisTreatment=NumericalEM::WireCurrentBasisTreatment::LinearRooftopCharge;in.wire.segmentsPerWavelength=10;in.wire.maxUnknowns=50;in.maxTotalUnknowns=120;in.maxSurfaceUnknowns=80;double x=.07;
 in.wire.wires.push_back({"V",{x,0,-.009},{x,0,-.001},.0005});in.wire.feeds.push_back({"F",{x,0,-.005},{voltage,0},50});
 AntennaSurface::SurfaceSpec s;s.kind=AntennaSurface::SurfaceKind::Rectangle;s.center={0,0,0};s.widthM=.18;s.heightM=.14;s.meshHintM=.07;auto sm=AntennaSurface::triangulate(s,28);for(auto&t:sm)in.triangles.push_back({t.a,t.b,t.c,0});
 HybridWireSurfaceMom::DielectricRegion d;d.name="slab";d.centerM={0,0,-.005};d.widthM=.18;d.heightM=.14;d.thicknessM=.01;d.relativePermittivity=er;d.lossTangent=loss;d.fieldFillFactor=er>1.0?.6:0.0;in.dielectrics.push_back(d);in.useEffectiveDielectricRegions=true;in.dielectricKernelModel=model;return in;}
int main(){
 auto r37=HybridWireSurfaceMom::solve(via(M::LayeredSlabSommerfeldPropagatingFarField));
 auto r38=HybridWireSurfaceMom::solve(via(M::LayeredSlabSommerfeldSurfaceWavePoleAudit));
 auto r2=HybridWireSurfaceMom::solve(via(M::LayeredSlabSommerfeldSurfaceWavePoleAudit,3.2,.01,2.0));
 if(!r37.valid||!r38.valid||!r2.valid||r38.feeds.empty())return 2;
 if(!r38.layeredSommerfeldPropagatingFarFieldUsed||!r38.layeredSommerfeldSurfaceWavePoleAuditUsed||!r38.layeredSommerfeldSurfaceWavePowerGuarded||r38.layeredSommerfeldSurfaceWavePowerResolved)return 3;
 if(r38.layeredSommerfeldSurfaceWavePoleCount<1||r38.layeredSommerfeldSurfaceWavePoles.empty()||r38.layeredSurfaceWavePowerW!=0.0)return 4;
 if(std::abs(r38.feeds[0].inputImpedanceOhm-r37.feeds[0].inputImpedanceOhm)>1e-9*std::max(1.0,std::abs(r37.feeds[0].inputImpedanceOhm)))return 5;
 if(std::abs(r38.layeredRadiatedPowerTotalW-r37.layeredRadiatedPowerTotalW)>1e-9*std::max(1e-18,r37.layeredRadiatedPowerTotalW))return 6;
 for(const auto &p:r38.layeredSommerfeldSurfaceWavePoles){if(!(p.betaOverK0>0.999&&p.attenuationNpPerM>=0.0&&p.denominatorMagnitude<2e-5&&p.residueMagnitudePerM>0.0))return 7;}
 if(std::abs(r2.feeds[0].inputImpedanceOhm-r38.feeds[0].inputImpedanceOhm)>1e-8*std::max(1.0,std::abs(r38.feeds[0].inputImpedanceOhm))||std::abs(r2.acceptedPowerW/r38.acceptedPowerW-4.0)>1e-7)return 8;
 auto h=HybridWireSurfaceMom::solve(via(M::LayeredSlabSommerfeldSurfaceWavePoleAudit,1.0,0.0));if(!h.valid||!h.layeredSommerfeldSurfaceWavePoleAuditUsed||h.layeredSommerfeldSurfaceWavePoleCount!=0)return 9;
 if(h.radiatedPowerW<=0||h.layeredRadiatedPowerTotalW<=0||std::abs(h.layeredRadiatedPowerTotalW/h.radiatedPowerW-1.0)>5e-4)return 10;
 std::cout<<"5.38 Zin="<<r38.feeds[0].inputImpedanceOhm<<" poles="<<r38.layeredSommerfeldSurfaceWavePoleCount<<" Pprop="<<r38.layeredRadiatedPowerTotalW<<" Psurf="<<r38.layeredSurfaceWavePowerW<<"\n";
 for(const auto&p:r38.layeredSommerfeldSurfaceWavePoles)std::cout<<"  "<<p.polarization<<" beta/k0="<<p.betaOverK0<<" alpha="<<p.attenuationNpPerM<<" |D|="<<p.denominatorMagnitude<<" |Res|="<<p.residueMagnitudePerM<<"\n";
 std::cout<<"5.38 homogeneous poles="<<h.layeredSommerfeldSurfaceWavePoleCount<<" Pprop/Pfree="<<h.layeredRadiatedPowerTotalW/h.radiatedPowerW<<"\n";return 0;}
'''
with tempfile.TemporaryDirectory() as td:
    td=Path(td); cpp=td/'verify538.cpp'; exe=td/'verify538'; cpp.write_text(program)
    cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-I'+str(ROOT/'include'),str(ROOT/'src/numerical_em_solvers.cpp'),str(ROOT/'src/pec_surface_mom.cpp'),str(ROOT/'src/antenna_surface_geometry.cpp'),str(ROOT/'src/hybrid_wire_surface_mom.cpp'),str(cpp),'-o',str(exe)]
    subprocess.run(cmd,check=True); subprocess.run([str(exe)],check=True,timeout=480)
print('QTsignalApp 5.38.0 guided-pole verification: PASS')
