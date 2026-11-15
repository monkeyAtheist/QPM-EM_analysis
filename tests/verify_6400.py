#!/usr/bin/env python3
"""Calculator/constant maintenance checks. Qt GUI must additionally be built on the target system."""
from pathlib import Path
import hashlib
import json
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
config = json.loads((root/'QTsignalApp.qtproject.json').read_text(encoding='utf-8'))
main = (root/'src/main.cpp').read_text(encoding='utf-8')
ui = (root/'src/em_calculator_widget.cpp').read_text(encoding='utf-8')
headers = (root/'include/widgets/em_calculator_widget.h').read_text(encoding='utf-8')
model = (root/'src/em_calculator_model.cpp').read_text(encoding='utf-8')
release = (root/'RELEASE_6_4_0.md').read_text(encoding='utf-8')

checks = {
    'version 6.4.0': config.get('version')=='6.4.0' and config.get('packaging',{}).get('productVersion')=='6.4.0' and 'QStringLiteral("6.4.0")' in main,
    'calculator tab': 'EM calculator' in ui and 'buildScientificTab' in headers,
    'catalogue tab': 'Physical constants' in ui and 'buildConstantsTab' in headers and 'updateConstantsFilter' in ui,
    'live search': 'emConstantsSearch' in ui and 'textChanged' in ui and 'setRowHidden' in ui,
    'constant insert and copy': 'Insert in calculator' in ui and 'Copy SI value' in ui and 'Copy symbol' in ui,
    'previous result and errors': 'm_lastAnswerValid' in ui and 'ans is not defined' in model,
    'history and notes': 'emCalculationHistory' in ui and 'Send to Notes' in ui and 'appendPlainText' in ui,
    'responsive access': 'QScrollArea' in ui and 'QGridLayout' in ui,
    'SI copy': 'Copy f (Hz)' in ui and 'Copy λ (m)' in ui and 'scaledValue' in ui,
    'single constant registry': 'physicalConstants()' in ui and 'physicalConstants()' in model,
    'release notes': '6.4.0' in release,
}
failed=[name for name,passed in checks.items() if not passed]
if failed: raise SystemExit('FAIL static checks: '+', '.join(failed))

# Solver hashes are the frozen pre-calculator-release baseline.
hashes = {
 'src/numerical_em_solvers.cpp': '8da57b9ed3630451dd834d9afdfa8363a1b729fa94701335b23e13dcb994316e',
 'src/pec_surface_mom.cpp': '989ac706950c05f9be3d0ece1e27c7860824c88a3a19f20a598117a2d4b541ca',
 'src/hybrid_wire_surface_mom.cpp': '89e8233779fa4c2db3a4f9036d377eecbf2a54970851b68c3c0de40dc4745210',
 'src/antenna_validation_bench.cpp': '6a55b04842395c49f15ef88b2c2f0445a5ab8ff16060b7a09cf147b2eb71e3bf',
 'src/electrostatic_model.cpp': 'b3e8b46a60d316895324e3f4a13b6e4ce3579dccdfeb5665b43176e0ed597356',
 'src/magnetostatic_model.cpp': '6fcf3123641254a1476308b3dacb990c0de470e8d60032bc8e9ef805cadca5af',
}
for rel, expected in hashes.items():
    digest=hashlib.sha256((root/rel).read_bytes()).hexdigest()
    if digest != expected: raise SystemExit(f'FAIL solver hash {rel}: {digest}')
with tempfile.TemporaryDirectory(prefix='qtsig640_') as td:
    out=Path(td)/'calculator_6400'
    cmd=['g++','-std=c++17','-O1','-Wall','-Wextra','-Wpedantic','-Werror',
         '-I'+str(root/'include'), str(root/'src/em_calculator_model.cpp'),
         str(root/'tests/release_calculator_6400.cpp'),'-o',str(out)]
    subprocess.run(cmd, cwd=root, check=True)
    subprocess.run([str(out)], cwd=root, check=True)
print('QTsignalApp 6.4.0 calculator verification: PASS (non-Qt numeric + UI static checks)')
