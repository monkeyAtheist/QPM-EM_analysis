# Tests Qt optionnels — QTsignalApp 6.7.0

Ces tests sont **hors du build de l'application QPM**. Ils nécessitent le SDK Qt 6
(`Core`, `Widgets`, `Test`) et CMake. Ils ne remplacent pas la validation de
l'application entière sous Windows et n'exécutent aucun solveur MoM lourd.

Depuis la racine `QTsignalApp`, avec Qt 6.11 / MinGW 64-bit dans le PATH :

```powershell
cmake -S tests/qt_6700 -B build/qt_6700 -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH="C:/Qt/Qt6.11.0/6.11.0/mingw_64"
cmake --build build/qt_6700 --parallel
ctest --test-dir build/qt_6700 --output-on-failure
```

Adapter les chemins à l'installation et au compilateur MinGW utilisés par QPM
(un même ABI Qt/compilateur pour la compilation et l'exécution). Sur Windows,
le dossier `bin` de Qt et du compilateur doit être accessible par `PATH`.
Sous Linux, la même configuration fonctionne avec Ninja ou Unix Makefiles si
Qt6Test est installé.

**Tests définis :**

- `project_document_6700` : schéma `.qta v19`, conversion des formats anciens,
  rejet v20, géométrie fil de longueur nulle et très grande version numérique.
- `logging_6700` : création en dossier utilisateur/test, trace `qWarning`,
  conservation des sessions et absence de troncature.
- `gui_smoke_6700` : constructeur du calculateur, évaluation d'une expression
  avec unités, invalidation après modification, saisie et copie du carnet Notes.

Pour le test GUI, CTest utilise `QT_QPA_PLATFORM=offscreen` (utile en CI).
Il faut **aussi** exécuter la checklist interactive `GUI_6700_QPM_CHECKLIST.md`
sur le poste Windows, à DPI 100 %, 125 % et 150 % : le mode offscreen ne valide
pas les boîtes de dialogue natives, le comportement de QSettings utilisateur,
la géométrie 3D ou les interactions de l'application complète.

Le test numérique indépendant de Qt est :

```powershell
python tests/verify_6700.py
```

## 6.8.0 additions

The existing `gui_smoke_6700` target now links `notes_ink_canvas.cpp`.
`ink_6800` tests mouse stroke recording, JSON persistence/invalid JSON
preservation, erase, clear, undo/redo and PNG dimensions. Both should be run
with `QT_QPA_PLATFORM=offscreen` for the automated smoke suite, then repeated
manually on a Windows Surface Pen to validate platform-specific tablet input.

For example:

```bash
cmake -S tests/qt_6700 -B build/qt-tests -G Ninja -DCMAKE_PREFIX_PATH="C:/Qt/Qt6.11.0/6.11.0/mingw_64"
cmake --build build/qt-tests -j 4
ctest --test-dir build/qt-tests --output-on-failure
```
