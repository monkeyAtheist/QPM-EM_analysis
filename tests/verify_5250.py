from pathlib import Path
import json, re, subprocess, tempfile, hashlib

ROOT=Path(__file__).resolve().parents[1]
project=json.loads((ROOT/'QTsignalApp.qtproject.json').read_text(encoding='utf-8'))
main=(ROOT/'src/main.cpp').read_text(encoding='utf-8')
hybrid=(ROOT/'src/hybrid_wire_surface_mom.cpp').read_text(encoding='utf-8')
hybrid_h=(ROOT/'include/hybrid_wire_surface_mom.h').read_text(encoding='utf-8')
designer=(ROOT/'src/antenna_designer_widget.cpp').read_text(encoding='utf-8')
refs=(ROOT/'src/electromagnetism_reference_widget.cpp').read_text(encoding='utf-8')
bench=(ROOT/'src/antenna_validation_bench.cpp').read_text(encoding='utf-8')
change=(ROOT/'CHANGELOG_APP.md').read_text(encoding='utf-8')

checks={
    'version': project.get('version')=='5.25.0' and project.get('packaging',{}).get('productVersion')=='5.25.0' and 'QStringLiteral("5.25.0")' in main,
    'layered api': all(x in hybrid_h for x in ['LayeredSlabQuasiStatic = 2','layeredSlabQuasiStaticUsed','layeredSlabRegionCount','layeredImageSeriesTerms']),
    'layered spectral model': all(x in hybrid for x in ['eps2*coth(kh)','layeredImageTermCount','layeredScalarGreenOverEpsilon','const C refl=(epsr-C{1.0,0.0})/(epsr+C{1.0,0.0})']),
    'residual correction': all(x in hybrid for x in ['const C dynamicBaseline','correctionStrength=std::clamp(1.0-slab.fieldFillFactor','direct 1/R singular coefficient','layeredStatic-effectiveStatic']),
    'no pec image': 'A PEC image plane in the dielectric kernel would therefore impose' in hybrid and 'conductor boundary twice' in hybrid,
    'rwg integration': hybrid.count('layeredScalarGreenOverEpsilon(input,') >= 3 and 'surfaceVectorGreen(input,' in hybrid,
    'ui kernel': all(x in designer for x in ['Layered slab image / fringing (quasi-static)','DielectricKernelModel::LayeredSlabQuasiStatic','Layered dielectric correction: %1 slab region(s)']),
    'patch presets': designer.count('m_hybridDielectricKernel->setCurrentIndex(2)') >= 2,
    'persistence': 'root.insert(QStringLiteral("hybridDielectricKernel")' in designer and 'root.contains(QStringLiteral("hybridDielectricKernel"))' in designer,
    'benchmark': bench.count('DielectricKernelModel::LayeredSlabQuasiStatic') >= 2 and 'residual quasi-static finite-slab dielectric image/fringing correction' in bench,
    'references': all(x in refs for x in ['Finite layered-slab image / fringing correction (5.25)','No PEC image plane is added','Sommerfeld TE/TM spectral Green tensor']),
    'changelog': change.startswith('# QTsignalApp 5.25.0') and 'Finite-slab dielectric image / fringing correction' in change and 'approximately `4.93%`' in change,
    'qta unchanged': 'root.insert(QStringLiteral("version"), 19)' in designer,
    '524 port retained': all(x in hybrid for x in ['+=0.5*t.integratedDivergenceM','-=0.5*t.integratedDivergenceM','ip=-0.5*(ipos-ineg)']),
}
failed=[k for k,v in checks.items() if not v]
if failed:
    raise SystemExit('QTsignalApp 5.25.0 static verification FAILED: '+', '.join(failed))

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
struct PatchGeometry { double f0=0,h=0,er=4.2,effectiveEr=1,width=0,length=0,groundW=0,groundL=0,feedY=0; };

