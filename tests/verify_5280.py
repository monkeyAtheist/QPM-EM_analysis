from pathlib import Path
import json, re, subprocess, tempfile, hashlib, xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
BASE = Path('/mnt/data/qtsignal_527')
project = json.loads((ROOT/'QTsignalApp.qtproject.json').read_text(encoding='utf-8'))
main = (ROOT/'src/main.cpp').read_text(encoding='utf-8')
designer = (ROOT/'src/antenna_designer_widget.cpp').read_text(encoding='utf-8')
num = (ROOT/'src/numerical_em_solvers.cpp').read_text(encoding='utf-8')
hyb = (ROOT/'src/hybrid_wire_surface_mom.cpp').read_text(encoding='utf-8')
plot_h = (ROOT/'include/widgets/field_profile_plot.h').read_text(encoding='utf-8')
plot_cpp = (ROOT/'src/field_profile_plot.cpp').read_text(encoding='utf-8')
rf_smith = (ROOT/'include/widgets/rf_smith_chart_widget.h').read_text(encoding='utf-8')
fdtd = (ROOT/'src/fdtd_workspace.cpp').read_text(encoding='utf-8')
refs = (ROOT/'src/electromagnetism_reference_widget.cpp').read_text(encoding='utf-8')
change = (ROOT/'CHANGELOG_APP.md').read_text(encoding='utf-8')

checks = {
    'version': project.get('version') == '5.28.0' and project.get('packaging',{}).get('productVersion') == '5.28.0' and 'QStringLiteral("5.28.0")' in main,
    'global normalization wire': 'updatePeakFromCut(azRaw,out.azimuthDeg,true)' in num and 'globalMax' in num,
    'hybrid theta convention': all(x in hyb for x in ['populateFarFieldResult', 'theta = 0 deg toward +Z', 'out.elevationDeg.push_back(thetaDeg)', 'updatePeakFromCut(elRaw,out.elevationDeg,false)']),
    '3d gain semantics': all(x in designer for x in ['Gmax ≈ %2 dBi', 'Realized gain ≈ %1 dBi', 'Gain (dBi)', 'm_peakGainDbi', 'm_peakRealizedGainDbi']),
    'raw hover values': 'QVector<double> hoverValues' in plot_h and 's.hoverValues.size() == s.values.size()' in plot_cpp and 'vswrSeries.hoverValues = vswrRaw' in designer,
    'smith interaction': all(x in designer for x in ['Hover any sweep point to inspect Γ, normalized impedance, return loss and VSWR.']) and 'm_hoverIndex' in rf_smith and 'Hover the trace for frequency' in rf_smith and 'hover original trace for values' in fdtd,
    'sweep limitation explicit': all(x in designer for x in ['Thin-wire antenna frequency sweep', 'WARNING: this sweep is thin-wire only']) and 'PEC/RWG surfaces and dielectric regions are intentionally not included in this sweep path yet' in refs,
    'reference radiation': 'Since 5.28, azimuth, elevation and 3D radiation views share one global normalization reference' in refs and 'G(θ,φ)=η<sub>rad</sub>D(θ,φ)' in refs,
    'changelog': change.startswith('# QTsignalApp 5.28.0') and 'Radiation/result audit and convention corrections' in change and '.qta` remains version 19' in change,
    'qta': 'root.insert(QStringLiteral("version"), 19)' in designer,
}
failed = [k for k,v in checks.items() if not v]
if failed:
    raise SystemExit('5.28 static verification FAILED: ' + ', '.join(failed))

