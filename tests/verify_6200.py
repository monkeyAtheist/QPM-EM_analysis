#!/usr/bin/env python3
from pathlib import Path
import hashlib
import json
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
project = json.loads((ROOT / 'QTsignalApp.qtproject.json').read_text(encoding='utf-8'))
main = (ROOT / 'src/main.cpp').read_text(encoding='utf-8')
mainwindow = (ROOT / 'src/mainwindow.cpp').read_text(encoding='utf-8')
widget = (ROOT / 'src/em_calculator_widget.cpp').read_text(encoding='utf-8')
widget_h = (ROOT / 'include/widgets/em_calculator_widget.h').read_text(encoding='utf-8')
designer = (ROOT / 'src/antenna_designer_widget.cpp').read_text(encoding='utf-8')
designer_h = (ROOT / 'include/widgets/antenna_designer_widget.h').read_text(encoding='utf-8')
model = (ROOT / 'src/em_calculator_model.cpp').read_text(encoding='utf-8')
readme = (ROOT / 'README_ENGINEERING_CALCULATOR.md').read_text(encoding='utf-8')
release = (ROOT / 'RELEASE_6_2_0.md').read_text(encoding='utf-8')
roadmap = (ROOT / 'SIMULATION_DESIGN_V1_ROADMAP.md').read_text(encoding='utf-8')
change = (ROOT / 'CHANGELOG_APP.md').read_text(encoding='utf-8')

checks = {
    'version 6.2.0': project.get('version') == '6.2.0' and project.get('packaging', {}).get('productVersion') == '6.2.0' and 'QStringLiteral("6.2.0")' in main,
    'top-level Calculator / Notes': 'QStringLiteral("Calculator / Notes")' in mainwindow and 'Notes' in widget and 'buildNotesTab' in widget,
    'notes editor': all(token in widget for token in ['QSettings', 'Open .txt', 'Save as .txt', 'Undo', 'Redo', 'Cut', 'Copy', 'Paste', 'Select all', 'calculatorNotes/plainText']),
    'notes autosave': 'm_notesSaveTimer' in widget and 'setInterval(600)' in widget and 'saveNotes()' in widget,
    'exponential UI': 'QStringLiteral("exp(")' in widget and 'QStringLiteral("e^(")' in widget and 'Exponential / decay' in widget,
    'exponential engine': 'if (id == "exp") return std::exp(a);' in model,
    'designer handoff button': 'Create in Antenna designer' in widget and 'setCreateAntennaHandler' in widget_h and 'createAntennaInDesigner' in widget,
    'designer handoff wiring': 'calculator->setCreateAntennaHandler' in mainwindow and 'createFromCalculator' in designer_h and 'AntennaDesignerWidget::createFromCalculator' in designer,
    'designer catalogue handoff': all(token in designer for token in ['ExactHalfWaveDipole', 'GroundPlaneQuarterWave', 'InvertedVee', 'Jpole', 'SlimJim', 'AxialModeHelix', 'Yagi3Element', 'RectangularPatch', 'ParabolicDish']),
    'feed-model warnings': all(token in designer for token in ['starter feed tap', 'single-point starter feed is not a complete balanced feed model', 'No feed is inserted automatically']),
    'undo before replace': 'pushGeometryHistory();' in designer and 'createFromCalculator' in designer,
    'docs': 'QTsignalApp 6.2.0' in release and 'Calculator / Notes' in readme and '6.2.0 post-1.0 workflow tools' in roadmap and '6.2.0 — Calculator / Notes' in change,
    'qta schema unaffected': '.qta v19' in release and '.qta v19' in roadmap,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('QTsignalApp 6.2.0 static verification FAILED: ' + ', '.join(failed))

# Freeze the validated physical kernels; 6.2.0 changes workflow/UI/geometry construction only.
hashes = {
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
for rel, expected in hashes.items():
    got = hashlib.sha256((ROOT / rel).read_bytes()).hexdigest()
    if got != expected:
        raise SystemExit(f'QTsignalApp 6.2.0 physical-kernel hash changed unexpectedly: {rel}\nexpected {expected}\ngot      {got}')

with tempfile.TemporaryDirectory(prefix='qtsig620_') as td:
    exe = Path(td) / 'release_calculator_6200'
    cmd = [
        'g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
        f'-I{ROOT / "include"}',
        str(ROOT / 'src/em_calculator_model.cpp'),
        str(ROOT / 'tests/release_calculator_6200.cpp'),
        '-o', str(exe)
    ]
    subprocess.run(cmd, check=True, cwd=ROOT)
    subprocess.run([str(exe)], check=True, cwd=ROOT)

print('QTsignalApp 6.2.0 Calculator/Notes/Designer-handoff verification: PASS')