PatchGeometry patchGeometry(double f0)
{
    PatchGeometry g;g.f0=f0;const double lambda=C0/f0;g.h=std::min(0.02*lambda,0.0016);
    const auto e=MicrostripModels::rectangularPatch(f0,g.h,g.er);
    if(e.valid){g.width=e.widthM;g.length=e.physicalLengthM;g.effectiveEr=e.effectivePermittivity;}
    g.groundW=std::max(1.35*g.width,g.width+12.0*g.h);
    g.groundL=std::max(1.35*g.length,g.length+12.0*g.h);
    g.feedY=-0.22*g.length;return g;
}

HybridWireSurfaceMom::Input patchSurface(const PatchGeometry&g,double solveFrequencyHz,bool fine)
{
    HybridWireSurfaceMom::Input in;const double lambda0=C0/g.f0;
    in.wire.frequencyHz=solveFrequencyHz;in.wire.computeFarField=false;
    AntennaSurface::SurfaceSpec ground;ground.name="PATCH_GROUND";ground.kind=AntennaSurface::SurfaceKind::Rectangle;
    ground.center={0,0,0};ground.widthM=g.groundW;ground.heightM=g.groundL;ground.meshHintM=fine?lambda0/18.0:lambda0/12.0;
    AntennaSurface::SurfaceSpec patch;patch.name="PATCH";patch.kind=AntennaSurface::SurfaceKind::Rectangle;
    patch.center={0,0,g.h};patch.widthM=g.width;patch.heightM=g.length;patch.meshHintM=fine?lambda0/25.0:lambda0/16.0;
    const std::size_t perSurface=fine?96:64;
    const auto gm=AntennaSurface::triangulate(ground,perSurface),pm=AntennaSurface::triangulate(patch,perSurface);
    for(const auto&t:gm)in.triangles.push_back({t.a,t.b,t.c,0});
    for(const auto&t:pm)in.triangles.push_back({t.a,t.b,t.c,1});
    HybridWireSurfaceMom::DifferentialSurfaceFeed sf;sf.name="PATCH_LUMPED_PORT";
    sf.positivePositionM={0,g.feedY,g.h};sf.negativePositionM={0,g.feedY,0};sf.voltageV={1,0};sf.referenceOhm=50;
    sf.positiveSurfaceIndex=1;sf.negativeSurfaceIndex=0;sf.mappingToleranceM=std::max(g.width,g.length)/3.0;sf.footprintRadiusM=0.08*lambda0;
    in.differentialSurfaceFeeds.push_back(sf);
    HybridWireSurfaceMom::DielectricRegion d;d.name="PATCH_SUBSTRATE";d.centerM={0,0,0.5*g.h};d.widthM=g.groundW;d.heightM=g.groundL;d.thicknessM=g.h;
    d.relativePermittivity=g.er;d.lossTangent=.02;d.fieldFillFactor=g.er>1?std::clamp((g.effectiveEr-1)/(g.er-1),0.0,1.0):0;
    in.dielectrics.push_back(d);in.maxTotalUnknowns=520;in.maxSurfaceUnknowns=420;in.computeFarField=false;in.enableTerminalHalfRwg=false;
    in.useEffectiveDielectricRegions=true;in.dielectricKernelModel=HybridWireSurfaceMom::DielectricKernelModel::LayeredSlabQuasiStatic;return in;
}

