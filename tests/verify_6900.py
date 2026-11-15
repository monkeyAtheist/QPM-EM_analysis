#!/usr/bin/env python3
"""6.9.0 notebook wiring, source contract and portable numerical regression.

Run Qt Test separately with Qt 6 SDK; this script does not compile Qt Widgets.
"""
from pathlib import Path
import hashlib
import json
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
config = json.loads((root / 'QTsignalApp.qtproject.json').read_text(encoding='utf8'))
ui = (root / 'src/em_calculator_widget.cpp').read_text(encoding='utf8')
ink = (root / 'src/notes_ink_canvas.cpp').read_text(encoding='utf8')
hdr = (root / 'include/widgets/notes_ink_canvas.h').read_text(encoding='utf8')
cmake = (root / 'tests/qt_6700/CMakeLists.txt').read_text(encoding='utf8')
checks = {
    '6.9 version in QPM/Qt': config['packaging']['productVersion'] in ('6.9.0', '6.10.0', '6.11.0') and
        any(f'QStringLiteral("{v}")' in (root/'src/main.cpp').read_text(encoding='utf8')
            for v in ('6.9.0', '6.10.0', '6.11.0')),
    'drawing registered in QPM': 'src/notes_ink_canvas.cpp' in config['files']['sources'] and
        'include/widgets/notes_ink_canvas.h' in config['files']['headers'],
    'page navigation and expanded live editor': all(x in ui for x in (
        'notesPageSelector', 'notesAddPage', 'notesDeletePage', 'notesExpandDrawing',
        'dialog.showMaximized()', 'expanded->addWidget(m_inkCanvas, 1)',
        'inkLayout->insertWidget(2, m_inkCanvas, 1)')),
    'typed text placed on drawing page': all(x in ink for x in (
        'getMultiLineText(', 'Tool::Text', 'editTextAt(', 'drawText(', 'textCount()')),
    'v1/v2 safe migration and bounded JSON': all(x in ink for x in (
        'version != 1 && version != 2 && version != 3', 'QVector<Page> candidate;',
        'm_pages = std::move(candidate);', '24 * 1024 * 1024')) and 'MaxPages = 32' in hdr,
    'legacy recovery and atomic files': '.v1.bak' in ui and 'QSaveFile file(path)' in ui,
    'Qt test targets': all(x in cmake for x in ('ink_6800.cpp', 'ink_6900.cpp', 'Qt6::Test')),
    'project format unchanged': 'constexpr int AntennaProjectSchemaVersion = 19;' in
        (root / 'src/antenna_designer_widget.cpp').read_text(encoding='utf8'),
    'all QPM production source files exist': all((root/p).is_file() for p in config['files']['sources']),
}
for name, ok in checks.items():
    print(('PASS' if ok else 'FAIL') + ': ' + name)
if not all(checks.values()): raise SystemExit('Failed source/config contract')

frozen = {
    'src/numerical_em_solvers.cpp': '8da57b9ed3630451dd834d9afdfa8363a1b729fa94701335b23e13dcb994316e',
    'src/pec_surface_mom.cpp': '989ac706950c05f9be3d0ece1e27c7860824c88a3a19f20a598117a2d4b541ca',
    'src/hybrid_wire_surface_mom.cpp': '89e8233779fa4c2db3a4f9036d377eecbf2a54970851b68c3c0de40dc4745210',
    'src/antenna_validation_bench.cpp': '6a55b04842395c49f15ef88b2c2f0445a5ab8ff16060b7a09cf147b2eb71e3bf',
    'src/electrostatic_model.cpp': 'b3e8b46a60d316895324e3f4a13b6e4ce3579dccdfeb5665b43176e0ed597356',
    'src/magnetostatic_model.cpp': '6fcf3123641254a1476308b3dacb990c0de470e8d60032bc8e9ef805cadca5af',
}
for source, digest in frozen.items():
    if hashlib.sha256((root/source).read_bytes()).hexdigest() != digest:
        raise SystemExit('FAIL: frozen EM solver changed: '+source)
print('PASS: six frozen EM solvers byte-for-byte unchanged')

compiler = ['g++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-I'+str(root/'include')]
with tempfile.TemporaryDirectory(prefix='qtsignal6900_') as directory:
    cases = [
        ('calculator + SI dimensions', ['src/em_calculator_model.cpp', 'src/em_units_calculator_model.cpp', 'tests/release_calculator_6600.cpp']),
        ('Laplace rail', ['src/laplace_rail_model.cpp', 'tests/release_laplace_630.cpp']),
        ('electrostatic fieldline RK4', ['tests/field_lines_6800.cpp']),
    ]
    for i, (name, srcs) in enumerate(cases):
        target = str(Path(directory)/f'case_{i}')
        subprocess.run(compiler + [str(root/s) for s in srcs] + ['-o',target],check=True,cwd=root)
        subprocess.run([target],check=True,cwd=root)
        print('PASS: C++17', name)
print('QTsignalApp 6.9.0 portable regression: PASS')
print('Windows QPM/Qt and Surface Pen GUI tests: NOT EXECUTED (Qt SDK required).')
