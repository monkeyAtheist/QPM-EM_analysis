from pathlib import Path
import json, subprocess, tempfile, xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1]
project=json.loads((ROOT/'QTsignalApp.qtproject.json').read_text())
main=(ROOT/'src/main.cpp').read_text(); hybrid=(ROOT/'src/hybrid_wire_surface_mom.cpp').read_text(); hh=(ROOT/'include/hybrid_wire_surface_mom.h').read_text(); designer=(ROOT/'src/antenna_designer_widget.cpp').read_text(); refs=(ROOT/'src/electromagnetism_reference_widget.cpp').read_text(); change=(ROOT/'CHANGELOG_APP.md').read_text(); bench=(ROOT/'src/antenna_validation_bench.cpp').read_text(); roadmap=(ROOT/'SIMULATION_DESIGN_V1_ROADMAP.md').read_text()
checks={
'version':project.get('version')=='5.39.0' and project.get('packaging',{}).get('productVersion')=='5.39.0' and 'QStringLiteral("5.39.0")' in main,
'api':all(x in hh for x in ['LayeredSlabSommerfeldGroundedPecModeAudit = 14','layeredSommerfeldGroundedPecModeAuditUsed','layeredSommerfeldGroundCoverageFraction','modalPowerNormalizationWPerM','groundedPecBoundary']),
'grounded':all(x in hybrid for x in ['groundedPecDispersionData','q*std::tan(q*h)-epsr*alpha','q/t+alpha','groundedPecCoverageFraction','findGroundedPecModes','populateGroundedModeNormalization','populateLayeredGroundedPecModeAudit']),
'ui':all(x in designer for x in ['Sommerfeld grounded-PEC modal audit + normalization (5.39)','LayeredSlabSommerfeldGroundedPecModeAudit','5.39 grounded-PEC modal audit']) and designer.count('m_hybridDielectricKernel->setCurrentIndex(14)')>=2,
'bench':'LayeredSlabSommerfeldGroundedPecModeAudit' in bench and 'Version 5.39' in bench,
'refs':all(x in refs for x in ['Grounded PEC modal reference (5.39)','TM: q<sub>2</sub> tan(q<sub>2</sub>h)','Simulation / Antenna Designer 1.0 scope']),
'roadmap':all(x in roadmap for x in ['Definition of Simulation / Designer 1.0','5.40 — Hybrid broadband sweep','5.41 — Validation closure','5.42 — 1.0 stabilization release candidate','QTsignalApp 6.0.0']),
'changelog':change.startswith('# QTsignalApp 5.39.0') and 'Grounded-PEC modal reference and Simulation/Designer 1.0 scope freeze' in change,
'qta':'root.insert(QStringLiteral("version"), 19)' in designer}
failed=[k for k,v in checks.items() if not v]
if failed: raise SystemExit('5.39 static verification FAILED: '+', '.join(failed))
for ui in (ROOT/'forms').glob('*.ui'): ET.parse(ui)
program=r'''
#include "hybrid_wire_surface_mom.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>
using M=HybridWireSurfaceMom::DielectricKernelModel;
HybridWireSurfaceMom::Input via(M model,double er=3.2,double loss=.01,double voltage=1.0,bool ground=true){
 HybridWireSurfaceMom::Input in;in.wire.frequencyHz=300e6;in.wire.computeFarField=true;in.computeFarField=true;in.wire.currentBasisTreatment=NumericalEM::WireCurrentBasisTreatment::LinearRooftopCharge;in.wire.segmentsPerWavelength=10;in.wire.maxUnknowns=50;in.maxTotalUnknowns=120;in.maxSurfaceUnknowns=80;double x=.07;
 in.wire.wires.push_back({"V",{x,0,.001},{x,0,.009},.0005});in.wire.feeds.push_back({"F",{x,0,.005},{voltage,0},50});
 const double gx=.09,gy=.07,gz=ground?0.0:.01;in.triangles.push_back({{-gx,-gy,gz},{gx,-gy,gz},{gx,gy,gz},0});in.triangles.push_back({{-gx,-gy,gz},{gx,gy,gz},{-gx,gy,gz},0});
 HybridWireSurfaceMom::DielectricRegion d;d.name="slab";d.centerM={0,0,.005};d.widthM=.18;d.heightM=.14;d.thicknessM=.01;d.relativePermittivity=er;d.lossTangent=loss;d.fieldFillFactor=er>1.0?.6:0.0;in.dielectrics.push_back(d);in.useEffectiveDielectricRegions=true;in.dielectricKernelModel=model;return in;}
int main(){
 auto r39=HybridWireSurfaceMom::solve(via(M::LayeredSlabSommerfeldGroundedPecModeAudit));
 if(!r39.valid||r39.feeds.empty())return 2;
 if(!r39.layeredSommerfeldGroundedPecModeAuditUsed||!r39.layeredSommerfeldGroundedPecBoundaryDetected||r39.layeredSommerfeldGroundCoverageFraction<.95)return 3;
 if(r39.layeredSommerfeldGroundedPecModeCount<1||!r39.layeredSommerfeldGroundedPecPowerGuarded||r39.layeredSurfaceWavePowerW!=0.0)return 4;
 int tm=0,te=0;for(const auto&m:r39.layeredSommerfeldGroundedPecModes){if(m.polarization=="TM")++tm;else if(m.polarization=="TE")++te;if(!m.groundedPecBoundary||!(m.betaOverK0>1.0)||!(m.denominatorMagnitude<2e-6)||!(m.modalPowerNormalizationWPerM>0.0)||m.dielectricPowerFraction<0.0||m.dielectricPowerFraction>1.0)return 5;}
 if(tm<1||te!=0)return 6; // electrically thin grounded slab: TM0, no low-frequency TE mode
 if(!r39.layeredSommerfeldPropagatingFarFieldUsed||r39.layeredRadiatedPowerTotalW<=0.0)return 7;
 auto ng=HybridWireSurfaceMom::solve(via(M::LayeredSlabSommerfeldGroundedPecModeAudit,3.2,.01,1.0,false));if(!ng.valid||ng.layeredSommerfeldGroundedPecBoundaryDetected||ng.layeredSommerfeldGroundedPecModeCount!=0||ng.layeredSommerfeldGroundCoverageFraction>.05)return 8;
 std::cout<<"5.39 Zin="<<r39.feeds[0].inputImpedanceOhm<<" groundCoverage="<<r39.layeredSommerfeldGroundCoverageFraction<<" modes="<<r39.layeredSommerfeldGroundedPecModeCount<<"\n";
 for(const auto&m:r39.layeredSommerfeldGroundedPecModes)std::cout<<"  "<<m.polarization<<" beta/k0="<<m.betaOverK0<<" alpha="<<m.attenuationNpPerM<<" Pnorm="<<m.modalPowerNormalizationWPerM<<" W/m frac_diel="<<m.dielectricPowerFraction<<"\n";
 std::cout<<"5.39 no-ground coverage="<<ng.layeredSommerfeldGroundCoverageFraction<<"\n";return 0;}
'''
with tempfile.TemporaryDirectory() as td:
    td=Path(td); cpp=td/'verify539.cpp'; exe=td/'verify539'; cpp.write_text(program)
    cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-I'+str(ROOT/'include'),str(ROOT/'src/numerical_em_solvers.cpp'),str(ROOT/'src/pec_surface_mom.cpp'),str(ROOT/'src/hybrid_wire_surface_mom.cpp'),str(cpp),'-o',str(exe)]
    subprocess.run(cmd,check=True); subprocess.run([str(exe)],check=True,timeout=480)
print('QTsignalApp 5.39.0 grounded-PEC modal + v1-roadmap verification: PASS')