HybridWireSurfaceMom::Input layeredTwoPlate(double voltage)
{
    HybridWireSurfaceMom::Input in;in.wire.frequencyHz=30e6;in.wire.computeFarField=false;in.computeFarField=false;
    const double s=.12,h=.01;AntennaSurface::SurfaceSpec a;a.kind=AntennaSurface::SurfaceKind::Rectangle;
    a.widthM=2*s;a.heightM=2*s;a.center={0,0,0};a.meshHintM=.04;AntennaSurface::SurfaceSpec b=a;b.center={0,0,h};
    const auto am=AntennaSurface::triangulate(a,72),bm=AntennaSurface::triangulate(b,72);
    for(const auto&t:am)in.triangles.push_back({t.a,t.b,t.c,0});
    for(const auto&t:bm)in.triangles.push_back({t.a,t.b,t.c,1});
    HybridWireSurfaceMom::DifferentialSurfaceFeed f;f.name="P";f.positivePositionM={0,0,h};f.negativePositionM={0,0,0};
    f.positiveSurfaceIndex=1;f.negativeSurfaceIndex=0;f.voltageV={voltage,0};f.mappingToleranceM=.03;f.footprintRadiusM=.05;in.differentialSurfaceFeeds.push_back(f);
    HybridWireSurfaceMom::DielectricRegion d;d.name="D";d.centerM={0,0,h/2};d.widthM=2*s;d.heightM=2*s;d.thicknessM=h;d.relativePermittivity=3;d.lossTangent=.01;d.fieldFillFactor=.65;in.dielectrics.push_back(d);
    in.maxSurfaceUnknowns=350;in.maxTotalUnknowns=400;in.enableTerminalHalfRwg=false;in.useEffectiveDielectricRegions=true;
    in.dielectricKernelModel=HybridWireSurfaceMom::DielectricKernelModel::LayeredSlabQuasiStatic;return in;
}

double relPercent(std::complex<double>a,std::complex<double>b){return 100.0*std::abs(a-b)/std::max(std::abs(b),1e-30);}
}

