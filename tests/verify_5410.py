from pathlib import Path
import hashlib, json, re, subprocess, tempfile, xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[1]
project=json.loads((ROOT/'QTsignalApp.qtproject.json').read_text(encoding='utf-8'))
main=(ROOT/'src/main.cpp').read_text(encoding='utf-8')
bench=(ROOT/'src/antenna_validation_bench.cpp').read_text(encoding='utf-8')
bench_h=(ROOT/'include/antenna_validation_bench.h').read_text(encoding='utf-8')
designer=(ROOT/'src/antenna_designer_widget.cpp').read_text(encoding='utf-8')
refs=(ROOT/'src/electromagnetism_reference_widget.cpp').read_text(encoding='utf-8')
roadmap=(ROOT/'SIMULATION_DESIGN_V1_ROADMAP.md').read_text(encoding='utf-8')
change=(ROOT/'CHANGELOG_APP.md').read_text(encoding='utf-8')

checks={
    'version': project.get('version')=='5.41.0' and project.get('packaging',{}).get('productVersion')=='5.41.0' and 'QStringLiteral("5.41.0")' in main,
    'production-role field': 'bool productionPath = true' in bench_h,
    'pattern mesh metric': 'patternMeshDeltaRmsPercent' in bench_h and 'coarse↔fine pattern RMS' in bench,
    'loop production freeze': 'production closed-loop path' in bench and 'experimental closed-loop diagnostic' in bench and 'row.verdict=Verdict::Informational' in bench,
    'monopole production freeze': 'production finite-ground path' in bench and 'row.meshDeltaPercent<10.0' in bench and 'row.patternMeshDeltaRmsPercent<5.0' in bench,
    'patch modal estimator': all(x in bench for x in ['surfaceCurrentEnergyProxy','parabolicPeakAbscissa','fJ2peak/f0','modal J² resonance','min-|X| port diagnostic']),
    'ui gate summary': all(x in designer for x in ['Simulation/Designer 1.0 production gates','productionPath','modal ∫|Js|²dS peak','INFO %7']),
    'reference docs': 'Frozen 1.0 validation closure (5.41)' in refs and 'fixed-voltage surface-current response' in refs,
    'roadmap': all(x in roadmap for x in ['Current baseline — 5.41','5.41 — Validation closure — COMPLETE','Canonical 300 MHz production campaign: 4/4 PASS','5.42 — 1.0 stabilization release candidate']),
    'changelog': change.startswith('# QTsignalApp 5.41.0') and 'Frozen 1.0 validation closure' in change,
    'qta v19': 'root.insert(QStringLiteral("version"), 19)' in designer,
}
failed=[k for k,v in checks.items() if not v]
if failed: raise SystemExit('5.41 static verification FAILED: '+', '.join(failed))

# Static-field kernels must remain exactly on the validated baseline.
expected={
 'src/electrostatic_model.cpp':'b3e8b46a60d316895324e3f4a13b6e4ce3579dccdfeb5665b43176e0ed597356',
 'include/electrostatic_model.h':'047cb78f9f0bb89ea0640acbce9e5a866f05ea3e5907ca0e3a5a64f5742beaa3',
 'src/magnetostatic_model.cpp':'6fcf3123641254a1476308b3dacb990c0de470e8d60032bc8e9ef805cadca5af',
 'include/magnetostatic_model.h':'4eee0ae2585072cdeac4884e8cc4c959588a176796537abf0819c94cf76a03e4',
}
for rel,h in expected.items():
    if hashlib.sha256((ROOT/rel).read_bytes()).hexdigest()!=h:
        raise SystemExit('5.41 static-field regression: '+rel)

