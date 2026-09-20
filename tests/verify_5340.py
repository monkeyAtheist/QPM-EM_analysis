from pathlib import Path
import json, subprocess, tempfile, xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1]
project=json.loads((ROOT/'QTsignalApp.qtproject.json').read_text())
main=(ROOT/'src/main.cpp').read_text(); hybrid=(ROOT/'src/hybrid_wire_surface_mom.cpp').read_text(); hh=(ROOT/'include/hybrid_wire_surface_mom.h').read_text(); designer=(ROOT/'src/antenna_designer_widget.cpp').read_text(); refs=(ROOT/'src/electromagnetism_reference_widget.cpp').read_text(); change=(ROOT/'CHANGELOG_APP.md').read_text(); bench=(ROOT/'src/antenna_validation_bench.cpp').read_text()
checks={
'version':project.get('version')=='5.34.0' and project.get('packaging',{}).get('productVersion')=='5.34.0' and 'QStringLiteral("5.34.0")' in main,
'api':all(x in hh for x in ['LayeredSlabSommerfeldVedNormal = 9','layeredSlabSommerfeldVedNormalUsed','layeredSommerfeldVedNormalVectorUsed','layeredSommerfeldVedScalarGradientUsed','layeredSommerfeldHedFullGradientUsed','layeredSommerfeldVedOffDiagonalGuarded']),
'ved':all(x in hybrid for x in ['vedScalarResidual','vectorNormalResidual','layeredWireSurfaceVedScalarResidualOverEpsilon','layeredWireSurfaceVedScalarTangentialGradient','layeredVedNormalSourceSupported']),
'reciprocal':all(x in hybrid for x in ['layeredWireSurfaceHedScalarFullGradient','observationStrictInterior','symmetrizeReciprocalMatrixBlock']),
'guard':all(x in hybrid for x in ['off-diagonal','real-projected','LayeredSlabSommerfeldVedNormal']),
'ui':all(x in designer for x in ['Sommerfeld VED / via normal-current transition (5.34)','DielectricKernelModel::LayeredSlabSommerfeldVedNormal','5.34 VED/via transition']) and designer.count('m_hybridDielectricKernel->setCurrentIndex(9)')>=2,
'bench':'DielectricKernelModel::LayeredSlabSommerfeldVedNormal' in bench and 'Version 5.34' in bench,
'refs':all(x in refs for x in ['VED / via normal-current transition (5.34)','ΔG<sub>Φ,VED</sub>','off-diagonal']),
'changelog':change.startswith('# QTsignalApp 5.34.0') and 'Guarded VED / via normal-current transition' in change,
'qta':'root.insert(QStringLiteral("version"), 19)' in designer}
failed=[k for k,v in checks.items() if not v]
if failed: raise SystemExit('5.34 static verification FAILED: '+', '.join(failed))
for ui in (ROOT/'forms').glob('*.ui'): ET.parse(ui)
program=r'''
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
HybridWireSurfaceMom::Input patch(const PG&g,HybridWireSurfaceMom::DielectricKernelModel model){HybridWireSurfaceMom::Input in;double la=C0/g.f0;in.wire.frequencyHz=g.f0;in.wire.computeFarField=false;in.computeFarField=false;AntennaSurface::SurfaceSpec a;a.kind=AntennaSurface::SurfaceKind::Rectangle;a.center={0,0,0};a.widthM=g.gw;a.heightM=g.gl;a.meshHintM=la/18;AntennaSurface::SurfaceSpec b;b.kind=AntennaSurface::SurfaceKind::Rectangle;b.center={0,0,g.h};b.widthM=g.w;b.heightM=g.l;b.meshHintM=la/25;auto am=AntennaSurface::triangulate(a,96),bm=AntennaSurface::triangulate(b,96);for(auto&t:am)in.triangles.push_back({t.a,t.b,t.c,0});for(auto&t:bm)in.triangles.push_back({t.a,t.b,t.c,1});HybridWireSurfaceMom::DifferentialSurfaceFeed sf;sf.name="P";sf.positivePositionM={0,g.fy,g.h};sf.negativePositionM={0,g.fy,0};sf.voltageV={1,0};sf.referenceOhm=50;sf.positiveSurfaceIndex=1;sf.negativeSurfaceIndex=0;sf.mappingToleranceM=std::max(g.w,g.l)/3;sf.footprintRadiusM=.08*la;in.differentialSurfaceFeeds.push_back(sf);HybridWireSurfaceMom::DielectricRegion d;d.name="D";d.centerM={0,0,.5*g.h};d.widthM=g.gw;d.heightM=g.gl;d.thicknessM=g.h;d.relativePermittivity=g.er;d.lossTangent=.02;d.fieldFillFactor=g.er>1?std::clamp((g.ee-1)/(g.er-1),0.,1.):0;in.dielectrics.push_back(d);in.maxTotalUnknowns=520;in.maxSurfaceUnknowns=420;in.enableTerminalHalfRwg=false;in.useEffectiveDielectricRegions=true;in.dielectricKernelModel=model;return in;}
HybridWireSurfaceMom::Input embedded(double z,double voltage,HybridWireSurfaceMom::DielectricKernelModel model,double er=3.2){HybridWireSurfaceMom::Input in;in.wire.frequencyHz=300e6;in.wire.computeFarField=false;in.computeFarField=false;in.wire.currentBasisTreatment=NumericalEM::WireCurrentBasisTreatment::LinearRooftopCharge;in.wire.segmentsPerWavelength=18;in.wire.maxUnknowns=100;in.maxTotalUnknowns=220;in.maxSurfaceUnknowns=140;in.wire.wires.push_back({"W",{-.10,0,z},{.10,0,z},.0008});in.wire.feeds.push_back({"F",{0,0,z},{voltage,0},50});AntennaSurface::SurfaceSpec s;s.kind=AntennaSurface::SurfaceKind::Rectangle;s.center={0,0,0};s.widthM=.22;s.heightM=.18;s.meshHintM=.055;auto sm=AntennaSurface::triangulate(s,64);for(auto&t:sm)in.triangles.push_back({t.a,t.b,t.c,0});HybridWireSurfaceMom::DielectricRegion d;d.name="slab";d.centerM={0,0,-.005};d.widthM=.22;d.heightM=.18;d.thicknessM=.01;d.relativePermittivity=er;d.lossTangent=.01;d.fieldFillFactor=.6;in.dielectrics.push_back(d);in.useEffectiveDielectricRegions=true;in.dielectricKernelModel=model;return in;}
HybridWireSurfaceMom::Input via(double x,double voltage,HybridWireSurfaceMom::DielectricKernelModel model,double er=3.2){auto in=embedded(-.005,voltage,model,er);in.wire.wires.clear();in.wire.feeds.clear();in.wire.wires.push_back({"V",{x,0,-.009},{x,0,-.001},.0005});in.wire.feeds.push_back({"F",{x,0,-.005},{voltage,0},50});return in;}
}
int main(){using M=HybridWireSurfaceMom::DielectricKernelModel;
auto g=pg(300e6);auto p8=HybridWireSurfaceMom::solve(patch(g,M::LayeredSlabSommerfeldInternalLayer));auto p9=HybridWireSurfaceMom::solve(patch(g,M::LayeredSlabSommerfeldVedNormal));if(!p8.valid||!p9.valid||p8.feeds.empty()||p9.feeds.empty())return 2;auto zp=p9.feeds[0].inputImpedanceOhm;if(std::abs(zp-p8.feeds[0].inputImpedanceOhm)>1e-9*std::max(1.0,std::abs(zp)))return 3;
auto h8=HybridWireSurfaceMom::solve(embedded(-.002,1,M::LayeredSlabSommerfeldInternalLayer));auto h9=HybridWireSurfaceMom::solve(embedded(-.002,1,M::LayeredSlabSommerfeldVedNormal));if(!h8.valid||!h9.valid||std::abs(h8.feeds[0].inputImpedanceOhm-h9.feeds[0].inputImpedanceOhm)>1e-7*std::max(1.0,std::abs(h8.feeds[0].inputImpedanceOhm)))return 4;
auto v8=HybridWireSurfaceMom::solve(via(.035,1,M::LayeredSlabSommerfeldInternalLayer));auto v9=HybridWireSurfaceMom::solve(via(.035,1,M::LayeredSlabSommerfeldVedNormal));auto v92=HybridWireSurfaceMom::solve(via(.035,2,M::LayeredSlabSommerfeldVedNormal));if(!v8.valid||!v9.valid||!v92.valid||v8.feeds.empty()||v9.feeds.empty()||v92.feeds.empty())return 5;auto z8=v8.feeds[0].inputImpedanceOhm,z9=v9.feeds[0].inputImpedanceOhm;double dv=std::abs(z9-z8);if(!(dv>1.0&&z9.real()>0.0&&v9.acceptedPowerW>0.0))return 6;if(!v9.layeredSlabSommerfeldVedNormalUsed||!v9.layeredSommerfeldVedNormalVectorUsed||!v9.layeredSommerfeldVedScalarGradientUsed||!v9.layeredSommerfeldHedFullGradientUsed||!v9.layeredSommerfeldVedOffDiagonalGuarded||v9.layeredSommerfeldInternalNormalCurrentGuarded)return 7;if(v9.mutualReciprocityRelative>1e-10)return 8;if(std::abs(z9-v92.feeds[0].inputImpedanceOhm)>1e-8*std::max(1.0,std::abs(z9)))return 9;if(std::abs(v92.acceptedPowerW/v9.acceptedPowerW-4.0)>1e-7)return 10;
auto hom8=HybridWireSurfaceMom::solve(via(.035,1,M::LayeredSlabSommerfeldInternalLayer,1.0));auto hom9=HybridWireSurfaceMom::solve(via(.035,1,M::LayeredSlabSommerfeldVedNormal,1.0));if(!hom8.valid||!hom9.valid||std::abs(hom8.feeds[0].inputImpedanceOhm-hom9.feeds[0].inputImpedanceOhm)>0.1)return 11;
std::cout<<"5.34 patch regression="<<zp<<"\n"<<"5.34 HED regression="<<h9.feeds[0].inputImpedanceOhm<<"\n"<<"5.34 via old/new="<<z8<<" / "<<z9<<" delta="<<dv<<"\n"<<"5.34 homogeneous delta="<<std::abs(hom8.feeds[0].inputImpedanceOhm-hom9.feeds[0].inputImpedanceOhm)<<"\n";return 0;}
'''
with tempfile.TemporaryDirectory() as td:
    td=Path(td); cpp=td/'verify534.cpp'; exe=td/'verify534'; cpp.write_text(program)
    cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-I'+str(ROOT/'include'),str(ROOT/'src/numerical_em_solvers.cpp'),str(ROOT/'src/pec_surface_mom.cpp'),str(ROOT/'src/antenna_surface_geometry.cpp'),str(ROOT/'src/hybrid_wire_surface_mom.cpp'),str(ROOT/'src/microstrip_models.cpp'),str(cpp),'-o',str(exe)]
    subprocess.run(cmd,check=True); subprocess.run([str(exe)],check=True,timeout=480)
print('QTsignalApp 5.34.0 VED/via verification: PASS')
