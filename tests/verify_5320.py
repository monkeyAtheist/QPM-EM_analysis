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
    'version': project.get('version') == '5.32.0' and project.get('packaging',{}).get('productVersion') == '5.32.0' and 'QStringLiteral("5.32.0")' in main,
    'api': all(x in hybrid_h for x in [
        'LayeredSlabSommerfeldExteriorHeight = 7',
        'layeredSlabSommerfeldExteriorHeightUsed',
        'layeredSommerfeldExteriorWireSurfaceVectorUsed',
        'layeredSommerfeldExteriorHedScalarDiagnosticEvaluated']),
    'exterior spectrum': all(x in hybrid for x in [
        'pointInExteriorHalfSpaceOfSlab', 'layeredExteriorPairFor',
        'exp[-gamma1(d_o+d_s)]', 'verticalPhase',
        'sommerfeldExteriorRadialTable', 'exteriorLayeredVectorPotentialCorrection']),
    'reactive guard': all(x in hybrid for x in [
        'layered.x=C{layered.x.real(),0.0}',
        'internal-layer source',
        'dyadicDot=C{dyadicDot.real(),0.0}']),
    'ui': all(x in designer for x in [
        'Sommerfeld exterior-height wire/RWG transition (5.32)',
        'DielectricKernelModel::LayeredSlabSommerfeldExteriorHeight',
        '5.32 exterior-height transition']) and designer.count('m_hybridDielectricKernel->setCurrentIndex(7)') >= 2,
    'benchmark': 'DielectricKernelModel::LayeredSlabSommerfeldExteriorHeight' in bench and 'Version 5.32' in bench,
    'refs': all(x in refs for x in [
        'Exterior-height Sommerfeld wire/RWG transition (5.32)',
        'P_z(k_ρ;d_o,d_s)', '5.32 still guards incomplete arbitrary-height physics']),
    'changelog': change.startswith('# QTsignalApp 5.32.0') and 'Exterior-height Sommerfeld transition for wire↔RWG coupling' in change and '.qta` schema remains version 19' in change,
    'qta': 'root.insert(QStringLiteral("version"), 19)' in designer,
}
failed = [k for k,v in checks.items() if not v]
if failed:
    raise SystemExit('5.32 static verification FAILED: ' + ', '.join(failed))

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
HybridWireSurfaceMom::Input patch(const PG&g,HybridWireSurfaceMom::DielectricKernelModel model){HybridWireSurfaceMom::Input in;double la=C0/g.f0;in.wire.frequencyHz=g.f0;in.wire.computeFarField=false;in.computeFarField=false;AntennaSurface::SurfaceSpec a;a.kind=AntennaSurface::SurfaceKind::Rectangle;a.center={0,0,0};a.widthM=g.gw;a.heightM=g.gl;a.meshHintM=la/18;AntennaSurface::SurfaceSpec b;b.kind=AntennaSurface::SurfaceKind::Rectangle;b.center={0,0,g.h};b.widthM=g.w;b.heightM=g.l;b.meshHintM=la/25;auto am=AntennaSurface::triangulate(a,96),bm=AntennaSurface::triangulate(b,96);for(auto&t:am)in.triangles.push_back({t.a,t.b,t.c,0});for(auto&t:bm)in.triangles.push_back({t.a,t.b,t.c,1});HybridWireSurfaceMom::DifferentialSurfaceFeed sf;sf.name="P";sf.positivePositionM={0,g.fy,g.h};sf.negativePositionM={0,g.fy,0};sf.voltageV={1,0};sf.referenceOhm=50;sf.positiveSurfaceIndex=1;sf.negativeSurfaceIndex=0;sf.mappingToleranceM=std::max(g.w,g.l)/3;sf.footprintRadiusM=.08*la;in.differentialSurfaceFeeds.push_back(sf);HybridWireSurfaceMom::DielectricRegion d;d.name="D";d.centerM={0,0,.5*g.h};d.widthM=g.gw;d.heightM=g.gl;d.thicknessM=g.h;d.relativePermittivity=g.er;d.lossTangent=.02;d.fieldFillFactor=g.er>1?std::clamp((g.ee-1)/(g.er-1),0.,1.):0;in.dielectrics.push_back(d);in.maxTotalUnknowns=520;in.maxSurfaceUnknowns=420;in.enableTerminalHalfRwg=false;in.useEffectiveDielectricRegions=true;in.dielectricKernelModel=model;return in;}
HybridWireSurfaceMom::Input elevated(double z,double voltage,HybridWireSurfaceMom::DielectricKernelModel model){HybridWireSurfaceMom::Input in;in.wire.frequencyHz=300e6;in.wire.computeFarField=false;in.computeFarField=false;in.wire.currentBasisTreatment=NumericalEM::WireCurrentBasisTreatment::LinearRooftopCharge;in.wire.segmentsPerWavelength=18;in.wire.maxUnknowns=100;in.maxTotalUnknowns=220;in.maxSurfaceUnknowns=140;in.wire.wires.push_back({"W",{-0.12,0,z},{0.12,0,z},.001});in.wire.feeds.push_back({"F",{0,0,z},{voltage,0},50});AntennaSurface::SurfaceSpec s;s.kind=AntennaSurface::SurfaceKind::Rectangle;s.center={0,0,0};s.widthM=.22;s.heightM=.18;s.meshHintM=.055;auto sm=AntennaSurface::triangulate(s,64);for(auto&t:sm)in.triangles.push_back({t.a,t.b,t.c,0});HybridWireSurfaceMom::DielectricRegion d;d.name="slab";d.centerM={0,0,-.005};d.widthM=.22;d.heightM=.18;d.thicknessM=.01;d.relativePermittivity=3.2;d.lossTangent=.01;d.fieldFillFactor=.6;in.dielectrics.push_back(d);in.useEffectiveDielectricRegions=true;in.dielectricKernelModel=model;return in;}
}
int main(){using M=HybridWireSurfaceMom::DielectricKernelModel;auto g=pg(300e6);auto p31=HybridWireSurfaceMom::solve(patch(g,M::LayeredSlabSommerfeldLongitudinalHed));auto p32=HybridWireSurfaceMom::solve(patch(g,M::LayeredSlabSommerfeldExteriorHeight));if(!p31.valid||!p32.valid||p31.feeds.empty()||p32.feeds.empty())return 2;auto z31=p31.feeds[0].inputImpedanceOhm,z32=p32.feeds[0].inputImpedanceOhm;if(std::abs(z31-z32)>1e-8*std::max(1.0,std::abs(z31)))return 3;if(!p32.layeredSlabSommerfeldExteriorHeightUsed||p32.layeredSommerfeldExteriorWireSurfaceVectorUsed)return 4;
auto nearOld=HybridWireSurfaceMom::solve(elevated(.03,1,M::LayeredSlabSommerfeldLongitudinalHed));auto nearNew=HybridWireSurfaceMom::solve(elevated(.03,1,M::LayeredSlabSommerfeldExteriorHeight));auto near2=HybridWireSurfaceMom::solve(elevated(.03,2,M::LayeredSlabSommerfeldExteriorHeight));auto farOld=HybridWireSurfaceMom::solve(elevated(.12,1,M::LayeredSlabSommerfeldLongitudinalHed));auto farNew=HybridWireSurfaceMom::solve(elevated(.12,1,M::LayeredSlabSommerfeldExteriorHeight));if(!nearOld.valid||!nearNew.valid||!near2.valid||!farOld.valid||!farNew.valid||nearNew.feeds.empty()||near2.feeds.empty()||farNew.feeds.empty())return 5;auto zn0=nearOld.feeds[0].inputImpedanceOhm,zn=nearNew.feeds[0].inputImpedanceOhm,zf0=farOld.feeds[0].inputImpedanceOhm,zf=farNew.feeds[0].inputImpedanceOhm;const double dn=std::abs(zn-zn0),df=std::abs(zf-zf0);if(!(dn>1.0&&df>0.05&&df<dn))return 6;if(nearNew.acceptedPowerW<=0.0||farNew.acceptedPowerW<=0.0||!nearNew.layeredSommerfeldExteriorWireSurfaceVectorUsed||!nearNew.layeredSommerfeldExteriorHedScalarDiagnosticEvaluated)return 7;if(nearNew.mutualReciprocityRelative>1e-10)return 8;if(std::abs(nearNew.feeds[0].inputImpedanceOhm-near2.feeds[0].inputImpedanceOhm)>1e-8*std::max(1.0,std::abs(nearNew.feeds[0].inputImpedanceOhm)))return 9;if(nearNew.acceptedPowerW>1e-15&&std::abs(near2.acceptedPowerW/nearNew.acceptedPowerW-4.0)>1e-7)return 10;std::cout<<"5.32 patch regression="<<z32<<"\n";std::cout<<"5.32 elevated near old/new="<<zn0<<" / "<<zn<<" delta="<<dn<<"\n";std::cout<<"5.32 elevated far old/new="<<zf0<<" / "<<zf<<" delta="<<df<<"\n";return 0;}
'''

with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    cpp = td/'verify532.cpp'; exe = td/'verify532'
    cpp.write_text(program, encoding='utf-8')
    cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-I'+str(ROOT/'include'),
         str(ROOT/'src/numerical_em_solvers.cpp'),str(ROOT/'src/pec_surface_mom.cpp'),str(ROOT/'src/antenna_surface_geometry.cpp'),
         str(ROOT/'src/hybrid_wire_surface_mom.cpp'),str(ROOT/'src/microstrip_models.cpp'),str(cpp),'-o',str(exe)]
    subprocess.run(cmd,check=True)
    subprocess.run([str(exe)],check=True,timeout=480)

print('QTsignalApp 5.32.0 exterior-height verification: PASS')
