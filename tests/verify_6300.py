#!/usr/bin/env python3
from pathlib import Path
import hashlib
import json
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
project = json.loads((ROOT/'QTsignalApp.qtproject.json').read_text(encoding='utf-8'))
main = (ROOT/'src/main.cpp').read_text(encoding='utf-8')
mainwindow = (ROOT/'src/mainwindow.cpp').read_text(encoding='utf-8')
calc = (ROOT/'src/em_calculator_widget.cpp').read_text(encoding='utf-8')
calc_h = (ROOT/'include/widgets/em_calculator_widget.h').read_text(encoding='utf-8')
rail = (ROOT/'src/laplace_rail_widget.cpp').read_text(encoding='utf-8')
rail_model = (ROOT/'src/laplace_rail_model.cpp').read_text(encoding='utf-8')
release = (ROOT/'RELEASE_6_3_0.md').read_text(encoding='utf-8')
readme = (ROOT/'README_ENGINEERING_CALCULATOR.md').read_text(encoding='utf-8')

checks = {
    'version 6.3.0': project.get('version')=='6.3.0' and project.get('packaging',{}).get('productVersion')=='6.3.0' and 'QStringLiteral("6.3.0")' in main,
    'project files': all(x in project['files']['sources'] for x in ['src/laplace_rail_model.cpp','src/laplace_rail_widget.cpp']) and all(x in project['files']['headers'] for x in ['include/laplace_rail_model.h','include/widgets/laplace_rail_widget.h']),
    'laplace tab': 'QStringLiteral("Laplace rail")' in calc and 'buildLaplaceRailTab' in calc_h and 'LaplaceRailWidget' in calc,
    'interactive graph': all(x in rail for x in ['mouseMoveEvent','mousePressEvent','setHoverHandler','QSlider','setSelectedTime']),
    'three regimes': all(x in rail_model for x in ['ConstantCurrent','VoltageDriven','GeneratorBraking','std::exp']),
    'calculator templates': 'Laplace force on straight conductor' in calc and 'Motional EMF on moving rod' in calc,
    'practice': all(x in rail for x in ['New exercise','Check','m_expectedExerciseAnswer']),
    'docs': 'QTsignalApp 6.3.0' in release and '## Laplace rail' in readme,
}
failed=[k for k,v in checks.items() if not v]
if failed: raise SystemExit('QTsignalApp 6.3.0 static verification FAILED: '+', '.join(failed))

# Reference fixtures must be explicit Hybrid v19 examples with a PEC-referenced RF terminal and ground junction.
for fname in ['RFMTA010504NNRB002_reference_PCB_edge.qta','RFMTA010504NNRB002_reference_PCB_center.qta']:
    d=json.loads((ROOT/'examples'/fname).read_text(encoding='utf-8'))
    assert d['version']==19 and d['hybridPortMode']==1 and d['hybridPortFeed']=='RF_terminal' and d['hybridJunctionFeed']=='GND_terminal'
    assert len(d['surfaces'])==1 and len(d['dielectrics'])==1 and d['sweepSolverIndex']==1

# Guard frozen production kernels.
hashes={
 'src/numerical_em_solvers.cpp':'8da57b9ed3630451dd834d9afdfa8363a1b729fa94701335b23e13dcb994316e',
 'src/pec_surface_mom.cpp':'989ac706950c05f9be3d0ece1e27c7860824c88a3a19f20a598117a2d4b541ca',
 'src/hybrid_wire_surface_mom.cpp':'89e8233779fa4c2db3a4f9036d377eecbf2a54970851b68c3c0de40dc4745210',
 'src/antenna_validation_bench.cpp':'6a55b04842395c49f15ef88b2c2f0445a5ab8ff16060b7a09cf147b2eb71e3bf',
 'src/electrostatic_model.cpp':'b3e8b46a60d316895324e3f4a13b6e4ce3579dccdfeb5665b43176e0ed597356',
 'src/magnetostatic_model.cpp':'6fcf3123641254a1476308b3dacb990c0de470e8d60032bc8e9ef805cadca5af',
}
for rel,expected in hashes.items():
    got=hashlib.sha256((ROOT/rel).read_bytes()).hexdigest()
    if got!=expected: raise SystemExit(f'physical-kernel hash changed unexpectedly: {rel}\nexpected {expected}\ngot {got}')

with tempfile.TemporaryDirectory(prefix='qtsig630_') as td:
    td=Path(td)
    cexe=td/'calculator'
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Wpedantic','-Werror',f'-I{ROOT/"include"}',str(ROOT/'src/em_calculator_model.cpp'),str(ROOT/'tests/release_calculator_6300.cpp'),'-o',str(cexe)],check=True,cwd=ROOT)
    subprocess.run([str(cexe)],check=True,cwd=ROOT)
    rexe=td/'laplace'
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Wpedantic','-Werror',f'-I{ROOT/"include"}',str(ROOT/'src/laplace_rail_model.cpp'),str(ROOT/'tests/release_laplace_630.cpp'),'-o',str(rexe)],check=True,cwd=ROOT)
    subprocess.run([str(rexe)],check=True,cwd=ROOT)

print('QTsignalApp 6.3.0 Laplace-rail/reference-fixture verification: PASS')