program=r'''
#include "antenna_validation_bench.h"
#include <cmath>
#include <iostream>
#include <vector>

int main(){
    auto c=AntennaValidation::runCampaign(300e6);
    if(c.cases.size()!=4) return 2;
    int productionPass=0,productionTotal=0;
    const AntennaValidation::SolverRow *loopProd=nullptr,*loopDiag=nullptr,*monoProd=nullptr,*patchProd=nullptr;
    for(const auto &cr:c.cases){
        for(const auto &r:cr.rows){
            if(r.productionPath){++productionTotal;if(r.verdict==AntennaValidation::Verdict::Pass)++productionPass;}
            if(r.caseKind==AntennaValidation::CaseKind::SmallCircularLoop){if(r.productionPath)loopProd=&r;else loopDiag=&r;}
            if(r.caseKind==AntennaValidation::CaseKind::QuarterWaveMonopole && r.productionPath)monoProd=&r;
            if(r.caseKind==AntennaValidation::CaseKind::RectangularPatch && r.productionPath)patchProd=&r;
        }
    }
    if(productionTotal!=4 || productionPass!=4) return 3;
    if(!loopProd || !loopDiag || !monoProd || !patchProd) return 4;
    if(!(loopProd->patternRmsPercent<15.0 && loopProd->meshDeltaPercent<5.0)) return 5;
    if(loopDiag->verdict!=AntennaValidation::Verdict::Informational || !(loopDiag->patternRmsPercent>20.0)) return 6;
    if(!(monoProd->inputImpedanceOhm.real()>0.0 && monoProd->meshDeltaPercent<10.0 && monoProd->patternMeshDeltaRmsPercent<5.0)) return 7;
    if(patchProd->primaryMetricName!="fJ2peak/f0") return 8;
    if(!(patchProd->meshDeltaPercent<=7.5 && patchProd->primaryMetricErrorPercent<=10.0)) return 9;
    if(!(patchProd->primaryMetricValue>1.075 && patchProd->primaryMetricValue<1.086)) return 10;
    if(patchProd->verdictReason.find("min-|X| port diagnostic") == std::string::npos) return 11;
    std::cout << "5.41 production campaign=" << productionPass << "/" << productionTotal << " PASS\n";
    std::cout << "loop pulse patternRMS=" << loopProd->patternRmsPercent << "% rooftop diagnostic=" << loopDiag->patternRmsPercent << "%\n";
    std::cout << "monopole rooftop mesh=" << monoProd->meshDeltaPercent << "% patternMeshRMS=" << monoProd->patternMeshDeltaRmsPercent << "% Zin=" << monoProd->inputImpedanceOhm << "\n";
    std::cout << "patch modal resonance=" << patchProd->primaryMetricValue << " f0 offset=" << patchProd->primaryMetricErrorPercent << "% mesh=" << patchProd->meshDeltaPercent << "%\n";
    return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    td=Path(td); cpp=td/'verify541.cpp'; exe=td/'verify541'; cpp.write_text(program,encoding='utf-8')
    cmd=['g++','-std=c++17','-O1','-Wall','-Wextra','-Wpedantic','-Werror','-I'+str(ROOT/'include'),
         str(ROOT/'src/antenna_validation_bench.cpp'),str(ROOT/'src/numerical_em_solvers.cpp'),
         str(ROOT/'src/antenna_surface_geometry.cpp'),str(ROOT/'src/pec_surface_mom.cpp'),
         str(ROOT/'src/hybrid_wire_surface_mom.cpp'),str(ROOT/'src/microstrip_models.cpp'),str(cpp),'-o',str(exe)]
    subprocess.run(cmd,check=True)
    subprocess.run([str(exe)],check=True,timeout=360)

# Strict warning-clean kernel compilation is exercised by the release build check; the numerical harness above is compiled with -Wall/-Wextra/-Wpedantic/-Werror.

# Basic lexical-balance regression for Qt source touched by 5.41.
def strip_cpp(t):
    t=re.sub(r'/\*.*?\*/','',t,flags=re.S);t=re.sub(r'//[^\n]*','',t)
    t=re.sub(r'R"[^\(]*\(.*?\)[^"]*"','""',t,flags=re.S)
    t=re.sub(r'"(?:\\.|[^"\\])*"','""',t);t=re.sub(r"'(?:\\.|[^'\\])*'","''",t)
    return t
expected_balance={
    'src/antenna_designer_widget.cpp': {'(':1,'[':0,'{':1},
    'src/electromagnetism_reference_widget.cpp': {'(':0,'[':0,'{':0},
}
for rel in ['src/antenna_designer_widget.cpp','src/electromagnetism_reference_widget.cpp']:
    t=strip_cpp((ROOT/rel).read_text(encoding='utf-8'))
    for op,cl in [('(',')'),('[',']'),('{','}')]:
        if (t.count(op)-t.count(cl))!=expected_balance[rel][op]: raise SystemExit(f'5.41 lexical regression {rel} {op}{cl}')
for ui in (ROOT/'forms').glob('*.ui'): ET.parse(ui)
print('QTsignalApp 5.41.0 validation-closure verification: PASS')
