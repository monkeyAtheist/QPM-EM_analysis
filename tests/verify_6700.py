#!/usr/bin/env python3
"""6.7.0 production wiring, numeric regression, source-isolation and frozen-solver contract.

QtCore/QTest and Qt Widgets/QTest suites live in tests/qt_6700; they require Qt 6 SDK.
This script does not misrepresent source checks as executed GUI tests.
"""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys
import tempfile

root=Path(__file__).resolve().parents[1]
config=json.loads((root/'QTsignalApp.qtproject.json').read_text(encoding='utf-8'))
sources=config['files']['sources']
headers=config['files']['headers']
entry=(root/'src/main.cpp').read_text(encoding='utf-8')
designer=(root/'src/antenna_designer_widget.cpp').read_text(encoding='utf-8')
document=(root/'src/antenna_project_document.cpp').read_text(encoding='utf-8')
log=(root/'src/app_diagnostics.cpp').read_text(encoding='utf-8')
mainwindow=(root/'src/mainwindow.cpp').read_text(encoding='utf-8')
ui=(root/'src/em_calculator_widget.cpp').read_text(encoding='utf-8')

checks={
    'QPM and Qt app version': config['version']==config['packaging']['productVersion']=='6.7.0'
        and 'QStringLiteral("6.7.0")' in entry,
    'schema v19 persisted': 'constexpr int AntennaProjectSchemaVersion = 19;' in designer
        and 'kSchemaVersion = 19;' in (root/'include/antenna_project_document.h').read_text(encoding='utf-8'),
    'QtCore project validation extracted': all(x in document for x in
        ['bool checkAntennaProjectSchema(', 'QJsonObject migrateAntennaProjectToCurrent(',
         'bool validateAntennaProjectDocument('])
         and 'validateAntennaProjectDocument(' not in designer.split('QDoubleSpinBox *numberBox(',1)[0],
    'application calls extracted project contract':
        all('AntennaProjectDocument::'+fn+'(' in designer for fn in
            ['checkAntennaProjectSchema','migrateAntennaProjectToCurrent','validateAntennaProjectDocument']),
    'schema conversion bound before int narrowing': 'requested > AntennaProjectSchemaVersion' in document
        and document.index('versionValue.toDouble() > static_cast<double>(AntennaProjectSchemaVersion)') < document.index('sourceVersion = static_cast<int>(versionValue.toDouble())'),
    'source modules declared in QPM': all(path in sources for path in
        ['src/app_diagnostics.cpp','src/antenna_project_document.cpp']) and all(path in headers for path in
        ['include/app_diagnostics.h','include/antenna_project_document.h']),
    'legacy source modules isolated from production': not any(path in sources for path in
        ['include/ElectroMagnetisme.cpp','src/matrice.cpp']) and all(path in config['files']['other'] for path in
        ['include/ElectroMagnetisme.cpp','src/matrice.cpp','src/matrice.hpp','include/mathdt.h']),
    'historical files preserved': all((root/path).exists() for path in
        ['include/ElectroMagnetisme.cpp','include/ElectroMagnetisme.hpp','src/matrice.cpp','src/matrice.hpp','include/mathdt.h']),
    'log no longer in exe folder': 'applicationDirPath()' not in log and 'QStandardPaths::AppLocalDataLocation' in log
        and 'QFile::remove(startupLogPath())' not in entry,
    'log retention and Help UI': 'MaxLogBytes = 2 * 1024 * 1024' in log
        and 'ArchivedLogs = 3' in log and 'Open diagnostics folder' in mainwindow,
    'Qt GUI test targets separated from production': 'src/em_calculator_widget.cpp' in
        (root/'tests/qt_6700/CMakeLists.txt').read_text(encoding='utf-8')
        and 'Qt6::Test' in (root/'tests/qt_6700/CMakeLists.txt').read_text(encoding='utf-8')
        and 'tests/qt_6700/gui_smoke_6700.cpp' not in sources,
    'stabilized GUI object identifiers': all(x in ui for x in
        ['emCalculatorTabs','emScientificExpression','emCalculateButton','emScientificResult','emNotesEditor']),
    'all Qt build sources present': all((root/path).is_file() for path in sources),
}
for label,ok in checks.items():
    if not ok: raise SystemExit(f'FAIL: {label}')
    print('PASS:',label)

