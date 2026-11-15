#!/usr/bin/env python3
"""6.5.1 focused regression for dimensional literals, UI reliability and frozen solver hashes."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
config = json.loads((root / 'QTsignalApp.qtproject.json').read_text(encoding='utf-8'))
main = (root / 'src/main.cpp').read_text(encoding='utf-8')
ui = (root / 'src/em_calculator_widget.cpp').read_text(encoding='utf-8')
header = (root / 'include/widgets/em_calculator_widget.h').read_text(encoding='utf-8')
units = (root / 'src/em_units_calculator_model.cpp').read_text(encoding='utf-8')
model = (root / 'src/em_calculator_model.cpp')

checks = {
    '6.5.1 package': config.get('version') == '6.5.1'
        and config.get('packaging', {}).get('productVersion') == '6.5.1'
        and 'QStringLiteral("6.5.1")' in main,
    'project schema v19': 'constexpr int AntennaProjectSchemaVersion = 19;' in
        (root / 'src/antenna_designer_widget.cpp').read_text(encoding='utf-8'),
    'unit suffix power': 'if (accept(\'^\')) unitValue = raised(unitValue, unary());' in units,
    'unit name reservation': 'units().find(name)!=units().end()' in units,
    'explicit units': 'm_allowBareUnit = true;' in units,
    'antenna calculation success and failure': 'bool EmCalculatorWidget::calculateAntenna()' in ui
        and 'return false;' in ui.split('bool EmCalculatorWidget::calculateAntenna()',1)[1]
        and 'm_validatedAntennaInputs = in;' in ui,
    'no stale create': 'if (!m_antennaValid) return;' in ui
        and 'const EmCalculator::AntennaInputs in = m_validatedAntennaInputs;' in ui,
    'dirty signal hooks': 'invalidateAntennaCalculation();' in ui
        and 'invalidateWaveCalculation();' in ui
        and 'qOverload<double>(&QDoubleSpinBox::valueChanged)' in ui,
    'validated result copying': 'm_validatedWaveResult.frequencyHz' in ui
        and 'm_validatedAntennaResult.primaryDimensionM' in ui,
    'notes confirmation': 'QMessageBox::question(this, QStringLiteral("Replace notes?")' in ui,
    'notes backup': 'calculatorNotes/backupBeforeImport' in ui
        and 'settings.sync();' in ui and 'settings.status() != QSettings::NoError' in ui
        and 'void EmCalculatorWidget::restorePreviousNotes()' in ui,
    'previous sample migration': 'sheet == legacySample || sheet == legacyExample' in ui,
    'headers': all(x in header for x in ('m_antennaValid', 'm_validatedAntennaInputs',
                                      'm_waveValid', 'restorePreviousNotes')),
    'notes': (root / 'RELEASE_6_5_1.md').exists(),
}
failed = [name for name, pass_ in checks.items() if not pass_]
if failed:
    raise SystemExit('FAIL static contract: ' + ', '.join(failed))

frozen_hashes = {
 'src/numerical_em_solvers.cpp': '8da57b9ed3630451dd834d9afdfa8363a1b729fa94701335b23e13dcb994316e',
 'src/pec_surface_mom.cpp': '989ac706950c05f9be3d0ece1e27c7860824c88a3a19f20a598117a2d4b541ca',
 'src/hybrid_wire_surface_mom.cpp': '89e8233779fa4c2db3a4f9036d377eecbf2a54970851b68c3c0de40dc4745210',
 'src/antenna_validation_bench.cpp': '6a55b04842395c49f15ef88b2c2f0445a5ab8ff16060b7a09cf147b2eb71e3bf',
 'src/electrostatic_model.cpp': 'b3e8b46a60d316895324e3f4a13b6e4ce3579dccdfeb5665b43176e0ed597356',
 'src/magnetostatic_model.cpp': '6fcf3123641254a1476308b3dacb990c0de470e8d60032bc8e9ef805cadca5af',
}
for rel, expected in frozen_hashes.items():
    got = hashlib.sha256((root / rel).read_bytes()).hexdigest()
    if got != expected:
        raise SystemExit(f'FAIL frozen EM kernel {rel}: {got}')

# Independently compile the historical numerical logic and the new explicit audit regressions.
compiler = ['g++','-std=c++17','-O1','-Wall','-Wextra','-Wpedantic','-Werror',
            '-I'+str(root/'include')]
with tempfile.TemporaryDirectory(prefix='qtsignalapp_6510_') as tmp:
    for tag, test, dimensioned in (
        ('6.4 constants, mathematical expressions and antenna sizing', 'release_calculator_6400.cpp', False),
        ('6.5 dimensional worksheet (reserved-name sample updated)', 'release_calculator_6500.cpp', True),
        ('6.5.1 parser and inverse-validation contracts', 'release_calculator_6510.cpp', True),
    ):
        exe = Path(tmp) / test.replace('.cpp','')
        cmd = compiler + [str(model)]
        if dimensioned: cmd.append(str(root/'src/em_units_calculator_model.cpp'))
        cmd += [str(root/'tests'/test), '-o',str(exe)]
        subprocess.run(cmd, check=True, cwd=root)
        subprocess.run([str(exe)], check=True, cwd=root)
        print('PASS:', tag)

    # All pre-existing formula templates must continue to evaluate.
    section = ui.split('const FormulaTemplate kFormulaTemplates[]',1)[1].split('};',1)[0]
    formulas = re.findall(r'\{\s*"([^"]+)",\s*"([^"]*)",\s*"([^"]+)"\s*\}', section)
    if len(formulas)<20:
        raise SystemExit('FAIL formula templates missing')
    code=['#include "em_units_calculator_model.h"','#include <iostream>','int main(){']
    for title,expr,_ in formulas:
        if not expr: continue
        # Template strings are ASCII constants curated in the C++ source.
        code += ['{ auto r=EmCalculator::evaluateQuantity("'+expr+'");'
                 ' if(!r.ok){std::cerr<<"template '+title+': "<<r.error<<"\\n";return 2;} }']
    code.append('return 0;}')
    source=Path(tmp)/'templates.cpp'
    source.write_text('\n'.join(code), encoding='utf-8')
    exe=Path(tmp)/'templates'
    subprocess.run(compiler+[str(model),str(root/'src/em_units_calculator_model.cpp'),
                            str(source),'-o',str(exe)], check=True, cwd=root)
    subprocess.run([str(exe)], check=True, cwd=root)
    print('PASS:',len(formulas)-1,'existing EM formula templates')
print('QTsignalApp 6.5.1 numerical/static/frozen-kernel verification: PASS')
print('Qt/Windows GUI, QSettings restart, Notes modal dialogs and Antenna Designer interaction: NOT TESTED HERE.')
