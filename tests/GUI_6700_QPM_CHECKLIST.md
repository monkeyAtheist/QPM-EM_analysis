# QTsignalApp 6.7.0 — recette Windows/QPM (à exécuter)

## Build
1. Ouvrir `QTsignalApp.qtproject.json` dans QPM (Qt 6.11 / MinGW 64-bit).
2. Faire Clean, Rebuild Debug et Release. Vérifier qu'`src/antenna_project_document.cpp`
   et `src/app_diagnostics.cpp` sont compilés, contrairement à `matrice.cpp` et
   `include/ElectroMagnetisme.cpp`.
3. Vérifier que l'archive n'importe pas l'ancien `compile_commands.json` généré
   sur un autre ordinateur. QPM pourra en régénérer un au besoin.

## Diagnostic et continuité
4. Démarrer l'application depuis le dossier de build et depuis un emplacement
   en lecture seule. Menu Help > Open diagnostics folder : répertoire existant et
   accessible. `Show diagnostics log path` indique le fichier actif.
5. Générer un warning Qt non bloquant si possible. Vérifier `QTsignalApp.log`,
   la date et la catégorie; fermer puis rouvrir : **le log précédent reste présent**.
6. Avec un fichier log grossi au-delà de 2 MiB, vérifier les archives `.1..3`
   et un fichier courant lisible. Les opérations d'échec de fichier doivent
   rester diagnostiquables depuis la sortie QPM.

## Persistance antenne et compatibilité
7. Charger `examples/dipole_exact_halfwave_100MHz.qta`, vérifier les fils/port,
   sauvegarder et recharger. Aucun décalage de coordonnées/échelle.
8. Charger successivement les fixtures `tests/fixtures/qta_legacy_unversioned.json`,
   `qta_future_v20.json` et `qta_invalid_zero_length_wire.json` via le filtre JSON.
   Le legacy doit migrer en mémoire sans modifier le fichier original ; les deux
   autres doivent être rejetés **sans effacer la géométrie déjà chargée**.
9. Sauvegarder sous `.qta` ; vérifier le format v19 conservé, le même nombre de
   wires/feeds/surfaces et le comportement Notes global/projet.

## GUI et régressions
10. Calculator : `ke*(2 nC)/(10 cm)^2` ≈ `1.79751 kV/m`, copie du résultat,
    modification de l'expression, puis impossibilité de copier l'ancien résultat.
11. Notes : copier/coller, sauvegarder en global et projet, redémarrer, vérifier
    que les deux contenus restent distincts ; sauvegarder en dossier lecture seule.
12. Faire les contrôles aux DPI 100/125/150 %, clavier, thème clair/sombre.
13. Tester Dipôle 100 MHz, sweep et affichage du diagramme après ce refactoring ;
    les solveurs physiques n'ont pas changé, mais l'intégration Qt est à confirmer.
14. Lancer les trois tests QTest si le module Qt6Test est installé. Consigner
    les résultats au lieu de marquer « PASS » sans exécution.