program = r'''
#include "numerical_em_solvers.h"
#include "antenna_validation_bench.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>

int main(){
    constexpr double f = 300e6;
    const double lambda = NumericalEM::C0/f;
    const double length = 0.5*lambda;
    NumericalEM::WireNetworkMomInput in;
    in.frequencyHz=f;
    in.segmentsPerWavelength=220;
    in.maxUnknowns=700;
    in.maxSegmentLengthRadiusFactor=3.0;
    in.junctionLocalSubdivisions=3;
    in.currentBasisTreatment=NumericalEM::WireCurrentBasisTreatment::LinearRooftopCharge;
    in.junctionTreatment=NumericalEM::WireJunctionTreatment::ReducedBasis;
    in.computeFarField=true;
    in.farFieldCutStepDeg=2.0;
    in.farFieldIntegrationStepDeg=5.0;
    const double radius=lambda/1000.0;
    in.wires={{"DIPOLE_LOWER",{0,0,-0.5*length},{0,0,0},radius},
              {"DIPOLE_UPPER",{0,0,0},{0,0,0.5*length},radius}};
    in.feeds={{"CENTER_FEED",{0,0,0},{1.0,0.0},50.0}};
    auto r=NumericalEM::solveWireNetworkMom(in);
    if(!r.valid || r.feeds.size()!=1) return 2;
    const auto &fr=r.feeds.front();
    const std::complex<double> z0{fr.referenceOhm,0.0};
    const auto g=(fr.activeImpedanceOhm-z0)/(fr.activeImpedanceOhm+z0);
    if(std::abs(g-fr.reflectionCoefficient)>1e-12) return 3;
    const double gm=std::abs(g);
    const double rl=gm>1e-15?-20.0*std::log10(gm):300.0;
    const double vswr=gm<1.0?(1.0+gm)/(1.0-gm):std::numeric_limits<double>::infinity();
    if(std::abs(rl-fr.returnLossDb)>1e-10) return 4;
    if(std::isfinite(vswr) && std::abs(vswr-fr.vswr)>1e-10) return 5;
    if(!(r.directivityDbi>2.05 && r.directivityDbi<2.30)) return 6;
    auto maxv=[](const std::vector<double>&v){return v.empty()?0.0:*std::max_element(v.begin(),v.end());};
    const double normMax=std::max({maxv(r.azimuthNormalizedFarField),maxv(r.elevationNormalizedFarField),maxv(r.farFieldNormalized)});
    if(normMax<0.999999 || normMax>1.000001) return 7;
    for(double v:r.azimuthNormalizedFarField) if(v<0.0||v>1.000001) return 8;
    for(double v:r.elevationNormalizedFarField) if(v<0.0||v>1.000001) return 9;
    for(double v:r.farFieldNormalized) if(v<0.0||v>1.000001) return 10;

    auto mono=AntennaValidation::runCase(AntennaValidation::CaseKind::QuarterWaveMonopole,f);
    if(mono.rows.size()!=2 || !mono.rows[1].valid) return 11;
    const auto &mr=mono.rows[1];
    if(mr.patternAngleDeg.empty() || std::abs(mr.patternAngleDeg.front())>1e-9 || std::abs(mr.patternAngleDeg.back()-180.0)>1e-9) return 12;
    for(std::size_t i=1;i<mr.patternAngleDeg.size();++i) if(mr.patternAngleDeg[i] < mr.patternAngleDeg[i-1]) return 13;
    if(!(mr.patternRmsPercent>20.0 && mr.patternRmsPercent<40.0)) return 14;
    if(std::abs(mr.inputImpedanceOhm-std::complex<double>(62.1802,40.8726))>0.2) return 15;

    auto dip=AntennaValidation::runCase(AntennaValidation::CaseKind::HalfWaveDipole,f);
    if(dip.rows.size()!=2 || !dip.rows[0].valid || !dip.rows[1].valid) return 16;
    if(dip.rows[0].patternRmsPercent>2.0 || dip.rows[1].patternRmsPercent>2.0) return 17;

    std::cout << "5.28 dipole rooftop D=" << r.directivityDbi << " dBi Zin=" << fr.activeImpedanceOhm
              << " |Gamma|=" << gm << " RL=" << fr.returnLossDb << " VSWR=" << fr.vswr << "\n";
    std::cout << "5.28 monopole rooftop Zin=" << mr.inputImpedanceOhm << " D=" << mr.directivityDbi
              << " dBi patternRMS=" << mr.patternRmsPercent << "% theta=[" << mr.patternAngleDeg.front() << "," << mr.patternAngleDeg.back() << "]\n";
    return 0;
}
'''

with tempfile.TemporaryDirectory() as td:
    td=Path(td); cpp=td/'t.cpp'; exe=td/'t'; cpp.write_text(program)
    cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-I'+str(ROOT/'include'),
         str(ROOT/'src/antenna_validation_bench.cpp'),str(ROOT/'src/numerical_em_solvers.cpp'),
         str(ROOT/'src/antenna_surface_geometry.cpp'),str(ROOT/'src/pec_surface_mom.cpp'),
         str(ROOT/'src/hybrid_wire_surface_mom.cpp'),str(ROOT/'src/microstrip_models.cpp'),str(cpp),'-o',str(exe)]
    subprocess.run(cmd,check=True)
    subprocess.run([str(exe)],check=True,timeout=420)

# Strict compile the modified non-Qt kernels.
for src in ['src/numerical_em_solvers.cpp','src/hybrid_wire_surface_mom.cpp','src/antenna_validation_bench.cpp']:
    with tempfile.TemporaryDirectory() as td:
        subprocess.run(['g++','-std=c++17','-O0','-Wall','-Wextra','-Wpedantic','-Werror','-I'+str(ROOT/'include'),'-c',str(ROOT/src),'-o',str(Path(td)/'x.o')],check=True)

# Static-field models must remain byte-identical to 5.27.
for rel in ['src/electrostatic_model.cpp','include/electrostatic_model.h','src/magnetostatic_model.cpp','include/magnetostatic_model.h']:
    if hashlib.sha256((ROOT/rel).read_bytes()).digest()!=hashlib.sha256((BASE/rel).read_bytes()).digest():
        raise SystemExit('static-field regression '+rel)

# Basic lexical balance for Qt sources modified in this result-view pass.
def strip_cpp(t):
    t=re.sub(r'/\*.*?\*/','',t,flags=re.S); t=re.sub(r'//[^\n]*','',t)
    t=re.sub(r'R"[^\(]*\(.*?\)[^"]*"','""',t,flags=re.S)
    t=re.sub(r'"(?:\\.|[^"\\])*"','""',t); t=re.sub(r"'(?:\\.|[^'\\])*'","''",t)
    return t
for rel in ['src/antenna_designer_widget.cpp','src/field_profile_plot.cpp','src/fdtd_workspace.cpp','include/widgets/rf_smith_chart_widget.h','src/electromagnetism_reference_widget.cpp']:
    t=strip_cpp((ROOT/rel).read_text())
    b=strip_cpp((BASE/rel).read_text())
    for op,cl in [('(',')'),('[',']'),('{','}')]:
        if (t.count(op)-t.count(cl)) != (b.count(op)-b.count(cl)):
            raise SystemExit(f'lexical regression {rel} {op}{cl}: current={t.count(op)-t.count(cl)} baseline={b.count(op)-b.count(cl)}')
for ui in ROOT.rglob('*.ui'): ET.parse(ui)
print('QTsignalApp 5.28.0 result-audit verification: PASS')
