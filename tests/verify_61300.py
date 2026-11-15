#!/usr/bin/env python3
"""6.13 portable release checks; Qt Widgets/UI and model QTest run on target Qt 6.11.

This suite does not claim to compile the Windows Qt GUI.
"""
from pathlib import Path
import hashlib, json, math, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
qpm=json.loads((root/'QTsignalApp.qtproject.json').read_text(encoding='utf-8'))
main=(root/'src/main.cpp').read_text(encoding='utf-8')
ink=(root/'src/notes_ink_canvas.cpp').read_text(encoding='utf-8')
hdr=(root/'include/widgets/notes_ink_canvas.h').read_text(encoding='utf-8')
ui=(root/'src/em_calculator_widget.cpp').read_text(encoding='utf-8')
mainwindow=(root/'src/mainwindow.cpp').read_text(encoding='utf-8')
electro=(root/'src/electrostatic_model.cpp').read_text(encoding='utf-8')
tests=(root/'tests/qt_6700/CMakeLists.txt').read_text(encoding='utf-8')
checks={
 'application/QPM version 6.13.0':qpm['packaging']['productVersion']=='6.13.0' and 'QStringLiteral("6.13.0")' in main,
 'QPM source/header paths exist':all((root/item).is_file() for item in qpm['files']['sources']+qpm['files']['headers']),
 'multi-object marquee and atomic undo':all(t in ink+hdr for t in ['collectMarquee(', 'm_groupSelection','MoveGroup','beforeGroup','afterGroup','selectAllObjects()']),
 'smart orthogonal wires follow stable symbol terminal IDs':all(t in ink+hdr for t in ['Tool::Wire','snapToTerminal(', 'anchoredWire(', 'pinLocation(','m_nextSymbolUid', 'drawPath(trace)']),
 'notebook schema v6 with v1-v5 migration':all(t in ink for t in ['root.insert(QStringLiteral("version"), 6)', 'version < 1 || version > 6','else item.uid=nextUid++', 'm_pages = std::move(candidate)']),
 'safe symbol deletion detaches anchored wires':all(t in ink for t in ['removeSymbolAt(', 'wire.aSymbol==edit.symbol.uid','wire.bSymbol==edit.symbol.uid','edit.afterShapes','edit.beforeShapes']),
 'v5 notebooks backed up before v6 save': 'oldVersion <= 5' in ui and 'QSaveFile file(path)' in ui,
 'wire tool and select all exposed': 'Smart wire (orthogonal)' in ui and 'QStringLiteral("Select all")' in ui,
 'Gauss law interpretation unambiguous': 'Q total' in mainwindow and '3Q/(4*pi*R^3)' in mainwindow and 'Selected source — density' in mainwindow,
 'physical model uses total charge and exact inside/outside': 'pointContribution(totalCharge, center, point)' in electro and 'CoulombK * totalCharge /' in electro,
 'new Qt tests registered':all((root/'tests/qt_6700'/name).exists() and name in tests for name in ('ink_61300.cpp','electrostatic_61300.cpp')),
 'antenna .qta v19 unchanged':'constexpr int AntennaProjectSchemaVersion = 19;' in (root/'src/antenna_designer_widget.cpp').read_text(),
}
for label,ok in checks.items(): print(('PASS' if ok else 'FAIL')+': '+label)
assert all(checks.values()),'6.13 static contract failed'

# The 6.12 production physical kernels must be byte-identical.
original=Path('/mnt/data/QTsignalApp_6.12.0_Electronic_Schematic_Notes.zip')
if original.is_file():
    import zipfile
    with zipfile.ZipFile(original) as z:
        for name in ('numerical_em_solvers.cpp','pec_surface_mom.cpp','hybrid_wire_surface_mom.cpp',
                     'antenna_validation_bench.cpp','electrostatic_model.cpp','magnetostatic_model.cpp'):
            src='QTsignalApp/src/'+name
            expected=hashlib.sha256(z.read(src)).hexdigest()
            actual=hashlib.sha256((root/'src'/name).read_bytes()).hexdigest()
            assert actual==expected,name+' changed from 6.12!'
    print('PASS: six 6.12 EM model kernels SHA-256 unchanged')

# Analytic independent reference for a uniformly charged solid sphere, Q=1 C / R=1m.
k=8.9875517923e9
Q=1.0; R=1.0
for distance,expected in ((0.0,0.0),(0.5,k*0.5),(1.0,k),(2.0,k/4.0)):
    E=k*Q*distance/R**3 if distance<R else k*Q/distance**2
    assert math.isclose(E,expected,rel_tol=1e-12,abs_tol=1e-8)
QforDensity=4.0*math.pi/3.0
assert math.isclose(k*QforDensity/4.0,9.41174222814915e9,rel_tol=1e-12)
assert math.isclose(k/2.0,4.49377589615e9,rel_tol=1e-12)
print(f'PASS: sphere analytic Gauss-law [Q=1 C] E(2,0,0)={k/4/1e9:.9f} GV/m')
print(f'PASS: sphere analytic Gauss-law [rho=1 C/m^3] E(2,0,0)={k*QforDensity/4/1e9:.9f} GV/m')

# C++17 strict portable tests: dimensional calculator + electrostatic streamline integrator.
with tempfile.TemporaryDirectory(prefix='qta613_') as tmp:
    tmp=Path(tmp)
    def compile_run(exe,parts):
        output=tmp/exe
        args=['g++','-std=c++17','-O1','-Wall','-Wextra','-Wpedantic','-Werror',
              '-I'+str(root/'include'),*(str(root/part) for part in parts),'-o',str(output)]
        subprocess.run(args,check=True)
        subprocess.run([str(output)],check=True)
    compile_run('calculator',['src/em_calculator_model.cpp','src/em_units_calculator_model.cpp','tests/release_calculator_6510.cpp'])
    compile_run('fieldlines',['tests/field_lines_6800.cpp'])
print('PASS: portable C++ numerical regressions')
print('QTsignalApp 6.13.0 source, physics reference and portable numerical gates: PASS')
print('PENDING: Qt 6.11 QPM rebuild + ink_61300 and electrostatic_61300 Qt Tests')
