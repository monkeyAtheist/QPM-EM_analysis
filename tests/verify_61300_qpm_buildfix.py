#!/usr/bin/env python3
"""Compile the actual localPins C++ function using minimal QString/QVector stand-ins."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/notes_ink_canvas.cpp').read_text(encoding='utf-8')
start = source.index('QVector<QPointF> NotesInkCanvas::localPins(')
end = source.index('QPointF NotesInkCanvas::pinLocation(', start)
body = source[start:end]
assert 'id.startsWith(QStringLiteral("mos_")) || id.startsWith(QStringLiteral("jfet_"))' in body
assert 'QStringLiteral("mos_") || id.startsWith' not in body

preamble = r'''
#include <cassert>
#include <string>
#include <vector>
#include <iostream>
struct QPointF { double x, y; };
template<class T> using QVector = std::vector<T>;
struct QString {
    std::string value;
    explicit QString(const char* s): value(s) {}
    bool operator==(const QString& rhs) const { return value == rhs.value; }
    bool startsWith(const QString& prefix) const { return value.rfind(prefix.value, 0) == 0; }
};
#define QStringLiteral(s) QString(s)
struct NotesInkCanvas { QVector<QPointF> localPins(const QString& id) const; };
'''
postamble = r'''
int main() {
    NotesInkCanvas canvas;
    const auto check = [&](const char* id, std::size_t expectedSize) {
        assert(canvas.localPins(QString(id)).size() == expectedSize);
    };
    check("transformer", 4);
    check("bjt_npn", 3);
    check("bjt_pnp", 3);
    check("mos_n", 3);
    check("mos_p", 3);
    check("jfet_n", 3);
    check("jfet_p", 3);
    check("opamp", 3);
    check("voltage_dc", 2);
    check("current_source", 2);
    check("ground", 1);
    check("antenna", 1);
    check("junction", 4);
    check("potentiometer", 3);
    check("resistor_iec", 2);
    const auto mos = canvas.localPins(QString("mos_n"));
    assert(mos[0].x == -70 && mos[0].y == 0);
    assert(mos[1].x == 10 && mos[1].y == -70);
    assert(mos[2].x == 10 && mos[2].y == 70);
    std::cout << "PASS: exact localPins() body compiled with C++17 warnings-as-errors; pin cases verified\n";
}
'''
with tempfile.TemporaryDirectory(prefix='qta613pins_') as folder:
    p = Path(folder)
    (p/'pins.cpp').write_text(preamble + '\n' + body + '\n' + postamble, encoding='utf-8')
    cmd=['g++','-std=c++17','-Wall','-Wextra','-Wpedantic','-Werror',
         str(p/'pins.cpp'),'-o',str(p/'pins')]
    subprocess.run(cmd,check=True)
    subprocess.run([str(p/'pins')],check=True)
print('QTsignalApp 6.13.0 QPM pin-function build fix: PASS')
