#!/usr/bin/env python3
"""QTsignalApp 6.8.0: electrostatic fieldline regression, ink wiring and EM contract.

Does not claim to execute Qt/Surface-Pen tests without Qt 6 SDK; the Qt Test
suite is provided under tests/qt_6700/ for Windows/QPM integration.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
config = json.loads((root/'QTsignalApp.qtproject.json').read_text(encoding='utf-8'))
sources = config['files']['sources']
headers = config['files']['headers']
entry = (root/'src/main.cpp').read_text(encoding='utf-8')
ui = (root/'src/em_calculator_widget.cpp').read_text(encoding='utf-8')
canvas = (root/'src/electrostatic_canvas.cpp').read_text(encoding='utf-8')
ink = (root/'src/notes_ink_canvas.cpp').read_text(encoding='utf-8')
cmake = (root/'tests/qt_6700/CMakeLists.txt').read_text(encoding='utf-8')
checks = {
    '6.8.0 package and app version': config.get('version', config['packaging']['productVersion'])==config['packaging']['productVersion']=='6.8.0'
        and 'QStringLiteral("6.8.0")' in entry,
    'QPM registers the drawing implementation and header':
        'src/notes_ink_canvas.cpp' in sources
        and 'include/widgets/notes_ink_canvas.h' in headers
        and 'include/electrostatic_field_line_integrator.h' in headers,
    'all QPM production files exist': all((root/p).is_file() for p in sources + headers),
    'no legacy schema symbol leak': 'AntennaProjectMinimumSchemaVersion' not in
        (root/'src/antenna_designer_widget.cpp').read_text(encoding='utf-8'),
    'fieldline uses adaptive polarity-aware integration': all(t in canvas for t in
        ['ElectrostaticFieldLines', 'source.strength > 0.0 ? +1.0 : -1.0',
         'trace(seed, direction, bounds, stops, sampler, settings)']),
    'ink saved via atomic sidecar and global AppData': all(t in ui for t in
        ['.notes.ink.json', 'QStandardPaths::AppDataLocation', 'QSaveFile file(path)',
         'loadInkNotes()', 'saveInkNotes()', 'Drawing (pen)', 'Copy image', 'Export PNG']),
    'surface stylus / mouse / touch handling': all(t in ink for t in
        ['tabletEvent(QTabletEvent *event)', 'mousePressEvent(QMouseEvent *event)',
         'Qt::WA_AcceptTouchEvents', 'event->pressure()', 'QEvent::TouchBegin']),
    'ink JSON validation before commit': ink.index('QVector<Stroke> candidate;') <
        ink.index('m_strokes = std::move(candidate);'),
    'Qt ink tests and updated calculator test dependencies':
        'src/notes_ink_canvas.cpp' in cmake and 'ink_6800.cpp' in cmake,
    'project schema stays v19': 'constexpr int AntennaProjectSchemaVersion = 19;' in
        (root/'src/antenna_designer_widget.cpp').read_text(encoding='utf-8'),
}
for name, ok in checks.items():
    print(f'{"PASS" if ok else "FAIL"}: {name}')
if not all(checks.values()): raise SystemExit(1)

frozen = {
    'src/numerical_em_solvers.cpp':'8da57b9ed3630451dd834d9afdfa8363a1b729fa94701335b23e13dcb994316e',
    'src/pec_surface_mom.cpp':'989ac706950c05f9be3d0ece1e27c7860824c88a3a19f20a598117a2d4b541ca',
    'src/hybrid_wire_surface_mom.cpp':'89e8233779fa4c2db3a4f9036d377eecbf2a54970851b68c3c0de40dc4745210',
    'src/antenna_validation_bench.cpp':'6a55b04842395c49f15ef88b2c2f0445a5ab8ff16060b7a09cf147b2eb71e3bf',
    'src/electrostatic_model.cpp':'b3e8b46a60d316895324e3f4a13b6e4ce3579dccdfeb5665b43176e0ed597356',
    'src/magnetostatic_model.cpp':'6fcf3123641254a1476308b3dacb990c0de470e8d60032bc8e9ef805cadca5af',
}
for path, expected in frozen.items():
    if hashlib.sha256((root/path).read_bytes()).hexdigest() != expected:
        raise SystemExit('FAIL frozen EM kernel '+path)
print('PASS: six certified EM kernels bit-for-bit unchanged')

with tempfile.TemporaryDirectory(prefix='qt6800_') as tmp:
    compiler=['g++','-std=c++17','-O1','-Wall','-Wextra','-Wpedantic','-Werror', '-I'+str(root/'include')]
    cases = [
      ('fieldline RK4 + source termination', ['tests/field_lines_6800.cpp']),
      ('6.6 scientific/units display compatibility', ['src/em_calculator_model.cpp','src/em_units_calculator_model.cpp','tests/release_calculator_6600.cpp']),
      ('Laplace rail compatibility', ['src/laplace_rail_model.cpp','tests/release_laplace_630.cpp']),
    ]
    for i,(name,paths) in enumerate(cases):
        output=str(Path(tmp)/f'run_{i}')
        subprocess.run(compiler + [str(root/p) for p in paths] + ['-o',output], check=True,cwd=root)
        subprocess.run([output], check=True,cwd=root)
        print('PASS:',name)
print('QTsignalApp 6.8.0 static and numerical regression: PASS')
print('Qt UI / Surface Pen pressure / Windows QPM rebuild: NOT EXECUTED (Qt 6 SDK/Windows required).')
