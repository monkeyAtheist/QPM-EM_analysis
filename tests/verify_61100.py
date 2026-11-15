#!/usr/bin/env python3
"""QTsignalApp 6.11.0 notes viewport/image integration regression (portable).

Portable tests do not compile Qt GUI; run ink_61100 (Qt Test) with Qt 6.11/QPM.
"""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
ui = (root/'src/em_calculator_widget.cpp').read_text(encoding='utf-8')
ink = (root/'src/notes_ink_canvas.cpp').read_text(encoding='utf-8')
hdr = (root/'include/widgets/notes_ink_canvas.h').read_text(encoding='utf-8')
cmake = (root/'tests/qt_6700/CMakeLists.txt').read_text(encoding='utf-8')
qpm = json.loads((root/'QTsignalApp.qtproject.json').read_text(encoding='utf-8'))
checks = {
    'QPM and application version 6.11.0': qpm['packaging']['productVersion']=='6.11.0' and
        'QStringLiteral("6.11.0")' in (root/'src/main.cpp').read_text(encoding='utf-8'),
    'zoom / fit / pan / touch available': all(x in ink for x in (
        'setZoomFactor(', 'fitPage()', 'beginPan(', 'movePan(', 'wheelEvent(QWheelEvent *event)',
        'm_touchPinching', 'Qt::MiddleButton')),
    'image insertion and move/edit controls': all(x in ui+ink+hdr for x in (
        'Insert image', 'QImageReader reader(path)', 'Tool::Hand', 'AddImage', 'RemoveImage', 'MoveImage')),
    'v4 schema / transactional import with legacy 1-3': all(x in ink for x in (
        'root.insert(QStringLiteral("version"), 4)', 'version != 1 && version != 2 && version != 3 && version != 4',
        'm_pages = std::move(candidate)', 'if (version == 4 && !imageValue.isArray())')),
    'bounded compressed PNG input and bitmap RAM': all(x in ink for x in (
        'png.size()>1500000', 'totalEmbeddedPixels', '40000000LL', 'bytes.toBase64()!=base64')),
    'annotations, undo/redo, export contain inserted images': all(x in ink for x in (
        'p.drawImage(', 'page().images=edit.previousImages', 'page().images.clear()',
        'edit.type=Edit::Type::AddImage', 'edit.type=Edit::Type::MoveImage')),
    'view changes do not call modified()': ('void NotesInkCanvas::setZoomFactor' in ink and
        'void NotesInkCanvas::fitPage()' in ink and
        'm_onViewChanged' in ink),
    'legacy ink backup v3': '.v3.bak' in ui and 'QSaveFile file(path)' in ui,
    'new Qt Test registered': 'ink_61100.cpp' in cmake and (root/'tests/qt_6700/ink_61100.cpp').is_file(),
    'QPM files remain complete': all((root/f).exists() for f in qpm['files']['sources']+qpm['files']['headers']),
    '.qta schema v19 unchanged': 'constexpr int AntennaProjectSchemaVersion = 19;' in
        (root/'src/antenna_designer_widget.cpp').read_text(encoding='utf-8'),
}
for title,ok in checks.items():
    print(('PASS' if ok else 'FAIL')+': '+title)
if not all(checks.values()): raise SystemExit('6.11 portable checks failed')
frozen = {
 'src/numerical_em_solvers.cpp':'8da57b9ed3630451dd834d9afdfa8363a1b729fa94701335b23e13dcb994316e',
 'src/pec_surface_mom.cpp':'989ac706950c05f9be3d0ece1e27c7860824c88a3a19f20a598117a2d4b541ca',
 'src/hybrid_wire_surface_mom.cpp':'89e8233779fa4c2db3a4f9036d377eecbf2a54970851b68c3c0de40dc4745210',
 'src/antenna_validation_bench.cpp':'6a55b04842395c49f15ef88b2c2f0445a5ab8ff16060b7a09cf147b2eb71e3bf',
 'src/electrostatic_model.cpp':'b3e8b46a60d316895324e3f4a13b6e4ce3579dccdfeb5665b43176e0ed597356',
 'src/magnetostatic_model.cpp':'6fcf3123641254a1476308b3dacb990c0de470e8d60032bc8e9ef805cadca5af',
}
for f, expected in frozen.items():
    if hashlib.sha256((root/f).read_bytes()).hexdigest()!=expected:
        raise SystemExit('Frozen EM kernel modified: '+f)
print('PASS: all six frozen EM kernels byte-identical')
subprocess.run([sys.executable, str(root/'tests/verify_61000.py')], cwd=root, check=True)
print('QTsignalApp 6.11.0 portable source and numerical checks: PASS')
print('Qt Widgets GUI / Surface Pen / QPM rebuild: PENDING Windows Qt6.11 target run')