int main()
{
    const auto geom=patchGeometry(300e6);if(!(geom.width>0&&geom.length>0))return 2;
    const auto coarse=HybridWireSurfaceMom::solve(patchSurface(geom,300e6,false));
    const auto fine=HybridWireSurfaceMom::solve(patchSurface(geom,300e6,true));
    if(!coarse.valid||!fine.valid||coarse.feeds.empty()||fine.feeds.empty())return 3;
    const auto zc=coarse.feeds.front().inputImpedanceOhm,zf=fine.feeds.front().inputImpedanceOhm;
    const double dz=relPercent(zc,zf);if(!(dz>3.0&&dz<7.0))return 4;
    if(!(zf.real()>5&&zf.real()<20&&zf.imag()<-25&&zf.imag()>-65))return 5;
    if(!fine.layeredSlabQuasiStaticUsed||fine.layeredSlabRegionCount!=1||fine.layeredImageSeriesTerms<3||fine.layeredImageSeriesTerms>6)return 6;
    double bestFactor=1,bestAbsX=1e300;
    for(double factor:{0.85,0.90,0.95,1.00,1.05,1.10,1.15}){
        const auto r=HybridWireSurfaceMom::solve(patchSurface(geom,300e6*factor,false));if(!r.valid||r.feeds.empty())return 7;
        const double ax=std::abs(r.feeds.front().inputImpedanceOhm.imag());if(ax<bestAbsX){bestAbsX=ax;bestFactor=factor;}}
    if(!(bestFactor>=1.10&&bestFactor<=1.15+1e-12))return 8;

    const auto a=HybridWireSurfaceMom::solve(layeredTwoPlate(1));const auto b=HybridWireSurfaceMom::solve(layeredTwoPlate(2));
    if(!a.valid||!b.valid||a.feeds.empty()||b.feeds.empty())return 9;
    if(!a.layeredSlabQuasiStaticUsed||a.layeredSlabRegionCount!=1||a.layeredImageSeriesTerms<3||a.layeredImageSeriesTerms>6)return 10;
    if(!a.differentialPortSymmetricVoltageSplitUsed||!b.differentialPortSymmetricVoltageSplitUsed)return 11;
    if(!(a.feeds.front().inputImpedanceOhm.real()>0))return 12;
    if(std::abs(a.feeds.front().inputImpedanceOhm-b.feeds.front().inputImpedanceOhm)>1e-8*std::max(1.0,std::abs(a.feeds.front().inputImpedanceOhm)))return 13;
    if(a.acceptedPowerW>1e-15){const double ratio=b.acceptedPowerW/a.acceptedPowerW;if(std::abs(ratio-4)>1e-7)return 14;}

    const auto mono=AntennaValidation::runCase(AntennaValidation::CaseKind::QuarterWaveMonopole,300e6);
    if(mono.rows.size()!=2||!mono.rows[1].valid)return 15;
    const auto zm=mono.rows[1].inputImpedanceOhm;
    if(!(zm.real()>45&&zm.real()<80&&zm.imag()>20&&zm.imag()<60))return 16;
    std::cout<<"5.25 patch layered Gaussian port="<<zf<<" dZ="<<dz<<"% fminX/f0="<<bestFactor<<" verdict=FAIL\n"
             <<"5.25 layered two-plate Zin="<<a.feeds.front().inputImpedanceOhm<<" regions="<<a.layeredSlabRegionCount<<" terms="<<a.layeredImageSeriesTerms<<"\n"
             <<"5.25 monopole rooftop="<<zm<<"\n";return 0;
}
'''

with tempfile.TemporaryDirectory() as td:
    td=Path(td);cpp=td/'test5250.cpp';exe=td/'test5250';cpp.write_text(program,encoding='utf-8')
    cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-I'+str(ROOT/'include'),
         str(ROOT/'src/antenna_validation_bench.cpp'),str(ROOT/'src/numerical_em_solvers.cpp'),
         str(ROOT/'src/antenna_surface_geometry.cpp'),str(ROOT/'src/pec_surface_mom.cpp'),
         str(ROOT/'src/hybrid_wire_surface_mom.cpp'),str(ROOT/'src/microstrip_models.cpp'),str(cpp),'-o',str(exe)]
    subprocess.run(cmd,check=True)
    subprocess.run([str(exe)],check=True,timeout=420)

for src in ['src/pec_surface_mom.cpp','src/hybrid_wire_surface_mom.cpp','src/antenna_validation_bench.cpp']:
    with tempfile.TemporaryDirectory() as td:
        obj=Path(td)/'x.o'
        subprocess.run(['g++','-std=c++17','-O0','-Wall','-Wextra','-Wpedantic','-Werror','-I'+str(ROOT/'include'),'-c',str(ROOT/src),'-o',str(obj)],check=True)

# Electrostatic/magnetostatic kernels must remain byte-identical to the user-supplied 5.24 archive.
base=Path('/mnt/data/qtsignal_original/QTsignalApp')
for rel in ['src/electrostatic_model.cpp','include/electrostatic_model.h','src/magnetostatic_model.cpp','include/magnetostatic_model.h']:
    if hashlib.sha256((ROOT/rel).read_bytes()).digest()!=hashlib.sha256((base/rel).read_bytes()).digest():
        raise SystemExit('QTsignalApp 5.25.0 verification FAILED: static-field regression in '+rel)

# Qt is not installed in the Linux validation container; perform a lexical balance check
# on the two edited Qt translation units in addition to compiling all non-Qt kernels above.
def strip_cpp(t):
    t=re.sub(r'/\*.*?\*/','',t,flags=re.S);t=re.sub(r'//[^\n]*','',t)
    t=re.sub(r'R"[^\(]*\(.*?\)[^"]*"','""',t,flags=re.S)
    t=re.sub(r'"(?:\\.|[^"\\])*"','""',t);t=re.sub(r"'(?:\\.|[^'\\])*'","''",t)
    return t
for rel in ['src/antenna_designer_widget.cpp','src/electromagnetism_reference_widget.cpp']:
    t=strip_cpp((ROOT/rel).read_text(encoding='utf-8'))
    tb=strip_cpp((base/rel).read_text(encoding='utf-8'))
    for op,cl in [('(',')'),('[',']'),('{','}')]:
        if (t.count(op)-t.count(cl))!=(tb.count(op)-tb.count(cl)):
            raise SystemExit(f'QTsignalApp 5.25.0 verification FAILED: lexical regression {rel} {op}{cl}')

print('QTsignalApp 5.25.0 layered-substrate verification: PASS')
