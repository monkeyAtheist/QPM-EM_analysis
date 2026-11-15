#!/usr/bin/env python3
"""Lightweight regression check for the Qt ink-toolbar lambda compile issue."""
from pathlib import Path
import re
source = (Path(__file__).resolve().parents[1] / 'src/em_calculator_widget.cpp').read_text(encoding='utf-8')
assert 'auto addInkButton = [&](const QString &label, const QString &tip, auto handler)' in source
assert not re.search(r'\bauto\s*\*\s*addInkButton\s*=', source)
assert source.count('addInkButton(QStringLiteral(') == 5
assert 'connect(button, &QPushButton::clicked, this, handler);' in source
print('QTsignalApp 6.8.0 QPM lambda build fix: PASS (static)')
