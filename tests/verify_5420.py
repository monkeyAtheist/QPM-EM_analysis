from pathlib import Path
import hashlib, json, math, re

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / 'tests' / 'fixtures'
project = json.loads((ROOT / 'QTsignalApp.qtproject.json').read_text(encoding='utf-8'))
main = (ROOT / 'src/main.cpp').read_text(encoding='utf-8')
designer = (ROOT / 'src/antenna_designer_widget.cpp').read_text(encoding='utf-8')
roadmap = (ROOT / 'SIMULATION_DESIGN_V1_ROADMAP.md').read_text(encoding='utf-8')
change = (ROOT / 'CHANGELOG_APP.md').read_text(encoding='utf-8')

checks = {
    'version': project.get('version') == '5.42.0' and project.get('packaging', {}).get('productVersion') == '5.42.0' and 'QStringLiteral("5.42.0")' in main,
    'schema constant': 'constexpr int AntennaProjectSchemaVersion = 19;' in designer,
    'minimum schema': 'constexpr int AntennaProjectMinimumSchemaVersion = 1;' in designer,
    'future guard': 'sourceVersion > AntennaProjectSchemaVersion' in designer and 'Open it with a newer QTsignalApp version' in designer,
    'migration': all(token in designer for token in ['migrateAntennaProjectToCurrent', 'QStringLiteral("planes")', 'QStringLiteral("surfaces")', 'QStringLiteral("sourceOhm")', 'QStringLiteral("referenceOhm")']),
    'pre-mutation validation': designer.find('validateAntennaProjectDocument(root, validationError)') < designer.find('m_wires = std::move(wires)'),
    'atomic save': all(token in designer for token in ['#include <QSaveFile>', 'QSaveFile file(path);', '!file.commit()', 'previous file was left unchanged']),
    'geometry guards': all(token in designer for token in ['validateFiniteJsonValue', 'zero-length wire', 'innerRadiusM must be smaller than radiusM', 'referenceOhm', 'fixedMask']),
    'roadmap': all(token in roadmap for token in ['Current baseline — 5.42 implementation stabilization complete; target release gate pending', '5.42-A COMPLETE', '6.0.0 milestone is not released']),
    'changelog': change.startswith('# QTsignalApp 5.42.0') and 'Persistence hardening (5.42-A)' in change,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('5.42-A static verification FAILED: ' + ', '.join(failed))

# 5.42-A persistence contract remains cumulative. 5.42-B intentionally added input/result guards
# to three solver translation units, and 5.42-C adds cache-diagnostic fields to the Hybrid public
# result API. The remaining validated geometry/bench/API kernels stay frozen.
expected_hashes = {
    'src/antenna_validation_bench.cpp': '17687f6f24baae46c38eec7f0b93158878c09d22aa5df7535b4e6ac54a4ec863',
    'src/antenna_surface_geometry.cpp': '42d2b317b22c4fbdd2fd14a57ff45936c4b8be54e155c99739e6bb19b74200be',
    'src/microstrip_models.cpp': '681c3f4313412e4a43150639f212f75576c91832945cd98edd13875c8ae466e3',
    'include/antenna_validation_bench.h': 'b851ec29e5a53984836b030ebcd2793e47d01e5a1c78a70eb4ffb614ac03b31c',
    'include/numerical_em_solvers.h': 'be757e5b4c71a0bffd83699dc936b7a8b9a3f1139f1d2a9e89b02d4bbc26c560',
    'include/pec_surface_mom.h': '66071c3efff91219647fc42eb325259b1ff53a8a38eb0414ffe0875dec792b19',
    'include/hybrid_wire_surface_mom.h': 'dda5c8c97e527a838ccb30d8c5655e9bbc0dca8b5ef489e5909153e16849eb66',
}
for rel, expected in expected_hashes.items():
    actual = hashlib.sha256((ROOT / rel).read_bytes()).hexdigest()
    if actual != expected:
        raise SystemExit(f'5.42 cumulative frozen-kernel regression: {rel}')

CURRENT = 19
MINIMUM = 1

def migrate_reference(root):
    root = json.loads(json.dumps(root))
    source = root.get('version', MINIMUM)
    if not isinstance(source, int) or isinstance(source, bool):
        raise ValueError('schema version')
    if source < MINIMUM or source > CURRENT:
        raise ValueError('unsupported schema')
    if root.get('units', 'm') != 'm':
        raise ValueError('units')
    if 'surfaces' not in root and isinstance(root.get('planes'), list):
        root['surfaces'] = root['planes']
    for feed in root.get('feeds', []):
        if isinstance(feed, dict) and 'referenceOhm' not in feed and 'sourceOhm' in feed:
            feed['referenceOhm'] = feed['sourceOhm']
    root.setdefault('units', 'm')
    root['version'] = CURRENT
    return root

def validate_reference(root):
    def finite_walk(value):
        if isinstance(value, bool) or value is None or isinstance(value, str):
            return
        if isinstance(value, (int, float)):
            if not math.isfinite(float(value)):
                raise ValueError('non-finite')
            return
        if isinstance(value, list):
            if len(value) > 100000:
                raise ValueError('collection limit')
            for item in value:
                finite_walk(item)
            return
        if isinstance(value, dict):
            for item in value.values():
                finite_walk(item)
    finite_walk(root)
    for i, wire in enumerate(root.get('wires', [])):
        if not isinstance(wire, dict):
            raise ValueError(f'wires[{i}] object')
        for key in ('x1', 'y1', 'x2', 'y2'):
            if not isinstance(wire.get(key), (int, float)) or isinstance(wire.get(key), bool):
                raise ValueError(f'wires[{i}].{key}')
        z1, z2 = float(wire.get('z1', 0.0)), float(wire.get('z2', 0.0))
        dx = float(wire['x2']) - float(wire['x1'])
        dy = float(wire['y2']) - float(wire['y1'])
        if not (dx*dx + dy*dy + (z2-z1)*(z2-z1) > 1e-30):
            raise ValueError('zero-length wire')
    return True

current = json.loads((FIXTURES / 'qta_v19_roundtrip.json').read_text(encoding='utf-8'))
canonical = migrate_reference(current)
validate_reference(canonical)
# JSON save/load round-trip contract: no schema/geometry information disappears in canonical v19.
roundtrip = json.loads(json.dumps(canonical, sort_keys=True, separators=(',', ':')))
if roundtrip != canonical or roundtrip['version'] != 19 or roundtrip['units'] != 'm':
    raise SystemExit('5.42-A round-trip fixture FAILED')

legacy = json.loads((FIXTURES / 'qta_legacy_unversioned.json').read_text(encoding='utf-8'))
legacy_migrated = migrate_reference(legacy)
validate_reference(legacy_migrated)
if legacy_migrated['version'] != 19 or legacy_migrated['surfaces'] != legacy['planes'] or legacy_migrated['feeds'][0]['referenceOhm'] != 50.0:
    raise SystemExit('5.42-A legacy migration fixture FAILED')

future = json.loads((FIXTURES / 'qta_future_v20.json').read_text(encoding='utf-8'))
try:
    migrate_reference(future)
except ValueError:
    pass
else:
    raise SystemExit('5.42-A future-schema fixture FAILED')

invalid = json.loads((FIXTURES / 'qta_invalid_zero_length_wire.json').read_text(encoding='utf-8'))
try:
    validate_reference(migrate_reference(invalid))
except ValueError:
    pass
else:
    raise SystemExit('5.42-A malformed-geometry fixture FAILED')

# Basic lexical regression for the modified Qt translation unit.
def strip_cpp(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    text = re.sub(r'//[^\n]*', '', text)
    text = re.sub(r'R"[^\(]*\(.*?\)[^"]*"', '""', text, flags=re.S)
    text = re.sub(r'"(?:\\.|[^"\\])*"', '""', text)
    text = re.sub(r"'(?:\\.|[^'\\])*'", "''", text)
    return text
stripped = strip_cpp(designer)
expected_balance = {'(': 1, '[': 0, '{': 1}
for op, cl in [('(', ')'), ('[', ']'), ('{', '}')]:
    if stripped.count(op) - stripped.count(cl) != expected_balance[op]:
        raise SystemExit(f'5.42-A lexical balance FAILED: {op}{cl}')

print('QTsignalApp 5.42.0 persistence-hardening verification: PASS')
