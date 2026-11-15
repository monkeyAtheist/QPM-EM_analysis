#!/usr/bin/env python3
"""6.12.0 portable contract + numeric regression. GUI test requires Qt 6.11/QPM."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
ink=(root/'src/notes_ink_canvas.cpp').read_text()
head=(root/'include/widgets/notes_ink_canvas.h').read_text()
ui=(root/'src/em_calculator_widget.cpp').read_text()
cmake=(root/'tests/qt_6700/CMakeLists.txt').read_text()
qpm=json.loads((root/'QTsignalApp.qtproject.json').read_text())
ids=re.search(r'const QStringList &symbolIds\(\).*?static const QStringList ids = \{(.*?)\};',ink,re.S)
assert ids,'symbol registry not found'
registry=re.findall(r'QStringLiteral\("([a-z0-9_]+)"\)',ids.group(1))
selector=re.search(r'// Circuit symbols remain vector annotations.*?connect\(symbolSelector,',ui,re.S)
assert selector,'symbol palette not found'
ui_ids=re.findall(r'addSymbolOption\(QStringLiteral\("[^"]+"\),QStringLiteral\("([a-z0-9_]+)"\)\);',selector.group(0))
checks={
    '6.12 app/QPM version': qpm['packaging']['productVersion']=='6.12.0' and 'QStringLiteral("6.12.0")' in (root/'src/main.cpp').read_text(),
    'palette and model agree: 27 ids':len(registry)==27 and len(ui_ids)==27 and registry==ui_ids and len(set(registry))==27,
    'vector symbol placement and selection':all(x in ink for x in ['bool NotesInkCanvas::addSymbol(', 'int NotesInkCanvas::symbolAt(', 'drawSymbol(p, symbol)', 'Tool::Component', 'SelectionKind::Symbol']),
    'symbol undo/redo exactly one case each':all(ink[ink.index(name):ink.index(next_name)].count('case Edit::Type::'+op+':')==1 for name,next_name in [('void NotesInkCanvas::undo()', 'void NotesInkCanvas::redo()'),('void NotesInkCanvas::redo()', 'void NotesInkCanvas::clearDocument()')] for op in ('AddSymbol','RemoveSymbol','ChangeSymbol')),
    'symbol rotation / scale / reference':all(x in head+ink+ui for x in ['rotateSelectedSymbol(', 'resizeSelectedSymbol(', 'renameSelectedSymbol(', 'notesSymbolSelector', 'notesSymbolSize', 'Snap 20 px']),
    'image interactive resize undo/redo':all(x in ink+head for x in ['m_resizingImage','resizeSelectedImage(', 'Type::ResizeImage']),
    'v1-v5 atomic schema and input guards':all(x in ink for x in ['root.insert(QStringLiteral("version"), 5)', 'version != 4 && version != 5', 'symbolEntries.size() > 400', 'm_pages = std::move(candidate)']),
    'legacy v4 backup + atomic save':all(x in ui for x in ['oldVersion <= 4', '.v%1.bak','QSaveFile file(path)']),
    'Qt Test registered': 'ink_61200.cpp' in cmake and (root/'tests/qt_6700/ink_61200.cpp').exists(),
    '.qta schema unchanged': 'constexpr int AntennaProjectSchemaVersion = 19;' in (root/'src/antenna_designer_widget.cpp').read_text(),
    'QPM paths valid':all((root/f).is_file() for f in (qpm['files']['sources']+qpm['files']['headers'])),
}
for label, ok in checks.items():
    print(('PASS' if ok else 'FAIL')+': '+label)
if not all(checks.values()): raise SystemExit('6.12 portable contract failed')
frozen={
 'src/numerical_em_solvers.cpp':'8da57b9ed3630451dd834d9afdfa8363a1b729fa94701335b23e13dcb994316e',
 'src/pec_surface_mom.cpp':'989ac706950c05f9be3d0ece1e27c7860824c88a3a19f20a598117a2d4b541ca',
 'src/hybrid_wire_surface_mom.cpp':'89e8233779fa4c2db3a4f9036d377eecbf2a54970851b68c3c0de40dc4745210',
 'src/antenna_validation_bench.cpp':'6a55b04842395c49f15ef88b2c2f0445a5ab8ff16060b7a09cf147b2eb71e3bf',
 'src/electrostatic_model.cpp':'b3e8b46a60d316895324e3f4a13b6e4ce3579dccdfeb5665b43176e0ed597356',
 'src/magnetostatic_model.cpp':'6fcf3123641254a1476308b3dacb990c0de470e8d60032bc8e9ef805cadca5af',
}
for f,expected in frozen.items():
    actual=hashlib.sha256((root/f).read_bytes()).hexdigest()
    assert actual==expected, f+' changed'
print('PASS: all six frozen electromagnetic kernels byte-identical')
# Real native numerical regression (Qt free). The widget/pen GUI requires Qt target-machine tests.
with tempfile.TemporaryDirectory(prefix='qtsignalapp_61200_') as tmp:
    sources=['src/em_calculator_model.cpp','src/em_units_calculator_model.cpp','tests/release_calculator_6510.cpp']
    binary=Path(tmp)/'calculator_regression'
    subprocess.run(['g++','-std=c++17','-O1','-Wall','-Wextra','-Wpedantic','-Werror','-I'+str(root/'include'),
                   *(str(root/x) for x in sources),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('PASS: dimensional calculator numerical regression')
print('QTsignalApp 6.12.0 portable regression: PASS')
print('PENDING: full Qt 6.11 MinGW QPM build and ink_61200 Qt Test / Surface Pen')
