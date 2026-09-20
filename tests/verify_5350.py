from pathlib import Path
import json, subprocess, tempfile, xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1]
project=json.loads((ROOT/'QTsignalApp.qtproject.json').read_text())
main=(ROOT/'src/main.cpp').read_text(); hybrid=(ROOT/'src/hybrid_wire_surface_mom.cpp').read_text(); hh=(ROOT/'include/hybrid_wire_surface_mom.h').read_text(); designer=(ROOT/'src/antenna_designer_widget.cpp').read_text(); refs=(ROOT/'src/electromagnetism_reference_widget.cpp').read_text(); change=(ROOT/'CHANGELOG_APP.md').read_text(); bench=(ROOT/'src/antenna_validation_bench.cpp').read_text()
checks={
'version':project.get('version')=='5.35.0' and project.get('packaging',{}).get('productVersion')=='5.35.0' and 'QStringLiteral("5.35.0")' in main,
'api':all(x in hh for x in ['LayeredSlabSommerfeldVedOffDiagonal = 10','layeredSommerfeldVedOffDiagonalVectorUsed','layeredSommerfeldVedOffDiagonalReciprocityGuardUsed']),
'cavity derivatives':all(x in hybrid for x in ['InternalCavityResidualData','signedObservation','signedSource','dFullObs','dFullSrc']),
'j1 mixed transform':all(x in hybrid for x in ['vectorRhoFromNormal','vectorNormalFromRho','std::cyl_bessel_j(1.0,z)','crossWeight','k2sq']),
'active dyadic':all(x in hybrid for x in ['G_{rho z}','G_{z rho}','LayeredSlabSommerfeldVedOffDiagonal && rho>1e-12']),
'guards':all(x in hybrid for x in ['layeredSommerfeldVedOffDiagonalReciprocityGuardUsed','symmetrizeReciprocalMatrixBlock','layeredSommerfeldReactiveProjectionUsed']),
'ui':all(x in designer for x in ['Sommerfeld VED off-diagonal rho-z / z-rho (5.35)','DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal','5.35 mixed TM vector dyadic']) and designer.count('m_hybridDielectricKernel->setCurrentIndex(10)')>=2,
'bench':'DielectricKernelModel::LayeredSlabSommerfeldVedOffDiagonal' in bench and 'Version 5.35' in bench,
'refs':all(x in refs for x in ['VED mixed rho-z / z-rho vector transition (5.35)','G<sub>&rho;z</sub>','G<sub>z&rho;</sub>','J<sub>1</sub>']),
'changelog':change.startswith('# QTsignalApp 5.35.0') and 'Reciprocal VED mixed rho-z / z-rho vector transition' in change,
'qta':'root.insert(QStringLiteral("version"), 19)' in designer}
failed=[k for k,v in checks.items() if not v]
if failed: raise SystemExit('5.35 static verification FAILED: '+', '.join(failed))
for ui in (ROOT/'forms').glob('*.ui'): ET.parse(ui)
program=r'''
#include "hybrid_wire_surface_mom.h"
#include "antenna_surface_geometry.h"
#include <cmath>
#include <complex>
#include <iostream>
using M=HybridWireSurfaceMom::DielectricKernelModel;
HybridWireSurfaceMom::Input via(M model,double er=3.2,double voltage=1.0,double x=.07){
 HybridWireSurfaceMom::Input in;in.wire.frequencyHz=300e6;in.wire.computeFarField=false;in.computeFarField=false;in.wire.currentBasisTreatment=NumericalEM::WireCurrentBasisTreatment::LinearRooftopCharge;in.wire.segmentsPerWavelength=10;in.wire.maxUnknowns=50;in.maxTotalUnknowns=120;in.maxSurfaceUnknowns=80;
 in.wire.wires.push_back({"V",{x,0,-.009},{x,0,-.001},.0005});in.wire.feeds.push_back({"F",{x,0,-.005},{voltage,0},50});
 AntennaSurface::SurfaceSpec s;s.kind=AntennaSurface::SurfaceKind::Rectangle;s.center={0,0,0};s.widthM=.18;s.heightM=.14;s.meshHintM=.07;auto sm=AntennaSurface::triangulate(s,28);for(auto&t:sm)in.triangles.push_back({t.a,t.b,t.c,0});
 HybridWireSurfaceMom::DielectricRegion d;d.name="slab";d.centerM={0,0,-.005};d.widthM=.18;d.heightM=.14;d.thicknessM=.01;d.relativePermittivity=er;d.lossTangent=.01;d.fieldFillFactor=er>1.0?.6:0.0;in.dielectrics.push_back(d);in.useEffectiveDielectricRegions=true;in.dielectricKernelModel=model;return in;}
int main(){
 auto old=HybridWireSurfaceMom::solve(via(M::LayeredSlabSommerfeldVedNormal));
 auto now=HybridWireSurfaceMom::solve(via(M::LayeredSlabSommerfeldVedOffDiagonal));
 auto now2=HybridWireSurfaceMom::solve(via(M::LayeredSlabSommerfeldVedOffDiagonal,3.2,2.0));
 auto homOld=HybridWireSurfaceMom::solve(via(M::LayeredSlabSommerfeldVedNormal,1.0));
 auto homNow=HybridWireSurfaceMom::solve(via(M::LayeredSlabSommerfeldVedOffDiagonal,1.0));
 if(!old.valid||!now.valid||!now2.valid||!homOld.valid||!homNow.valid||old.feeds.empty()||now.feeds.empty()||now2.feeds.empty())return 2;
 const auto zo=old.feeds[0].inputImpedanceOhm,zn=now.feeds[0].inputImpedanceOhm;
 const double dz=std::abs(zn-zo),dh=std::abs(homNow.feeds[0].inputImpedanceOhm-homOld.feeds[0].inputImpedanceOhm);
 if(!(dz>0.1&&dz<10.0&&zn.real()>0.0&&now.acceptedPowerW>0.0))return 3;
 if(!now.layeredSommerfeldVedOffDiagonalVectorUsed||!now.layeredSommerfeldVedOffDiagonalReciprocityGuardUsed||now.layeredSommerfeldVedOffDiagonalGuarded)return 4;
 if(now.mutualReciprocityRelative>1e-10)return 5;
 if(std::abs(now2.feeds[0].inputImpedanceOhm-zn)>1e-8*std::max(1.0,std::abs(zn)))return 6;
 if(std::abs(now2.acceptedPowerW/now.acceptedPowerW-4.0)>1e-7)return 7;
 if(dh>0.05)return 8;
 std::cout<<"5.35 via old/new="<<zo<<" / "<<zn<<" delta="<<dz<<"\n";
 std::cout<<"5.35 homogeneous delta="<<dh<<" reciprocity="<<now.mutualReciprocityRelative<<" pre="<<now.mutualReciprocityPreSymmetryRelative<<"\n";
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    td=Path(td); cpp=td/'verify535.cpp'; exe=td/'verify535'; cpp.write_text(program)
    cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-I'+str(ROOT/'include'),str(ROOT/'src/numerical_em_solvers.cpp'),str(ROOT/'src/pec_surface_mom.cpp'),str(ROOT/'src/antenna_surface_geometry.cpp'),str(ROOT/'src/hybrid_wire_surface_mom.cpp'),str(cpp),'-o',str(exe)]
    subprocess.run(cmd,check=True); subprocess.run([str(exe)],check=True,timeout=480)
print('QTsignalApp 5.35.0 VED off-diagonal verification: PASS')