# Guard against accidentally reintroducing active includes of retired implementations.
for file in [root/x for x in sources]:
    source=file.read_text(encoding='utf-8',errors='replace')
    for legacy in ['matrice.hpp','ElectroMagnetisme.hpp','mathdt.h']:
        if re.search(r'^\s*#include\s*[<"](?:.*/)?'+re.escape(legacy)+r'[>"]',source,re.M):
            raise SystemExit(f'FAIL: {file.name} includes isolated legacy {legacy}')
print('PASS: legacy inclusion graph (production compilation units)')

# Stable production kernel digests are the 6.6/6.0 certified baseline.
frozen={
 'src/numerical_em_solvers.cpp':'8da57b9ed3630451dd834d9afdfa8363a1b729fa94701335b23e13dcb994316e',
 'src/pec_surface_mom.cpp':'989ac706950c05f9be3d0ece1e27c7860824c88a3a19f20a598117a2d4b541ca',
 'src/hybrid_wire_surface_mom.cpp':'89e8233779fa4c2db3a4f9036d377eecbf2a54970851b68c3c0de40dc4745210',
 'src/antenna_validation_bench.cpp':'6a55b04842395c49f15ef88b2c2f0445a5ab8ff16060b7a09cf147b2eb71e3bf',
 'src/electrostatic_model.cpp':'b3e8b46a60d316895324e3f4a13b6e4ce3579dccdfeb5665b43176e0ed597356',
 'src/magnetostatic_model.cpp':'6fcf3123641254a1476308b3dacb990c0de470e8d60032bc8e9ef805cadca5af',
}
for file,digest in frozen.items():
    if hashlib.sha256((root/file).read_bytes()).hexdigest()!=digest:
        raise SystemExit('FAIL frozen EM kernel '+file)
print('PASS: six certified EM kernel hashes')

# Run real C++ numerical tests, independent of SDK and UI.
compiler=['g++','-std=c++17','-O1','-Wall','-Wextra','-Wpedantic','-Werror','-I'+str(root/'include')]
with tempfile.TemporaryDirectory(prefix='qtsignal_6700_') as tmp:
    variants=[
        ('6.4 constants, antenna, math','tests/release_calculator_6400.cpp',False),
        ('6.5 dimensions & worksheet','tests/release_calculator_6500.cpp',True),
        ('6.5.1 SI parser','tests/release_calculator_6510.cpp',True),
        ('6.6 SI presentation','tests/release_calculator_6600.cpp',True),
        ('6.3 rail de Laplace','tests/release_laplace_630.cpp',None),
    ]
    for i,(label,test,dim) in enumerate(variants):
        binaries=[str(root/'src/laplace_rail_model.cpp')] if dim is None else [str(root/'src/em_calculator_model.cpp')]
        if dim: binaries.append(str(root/'src/em_units_calculator_model.cpp'))
        dest=str(Path(tmp)/f'test{i}')
        subprocess.run(compiler + binaries + [str(root/test),'-o',dest], check=True, cwd=root)
        subprocess.run([dest],check=True,cwd=root)
        print('PASS:',label)

    # Template set is exercised separately from UI string checks.
    section=ui.split('const FormulaTemplate kFormulaTemplates[]',1)[1].split('};',1)[0]
    exprs=re.findall(r'\{\s*"([^"]+)",\s*"([^"]*)",\s*"([^"]+)"\s*\}',section)
    if len(exprs)<20: raise SystemExit('FAIL: formulas missing')
    src=['#include "em_units_calculator_model.h"','#include <iostream>','int main(){']
    for title,expression,_ in exprs:
        if not expression: continue
        src += ['{auto r=EmCalculator::evaluateQuantity("'+expression+'");'
                'if(!r.ok){std::cerr<<"'+title+': "<<r.error;return 2;}}']
    src.append('return 0;}')
    test_cpp=Path(tmp)/'formulas.cpp';test_cpp.write_text('\n'.join(src),encoding='utf-8')
    target=Path(tmp)/'formulas'
    subprocess.run(compiler+[str(root/'src/em_calculator_model.cpp'),str(root/'src/em_units_calculator_model.cpp'),str(test_cpp),'-o',str(target)],check=True,cwd=root)
    subprocess.run([str(target)],check=True,cwd=root)
    print('PASS:',len(exprs)-1,'calculator EM templates')
print('QTsignalApp 6.7.0 static/QPM/numerical regression: PASS')
print('QtCore and Qt Widgets QTest integration: NOT EXECUTED (Qt SDK required).')
