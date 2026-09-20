#!/usr/bin/env python3
from pathlib import Path
import hashlib, json, subprocess, tempfile

ROOT=Path(__file__).resolve().parents[1]
project=json.loads((ROOT/'QTsignalApp.qtproject.json').read_text(encoding='utf-8'))
main=(ROOT/'src/main.cpp').read_text(encoding='utf-8')
widget=(ROOT/'src/antenna_designer_widget.cpp').read_text(encoding='utf-8')
header=(ROOT/'include/widgets/antenna_designer_widget.h').read_text(encoding='utf-8')
plot=(ROOT/'src/field_profile_plot.cpp').read_text(encoding='utf-8')
roadmap=(ROOT/'SIMULATION_DESIGN_V1_ROADMAP.md').read_text(encoding='utf-8')
release=(ROOT/'RELEASE_6_0_1.md').read_text(encoding='utf-8')
change=(ROOT/'CHANGELOG_APP.md').read_text(encoding='utf-8')

checks={
    'version 6.0.1': project.get('version')=='6.0.1' and project.get('packaging',{}).get('productVersion')=='6.0.1' and 'QStringLiteral("6.0.1")' in main,
    'qta schema remains v19': 'constexpr int AntennaProjectSchemaVersion = 19;' in widget,
    'sweep excitation selector': 'm_sweepExcitationMode' in header and 'All configured feeds — preserve amplitudes/phases' in widget and 'Observed feed only — zero other impressed voltages' in widget,
    'multi-feed RF terminology': 'Active Γ' in widget and 'Driving Γ' in widget and 'not an N-port S11 matrix entry' in widget,
    'split dipole warning': 'split two-source topology; not a centre-fed dipole' in widget and 'Electrical-length check:' in widget and '0.475 λ' in widget,
    'coarse/boundary sweep guards': 'coarse sweep step' in widget and 'resonance is not bracketed by this sweep' in widget,
    'dB no SI prefix': 'unit.startsWith(QStringLiteral("dB")' in plot and '-235 mdB' in plot,
    'release docs': 'QTsignalApp 6.0.1' in release and '6.0.1 — dipole topology and sweep reporting maintenance' in change and '6.0.1 maintenance' in roadmap,
    'dipole examples': (ROOT/'examples/dipole_exact_halfwave_100MHz.qta').is_file() and (ROOT/'examples/dipole_resonant_start_100MHz.qta').is_file(),
}
# Validate the two example projects structurally, including the one-centre-feed topology.
for example_name, expected_lambda in [('dipole_exact_halfwave_100MHz.qta',0.5),('dipole_resonant_start_100MHz.qta',0.475)]:
    ex=json.loads((ROOT/'examples'/example_name).read_text(encoding='utf-8'))
    if ex.get('version')!=19 or len(ex.get('feeds',[]))!=1 or len(ex.get('wires',[]))!=2:
        raise SystemExit('QTsignalApp 6.0.1 invalid dipole example topology: '+example_name)
    w=ex['wires']; total=sum(((x['x2']-x['x1'])**2+(x['y2']-x['y1'])**2+(x['z2']-x['z1'])**2)**0.5 for x in w)
    lam=299792458.0/ex['designFrequencyHz']
    if abs(total/lam-expected_lambda)>1e-9 or abs(ex['feeds'][0]['x'])>1e-12 or abs(ex['feeds'][0]['y'])>1e-12 or abs(ex['feeds'][0]['z'])>1e-12:
        raise SystemExit('QTsignalApp 6.0.1 invalid dipole example dimensions/feed: '+example_name)

failed=[k for k,v in checks.items() if not v]
if failed:
    raise SystemExit('QTsignalApp 6.0.1 static verification FAILED: '+', '.join(failed))

# Guard the validated 6.0.0 physical kernels. 6.0.1 is intentionally UI/reporting only.
hashes={
 'src/numerical_em_solvers.cpp':'8da57b9ed3630451dd834d9afdfa8363a1b729fa94701335b23e13dcb994316e',
 'src/pec_surface_mom.cpp':'989ac706950c05f9be3d0ece1e27c7860824c88a3a19f20a598117a2d4b541ca',
 'src/hybrid_wire_surface_mom.cpp':'89e8233779fa4c2db3a4f9036d377eecbf2a54970851b68c3c0de40dc4745210',
 'src/antenna_validation_bench.cpp':'6a55b04842395c49f15ef88b2c2f0445a5ab8ff16060b7a09cf147b2eb71e3bf',
 'src/electrostatic_model.cpp':'b3e8b46a60d316895324e3f4a13b6e4ce3579dccdfeb5665b43176e0ed597356',
 'src/magnetostatic_model.cpp':'6fcf3123641254a1476308b3dacb990c0de470e8d60032bc8e9ef805cadca5af',
 'include/numerical_em_solvers.h':'be757e5b4c71a0bffd83699dc936b7a8b9a3f1139f1d2a9e89b02d4bbc26c560',
 'include/pec_surface_mom.h':'66071c3efff91219647fc42eb325259b1ff53a8a38eb0414ffe0875dec792b19',
 'include/hybrid_wire_surface_mom.h':'dda5c8c97e527a838ccb30d8c5655e9bbc0dca8b5ef489e5909153e16849eb66',
}
for rel,expected in hashes.items():
    got=hashlib.sha256((ROOT/rel).read_bytes()).hexdigest()
    if got!=expected:
        raise SystemExit(f'QTsignalApp 6.0.1 physical-kernel hash changed unexpectedly: {rel}\nexpected {expected}\ngot      {got}')

with tempfile.TemporaryDirectory(prefix='qtsig601_') as td:
    exe=Path(td)/'release_dipole_6010'
    cmd=['g++','-std=c++17','-O2','-Wall','-Wextra','-Wpedantic','-Werror',f'-I{ROOT/"include"}',
         str(ROOT/'tests/release_dipole_6010.cpp'),str(ROOT/'src/numerical_em_solvers.cpp'),'-o',str(exe)]
    subprocess.run(cmd,check=True,cwd=ROOT)
    subprocess.run([str(exe)],check=True,cwd=ROOT)

print('QTsignalApp 6.0.1 dipole-topology / sweep-reporting verification: PASS')
