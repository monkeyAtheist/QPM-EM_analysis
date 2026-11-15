#!/usr/bin/env python3
"""6.6.0 UX contracts, numerical compatibility and unchanged production EM kernels.

Qt UI behaviour requires the accompanying GUI_6600_QPM_CHECKLIST.md on Windows.
"""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
ui = (root / 'src/em_calculator_widget.cpp').read_text(encoding='utf-8')
header = (root / 'include/widgets/em_calculator_widget.h').read_text(encoding='utf-8')
designer = (root / 'src/antenna_designer_widget.cpp').read_text(encoding='utf-8')
designer_header = (root / 'include/widgets/antenna_designer_widget.h').read_text(encoding='utf-8')
main = (root / 'src/mainwindow.cpp').read_text(encoding='utf-8')
entry = (root / 'src/main.cpp').read_text(encoding='utf-8')
config = json.loads((root / 'QTsignalApp.qtproject.json').read_text(encoding='utf-8'))

checks = {
    'package and UI version': config['version'] == config['packaging']['productVersion'] == '6.6.0'
       and 'QStringLiteral("6.6.0")' in entry,
    'schema preserved': 'constexpr int AntennaProjectSchemaVersion = 19;' in designer,
    'format registry in build': 'include/em_display_format.h' in config['files']['headers'],
    'validated science snapshots': all(x in ui for x in ['m_validatedScientificExpression = expression;',
        'm_validatedScientificQuantity = result.quantity;', 'invalidateScientificDisplay();',
        'scientificExactText()', 'scientificDisplayText()']),
    'prefixed presentation without numerical change': 'EmDisplay::scaled(m_validatedScientificQuantity.siValue' in ui,
    'precision and copy modes': all(x in ui for x in ['emAutoPrefix', 'emSignificantDigits',
        'Copy SI value', 'Copy displayed value + unit', 'Copy expression']),
    'compact function palette': 'emFunctionPaletteToggle' in ui and 'quick->setVisible(false)' in ui,
    'keyboard focus': 'Ctrl+L' in ui and 'setContext(Qt::WidgetWithChildrenShortcut)' in ui,
    'scientific results invalidated when worksheet changes':
        'invalidateScientificDisplay(); // Dependent scientific result' in ui,
    'portable sidecars': all(x in ui for x in ['.notes.md', '.worksheet.txt',
        'bool EmCalculatorWidget::saveWorksheet()', 'bool EmCalculatorWidget::loadWorksheet()',
        'bool EmCalculatorWidget::saveNotes()', 'bool EmCalculatorWidget::loadNotes()',
        'QSaveFile file(path);', 'if (!saveNotes() || !saveWorksheet())']),
    'project-linked only on successful save/load':
        'antennaProjectFileChanged' in designer_header and designer.count('emit antennaProjectFileChanged(') == 2
        and 'AntennaDesignerWidget::antennaProjectFileChanged' in main
        and 'calculator->setCreateAntennaHandler([this, antennaDesigner, calculator]' in main,
    'designer geometry confirmation': 'Preview antenna geometry' in ui
        and 'Replace geometry' in ui and 'confirmation.clickedButton() != confirm' in ui,
    'new release docs': (root / 'RELEASE_6_6_0.md').is_file()
        and (root / 'tests/GUI_6600_QPM_CHECKLIST.md').is_file(),
}
for label, ok in checks.items():
    if not ok: raise SystemExit('FAIL: ' + label)

frozen_hashes = {
 'src/numerical_em_solvers.cpp': '8da57b9ed3630451dd834d9afdfa8363a1b729fa94701335b23e13dcb994316e',
 'src/pec_surface_mom.cpp': '989ac706950c05f9be3d0ece1e27c7860824c88a3a19f20a598117a2d4b541ca',
 'src/hybrid_wire_surface_mom.cpp': '89e8233779fa4c2db3a4f9036d377eecbf2a54970851b68c3c0de40dc4745210',
 'src/antenna_validation_bench.cpp': '6a55b04842395c49f15ef88b2c2f0445a5ab8ff16060b7a09cf147b2eb71e3bf',
 'src/electrostatic_model.cpp': 'b3e8b46a60d316895324e3f4a13b6e4ce3579dccdfeb5665b43176e0ed597356',
 'src/magnetostatic_model.cpp': '6fcf3123641254a1476308b3dacb990c0de470e8d60032bc8e9ef805cadca5af',
}
for path, expected in frozen_hashes.items():
    got = hashlib.sha256((root / path).read_bytes()).hexdigest()
    if got != expected: raise SystemExit(f'FAIL frozen EM kernel {path}: {got}')

compiler = ['g++','-std=c++17','-O1','-Wall','-Wextra','-Wpedantic','-Werror','-I'+str(root/'include')]
with tempfile.TemporaryDirectory(prefix='qtsignalapp_6600_') as temp:
    for label, sources in [
        ('6.4 calculator constants/antennas', ['src/em_calculator_model.cpp','tests/release_calculator_6400.cpp']),
        ('6.5 dimensional worksheet', ['src/em_calculator_model.cpp','src/em_units_calculator_model.cpp','tests/release_calculator_6500.cpp']),
        ('6.5.1 parser/variables regression', ['src/em_calculator_model.cpp','src/em_units_calculator_model.cpp','tests/release_calculator_6510.cpp']),
        ('6.6 formatting regression', ['src/em_calculator_model.cpp','src/em_units_calculator_model.cpp','tests/release_calculator_6600.cpp']),
    ]:
        binary = Path(temp)/str(len(label))
        subprocess.run(compiler + [str(root/p) for p in sources]+['-o',str(binary)], check=True, cwd=root)
        subprocess.run([str(binary)], check=True, cwd=root)
        print('PASS:', label)
    section = ui.split('const FormulaTemplate kFormulaTemplates[]',1)[1].split('};',1)[0]
    formulas = re.findall(r'\{\s*"([^"]+)",\s*"([^"]*)",\s*"([^"]+)"\s*\}',section)
    if len(formulas)<20: raise SystemExit('FAIL: missing formula templates')
    lines = ['#include "em_units_calculator_model.h"','#include <iostream>','int main(){']
    for title, expr, _ in formulas:
        if not expr: continue
        lines.append('{auto r=EmCalculator::evaluateQuantity("'+expr+'");'
            'if(!r.ok){std::cerr<<"'+title+': "<<r.error;return 2;}}')
    lines.append('return 0;}')
    source = Path(temp)/'templates.cpp';source.write_text('\n'.join(lines))
    binary = Path(temp)/'formula_run'
    subprocess.run(compiler+[str(root/'src/em_calculator_model.cpp'),
      str(root/'src/em_units_calculator_model.cpp'),str(source),'-o',str(binary)],check=True,cwd=root)
    subprocess.run([str(binary)],check=True,cwd=root)
    print('PASS:',len(formulas)-1,'EM formula templates')
print('QTsignalApp 6.6.0 numerical/static/frozen-kernel verification: PASS')
print('Qt GUI, DPI, file-dialog interactions, and QSettings/sidecar integration: NOT EXECUTED in this environment.')
