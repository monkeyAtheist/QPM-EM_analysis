# QTsignalApp 6.5.1 — recette IHM Qt/QPM (à exécuter sur Windows)

Ce fichier est une **procédure**, pas un résultat de test. Le moteur numérique C++ a été testé ; les comportements Qt nécessitent le build/rendu sur la machine cible.

## Calculateur dimensionnel

1. Ouvrir **Calculator / Notes → Variables & units** et charger l'exemple. Vérifier `force` au lieu de `F`, l'absence d'erreur et le résultat SI `E ≈ 1797,51 V/m`.
2. Saisir `A = 9 m²`; vérifier `9 m²`. Remplacer par `A = (9 m)^2`; vérifier `81 m²`. Tester `A = sqrt(9 m²)` → `3 m`, `A = 3 cm^2` → `0,0003 m²`, `A = 2*cm` → `0,02 m`.
3. Tester `m = 2`, puis `F = 2 N` : erreur claire de symbole réservé. `force = 2 N` doit réussir. Tester `E = ke*(2 nC)/(10 cm)^2` → validé.
4. Enregistrer une feuille de calcul personnalisée, fermer/rouvrir. Vérifier qu'elle n'a **pas** été modifiée ou écrasée automatiquement.
5. Si une installation 6.5.0 a sauvegardé le modèle exact `F = q*E`, l'ouvrir : seule cette feuille par défaut doit être automatiquement migrée en `force = q*E`. Les feuilles personnalisées utilisant `F` doivent demander un renommage explicite.

## Antenna sizing et Wave / frequency

1. Calculer une antenne à 144 MHz, puis changer **uniquement** la fréquence : `Create in Antenna designer`, `Copy f`, `Copy length`, `Copy result` doivent se désactiver et le texte indiquer « recalcul nécessaire ».
2. Recalculer ; vérifier que les boutons se réactivent. Si la création vers Designer est disponible, vérifier que la nouvelle antenne utilise le résultat recalculé.
3. Passer en mode **Primary dimension → frequency**. Entrer une dimension valide, calculer, vérifier la fréquence renvoyée ; ensuite fournir une entrée invalide (zéro), recalculer : **aucun transfert** et **aucune copie du dernier résultat** ne doivent être possibles.
4. Pour Wave / frequency, changer `εr`, `μr`, l'unité ou l'entrée : les boutons Copier doivent se désactiver jusqu'au recalcul. Vérifier les valeurs SI copiées après recalcul.
5. Recommencer au DPI Windows **100 %**, **125 %** et **150 %** et redimensionner la fenêtre.

## Notes (test de persistance, avec relancement de l'application)

1. Rédiger « Notes avant import » et confirmer l'autosauvegarde ; fermer puis rouvrir l'application.
2. Préparer un `.txt` contenant « Notes importées ». Utiliser **Open .txt**, puis choisir **Cancel** : le texte initial doit rester inchangé.
3. Choisir **Yes** au deuxième essai : « Notes importées » doit être affiché et sauvegardé.
4. Fermer et rouvrir l'application, cliquer **Restore backup**, confirmer : le texte initial « Notes avant import » doit revenir.
5. Cliquer à nouveau **Restore backup** : le texte importé doit revenir (échange réversible).
6. Si le fichier est absent/non lisible, l'import doit être refusé sans toucher au carnet. Si l'enregistrement du backup QSettings échoue (dossier de profil non accessible), l'import doit également être annulé.

## Gates de la release

- Vérificateur numérique : `python3 tests/verify_6510.py` → PASS attendu.
- Compilation QPM/Qt Widgets Windows → à exécuter.
- Création/restore Notes + fermeture/redémarrage → à exécuter.
- Aucun changement de schéma `.qta` ni des kernels physiques validés.
