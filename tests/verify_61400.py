#!/usr/bin/env python3
"""Portable two-way charge/density contract, without Qt SDK; Qt tests remain target-only."""
from pathlib import Path
import hashlib
import json
import subprocess
import tempfile
import zipfile

root=Path(__file__).resolve().parents[1]
qpm=json.loads((root/'QTsignalApp.qtproject.json').read_text(encoding='utf-8'))
main=(root/'src/main.cpp').read_text(encoding='utf-8')
cpp=(root/'src/mainwindow.cpp').read_text(encoding='utf-8')
hdr=(root/'include/mainwindow.h').read_text(encoding='utf-8')
models=(root/'src/electrostatic_model.cpp').read_text(encoding='utf-8')
assert qpm['packaging']['productVersion']=='6.14.0'
assert 'setApplicationVersion(QStringLiteral("6.14.0"))' in main
assert 'include/electrostatic_charge_density.h' in qpm['files']['headers']
assert all((root/p).is_file() for p in qpm['files']['headers']+qpm['files']['sources'])
for label,token in {
    'Q input drives density':'updateChargeFromPrimary();',
    'Density input drives Q':'updateChargeFromDensity();',
    'Geometry changes resynchronize':'updateChargeAfterGeometryChange();',
    'Conversion controls visible':'sourceDensitySpin',
    'Density prefix controls':'densityUnitCombo',
    'Native strength SI precision':'source.strength = m_chargePrimarySI;',
    'Q and rho relation':'relation(chargeShape(',
    'No circular updates':'const QSignalBlocker bs(spin);',
    'No stale loader writes':'if (m_loadingEditor || m_selectedSource < 0)',
    'No finite total for infinite cylinder':'relation.primaryIsTotalCharge',
    'Live recalculation of E':'&MainWindow::applySourceEditor',
}.items():
    assert token in cpp, f'Missing: {label}'
assert 'm_densityIsDriving' in hdr
assert 'Q total' in cpp and 'Selected source — density' not in cpp
assert 'pointContribution(totalCharge, center, point)' in models

# Release baseline: every physical solver is untouched; only editor is changed.
base=Path('/mnt/data/QTsignalApp_6.13.1_Notes_Compact_Drawing_UI.zip')
if base.is_file():
    with zipfile.ZipFile(base) as z:
        for name in ('numerical_em_solvers.cpp','pec_surface_mom.cpp','hybrid_wire_surface_mom.cpp',
                     'antenna_validation_bench.cpp','electrostatic_model.cpp','magnetostatic_model.cpp',
                     'notes_ink_canvas.cpp'):
            rel='src/'+name
            expected=hashlib.sha256(z.read('QTsignalApp/'+rel)).hexdigest()
            got=hashlib.sha256((root/rel).read_bytes()).hexdigest()
            assert got==expected, f'Unexpected change in {rel}'
    print('PASS: seven baseline solver/notes files byte-identical')

with tempfile.TemporaryDirectory(prefix='qta614_') as tmp:
    exe=Path(tmp)/'charge_density_test'
    compiler=['g++','-std=c++17','-O1','-Wall','-Wextra','-Wpedantic','-Werror',
              '-I'+str(root/'include'), str(root/'tests/charge_density_61400.cpp'), '-o',str(exe)]
    subprocess.run(compiler,check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: C++17 math and bidirectional SI units')
print('PASS: QPM 6.14 configuration and Qt editor wiring static contracts')
print('QTsignalApp 6.14.0 portable validation: PASS')
print('PENDING: Qt 6.11/MinGW build and Qt Test charge_density_61400 on the user target')
