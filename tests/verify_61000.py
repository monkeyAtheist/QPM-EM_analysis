#!/usr/bin/env python3
"""6.10.0 portable notes technical drawing contract + inherited numeric regressions.
Qt widgets GUI / tablet tests must run with the installed Qt6 SDK separately.
"""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
ui = (root/'src/em_calculator_widget.cpp').read_text(encoding='utf8')
ink = (root/'src/notes_ink_canvas.cpp').read_text(encoding='utf8')
hdr = (root/'include/widgets/notes_ink_canvas.h').read_text(encoding='utf8')
cmake = (root/'tests/qt_6700/CMakeLists.txt').read_text(encoding='utf8')
main = (root/'src/main.cpp').read_text(encoding='utf8')
qpm = json.loads((root/'QTsignalApp.qtproject.json').read_text(encoding='utf8'))
checks = {
    '6.10.0 version': qpm['packaging']['productVersion'] in ('6.10.0', '6.11.0') and any('QStringLiteral("'+v+'")' in main for v in ('6.10.0', '6.11.0')),
    'notes toolbar tools exposed': all(f'tool->addItem(QStringLiteral("{t}"))' in ui for t in
                                  ('Pen', 'Eraser', 'Text', 'Select / move', 'Line', 'Arrow', 'Rectangle', 'Ellipse / circle')),
    'vector shape drawing and editable selection': all(x in ink for x in
        ('drawShape(', 'addShape(', 'beginSelection(', 'moveSelection(', 'Tool::Select', 'm_shapeDrawing')),
    'move undo/redo and erase shapes': all(x in ink for x in
        ('Type::MoveStroke', 'Type::MoveText', 'Type::MoveShape', 'Type::RemoveShape', 'Type::Clear')),
    'Shift constrained geometries': 'bool constrain' in hdr and 'Qt::ShiftModifier' in ink,
    'v1/v2/v3 safe migration': 'version != 1 && version != 2 && version != 3' in ink and
                              'QVector<Page> candidate;' in ink and 'root.insert(QStringLiteral("version"), 4)' in ink,
    'bounded shapes and valid geometry': 'shapes.size() >= 500' in ink and 'shapeEntries.size() > 500' in ink,
    'sidecar backups and atomic save': '.v2.bak' in ui and '.v1.bak' in ui and 'QSaveFile file(path)' in ui,
    'Qt Tests in CMake': 'ink_61000.cpp' in cmake and 'ink_6900.cpp' in cmake,
    'schema v19 .qta untouched': 'constexpr int AntennaProjectSchemaVersion = 19;' in
                                (root/'src/antenna_designer_widget.cpp').read_text(encoding='utf8'),
    'QPM source files exist': all((root/x).is_file() for x in qpm['files']['sources']),
}
for name, passed in checks.items():
    print(('PASS' if passed else 'FAIL') + ': ' + name)
if not all(checks.values()):
    sys.exit('6.10 notes integration contract failed')
frozen = {
    'src/numerical_em_solvers.cpp': '8da57b9ed3630451dd834d9afdfa8363a1b729fa94701335b23e13dcb994316e',
    'src/pec_surface_mom.cpp': '989ac706950c05f9be3d0ece1e27c7860824c88a3a19f20a598117a2d4b541ca',
    'src/hybrid_wire_surface_mom.cpp': '89e8233779fa4c2db3a4f9036d377eecbf2a54970851b68c3c0de40dc4745210',
    'src/antenna_validation_bench.cpp': '6a55b04842395c49f15ef88b2c2f0445a5ab8ff16060b7a09cf147b2eb71e3bf',
    'src/electrostatic_model.cpp': 'b3e8b46a60d316895324e3f4a13b6e4ce3579dccdfeb5665b43176e0ed597356',
    'src/magnetostatic_model.cpp': '6fcf3123641254a1476308b3dacb990c0de470e8d60032bc8e9ef805cadca5af',
}
for name, expected in frozen.items():
    actual = hashlib.sha256((root/name).read_bytes()).hexdigest()
    if actual != expected: sys.exit('Frozen solver was modified: '+name)
print('PASS: frozen EM kernels unchanged')
subprocess.run([sys.executable, str(root/'tests/verify_6900.py')], check=True, cwd=root)
print('QTsignalApp 6.10+ inherited regression: PASS')
print('Qt Widgets/Surface Pen GUI validation: NOT RUN (requires Qt 6.11 on Windows/QPM).')
