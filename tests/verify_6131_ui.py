#!/usr/bin/env python3
"""Portable (no Qt headers) layout contract for QTsignalApp 6.13.1."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys
import zipfile

root = Path(__file__).resolve().parents[1]
cpp = (root / 'src/em_calculator_widget.cpp').read_text(encoding='utf-8')
qpm = json.loads((root / 'QTsignalApp.qtproject.json').read_text(encoding='utf-8'))
main = (root / 'src/main.cpp').read_text(encoding='utf-8')
assert qpm['packaging']['productVersion'] == '6.13.1'
assert 'setApplicationVersion(QStringLiteral("6.13.1"))' in main
# Notebook mode should reclaim space without removing the help content.
assert 'intro->setVisible(index != m_notesTabIndex)' in cpp
assert 'm_projectNotesLabel->setWordWrap(true)' not in cpp
assert '"Global notes · no .qta file linked"' in cpp
assert 'auto *inkHelp = new QLabel' not in cpp
assert 'Drawing editor — quick guide' in cpp
# Advanced controls must be separate widgets, hidden by default and exposed by toggle.
assert 'notesDrawingAdvancedPanel' in cpp
assert 'notesDrawingDetailsToggle' in cpp
assert 'advancedPanel->hide()' in cpp
assert 'connect(advancedButton, &QToolButton::toggled, advancedPanel, &QWidget::setVisible)' in cpp
assert 'if (index == int(NotesInkCanvas::Tool::Component))' in cpp
assert 'advancedButton->setChecked(true)' in cpp
# No hidden control accidentally left in the main toolbar rows 3-5.
assert not re.search(r'inkTools->addWidget\([^,;]+,\s*[345]\s*,', cpp)
for tool in ['symbolSelector','symbolSize','chooseSymbol','rotateSymbol','setSelectedSize','renameSymbol','removeSymbol',
             'zoomIn','zoomOut','zoomFit']:
    assert re.search(r'advancedTools->addWidget\('+tool+r',',cpp), tool
# Fullscreen moves both toolbars and the live drawing, then restores them.
assert 'inkLayout->removeWidget(advancedPanel)' in cpp
assert 'expanded->addWidget(advancedPanel)' in cpp
assert 'inkLayout->insertWidget(1, advancedPanel)' in cpp
assert 'inkLayout->insertWidget(2, m_inkCanvas, 1)' in cpp
# Existing graphics page data and schema remain independent of presentation.
ink = (root / 'src/notes_ink_canvas.cpp').read_bytes()
try:
    baseline = Path('/mnt/data/QTsignalApp_6.13.0_QPM_Build_Fix.zip')
    if baseline.exists():
        with zipfile.ZipFile(baseline) as z:
            for relative in [
                'src/notes_ink_canvas.cpp',
                'src/electrostatic_model.cpp',
                'src/magnetostatic_model.cpp',
                'src/numerical_em_solvers.cpp',
                'src/pec_surface_mom.cpp',
                'src/hybrid_wire_surface_mom.cpp',
            ]:
                assert hashlib.sha256((root/relative).read_bytes()).digest() == hashlib.sha256(
                    z.read('QTsignalApp/'+relative)).digest(), relative
except (KeyError, zipfile.BadZipFile) as exc:
    raise AssertionError('Baseline integrity verification failed') from exc
# Hook for target Qt Test, run by QPM/CMake on Windows.
cmake=(root/'tests/qt_6700/CMakeLists.txt').read_text()
assert 'add_executable(notes_layout_6131' in cmake
assert 'QT_QPA_PLATFORM=offscreen' in cmake
print('QTsignalApp 6.13.1 notebook compact-layout source/integrity: PASS')
print('PENDING: QPM Qt 6.11 rebuild and Qt Test notes_layout_6131')
