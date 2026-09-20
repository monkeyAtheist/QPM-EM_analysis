import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
project = json.loads((root/'QTsignalApp.qtproject.json').read_text())
main = (root/'src'/'main.cpp').read_text()
designer = (root/'src'/'antenna_designer_widget.cpp').read_text()
plot = (root/'src'/'field_profile_plot.cpp').read_text()
change = (root/'CHANGELOG_APP.md').read_text()

checks = {
    'version': project.get('version') == '5.27.0' and project.get('packaging', {}).get('productVersion') == '5.27.0' and 'QStringLiteral("5.27.0")' in main,
    'field hover': all(x in plot for x in ['m_hoverActive', 'leaveEvent(QEvent *event)', 'QStringLiteral("%1: %2").arg(m_xAxisLabel']),
    'smith hover': all(x in designer for x in ['Hover any sweep point to inspect Γ, normalized impedance, return loss and VSWR.', 'QStringLiteral("Return loss = %1 dB")', 'QStringLiteral("VSWR = %1")']),
    'polar hover': all(x in designer for x in ['Hover the pattern to inspect angle and level.', 'Normalized field = %1', 'Relative level = %1 dB']),
    'radiation 3d colormap': all(x in designer for x in ['setColorMapEnabled(bool enabled)', 'setLegendVisible(bool enabled)', 'QStringLiteral("Color map")', 'QStringLiteral("Legend")', 'Approx. gain ≈ %1 dBi']),
    'changelog': change.startswith('# QTsignalApp 5.27.0') and 'Interactive result views and 3D gain color map' in change and 'project schema remains version 19.' in change,
    'qta': 'root.insert(QStringLiteral("version"), 19)' in designer,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit('5.27 static verification FAILED: ' + ', '.join(failed))
print('QTsignalApp 5.27.0 static verification: PASS')
