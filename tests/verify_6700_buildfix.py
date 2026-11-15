#!/usr/bin/env python3
"""Ensure the 6.7.0 split schema module does not break the QPM C++ widget build.

Static regression only: the real Windows/Qt compiler/linker must still run.
"""
import json
from pathlib import Path
root = Path(__file__).resolve().parents[1]
widget = (root / 'src/antenna_designer_widget.cpp').read_text(encoding='utf-8')
source = (root / 'src/antenna_project_document.cpp').read_text(encoding='utf-8')
header = (root / 'include/antenna_project_document.h').read_text(encoding='utf-8')
project = json.loads((root / 'QTsignalApp.qtproject.json').read_text(encoding='utf-8'))
checks = {
    'no inaccessible implementation-local schema symbol in widget':
        'AntennaProjectMinimumSchemaVersion' not in widget,
    'widget uses initialized output parameter':
        'int sourceSchemaVersion = 0;' in widget,
    'schema check is invoked before version is read for migration':
        widget.index('AntennaProjectDocument::checkAntennaProjectSchema(rawRoot, sourceSchemaVersion,') <
        widget.index('AntennaProjectDocument::migrateAntennaProjectToCurrent(rawRoot, sourceSchemaVersion)'),
    'schema check always initializes the output before branching':
        source.index('sourceVersion = AntennaProjectMinimumSchemaVersion;') <
        source.index('if (!root.contains(QStringLiteral("version")))'),
    'schema public API unchanged':
        'inline constexpr int kSchemaVersion = 19;' in header,
    'QtCore implementation is in QPM sources':
        'src/antenna_project_document.cpp' in project['files']['sources'],
    'application version unchanged':
        project['version'] == project['packaging']['productVersion'] == '6.7.0',
}
for label, ok in checks.items():
    print(('PASS' if ok else 'FAIL') + ': ' + label)
if not all(checks.values()):
    raise SystemExit(1)
print('QTsignalApp 6.7.0 QPM build fix static regression: PASS')
