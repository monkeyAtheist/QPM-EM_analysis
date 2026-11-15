#!/usr/bin/env python3
"""6.5.0 dimensional calculator, unit-templates, interface contracts and frozen EM hashes."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
config = json.loads((root / 'QTsignalApp.qtproject.json').read_text(encoding='utf-8'))
ui = (root / 'src/em_calculator_widget.cpp').read_text(encoding='utf-8')
header = (root / 'include/widgets/em_calculator_widget.h').read_text(encoding='utf-8')
main = (root / 'src/main.cpp').read_text(encoding='utf-8')
unit_model = (root / 'src/em_units_calculator_model.cpp').read_text(encoding='utf-8')
release = (root / 'RELEASE_6_5_0.md').read_text(encoding='utf-8')

checks = {
    '6.5 package version': config.get('version') == '6.5.0'
        and config.get('packaging', {}).get('productVersion') == '6.5.0'
        and 'QStringLiteral("6.5.0")' in main,
    'new model in project': 'src/em_units_calculator_model.cpp' in config.get('project', {}).get('sources', [])
        or 'src/em_units_calculator_model.cpp' in (root / 'QTsignalApp.qtproject.json').read_text(),
    'worksheet in UI': all(x in ui for x in (
        'Variables & units', 'emVariablesWorksheet', 'emEvaluateVariablesButton',
        'emVariablesResultTable', 'evaluateVariableWorksheet()', 'QSettings',
        'Copy selected SI value', 'Send to Notes')),
    'worksheet wired': all(x in header for x in (
        'buildVariablesTab', 'evaluateVariableWorksheet', 'm_userVariables', 'm_variableWorksheet')),
    'scientific calculator reuses worksheet values':
        'evaluateQuantity(expression.toStdString(), vars)' in ui
        and 'm_userVariables' in ui and 'emScientificDerivedUnit' in ui,
    'invalidates stale variables': 'm_userVariables.clear(); // Never reuse stale definitions after an edit.' in ui,
    'unchanged persistence schema': 'constexpr int AntennaProjectSchemaVersion = 19;' in
        (root / 'src/antenna_designer_widget.cpp').read_text(encoding='utf-8'),
    'strict dimensions in engine': 'Incompatible units for addition/subtraction.' in unit_model
        and 'Function requires a dimensionless input.' in unit_model,
    'release notes': '6.5.0' in release,
}
failed = [k for k,v in checks.items() if not v]
if failed: raise SystemExit('FAIL static contracts: ' + ', '.join(failed))

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
    if got != expected: raise SystemExit(f'FAIL unchanged-solver hash: {rel}: {got}')

# Every pre-existing formula template must remain numerically evaluable with explicit dimensions.
formula_section = ui.split('const FormulaTemplate kFormulaTemplates[]', 1)[1].split('};', 1)[0]
templates = re.findall(r'\{\s*"([^"]+)",\s*"([^"]*)",\s*"([^"]+)"\s*\}', formula_section)
if len(templates) < 20: raise SystemExit('FAIL: missing calculator templates')
examples = '\n'.join(
    '{auto v=EmCalculator::evaluateQuantity("' + expr + '"); '
    'if (!v.ok) {std::cerr << "' + title + ': " << v.error << "\\n"; return 2;}}'
    for title, expr, _ in templates if expr
)

with tempfile.TemporaryDirectory(prefix='qtsignalapp_6500_') as td:
    temp = Path(td)
    compiler = ['g++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                '-I' + str(root / 'include')]
    source = [str(root / 'src/em_calculator_model.cpp')]
    units = [str(root / 'src/em_units_calculator_model.cpp')]
    for label, inputs, target in (
        ('6.4 calculator numerical compatibility', source + [str(root / 'tests/release_calculator_6400.cpp')], 'legacy'),
        ('6.5 dimensional worksheet', source + units + [str(root / 'tests/release_calculator_6500.cpp')], 'units')):
        output = temp / target
        subprocess.run(compiler + inputs + ['-o', str(output)], check=True, cwd=root)
        subprocess.run([str(output)], check=True, cwd=root)
        print('PASS:', label)
    template = temp / 'all_calculator_templates.cpp'
    template.write_text('#include "em_units_calculator_model.h"\n#include <iostream>\nint main(){\n'
        + examples + '\nreturn 0;\n}\n', encoding='utf-8')
    output = temp / 'templates'
    subprocess.run(compiler + source + units + [str(template), '-o', str(output)], check=True, cwd=root)
    subprocess.run([str(output)], check=True, cwd=root)
    print(f'PASS: {len(templates)-1} dimensioned EM formula templates')

print('QTsignalApp 6.5.0 variables/units numerical + static/UI contracts: PASS')
print('Qt Windows interface build is not covered by this test.')
