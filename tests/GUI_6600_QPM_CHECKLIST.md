# Recette Qt/QPM à exécuter sur Windows — 6.6.0

Cette checklist est **à exécuter** : elle ne constitue pas une preuve de test déjà effectué.

1. Compiler Debug/Release avec Qt 6.11 sous QPM, à DPI 100 %, 125 % et 150 %. Réduire la fenêtre : vérifier le défilement des sept sous-onglets, l'absence de commandes tronquées et le basculement de palette.
2. Dans Scientific, évaluer `ke*(2 nC)/(10 cm)^2` : vérifier une valeur d'environ `1.79751 kV/m` avec préfixes, puis `1797.51 V/m` en désactivant la mise à l'échelle. `Copy SI value` doit copier ~1797.51, quel que soit le réglage d'affichage.
3. Tester `Copy displayed value + unit`, `Copy expression`, les 3–12 chiffres significatifs, `Ctrl+L`, le collage d'une formule et l'historique. Après modification de l'expression, les commandes de copie et l'envoi vers Notes doivent se désactiver jusqu'à Calculate.
4. Dans Variables, changer `q` de 2 nC à 3 nC : vérifier que le résultat Scientific antérieur ne peut plus être copié. Recalculer la feuille, puis le résultat Scientific.
5. Depuis Notes, créer un contenu global et redémarrer : vérifier son retour (compatibilité QSettings 6.5.1). Essayer Projet sans fichier `.qta` : l'opération doit être refusée avec indication claire.
6. Sauvegarder `antenna.qta` dans Designer, revenir à Notes et sélectionner Projet. Créer un texte et une feuille de variables distincts ; confirmer la présence de `antenna.qta.notes.md` et `antenna.qta.worksheet.txt`. Basculer Global/Projet à répétition ; aucune des deux portées ne doit écraser l'autre.
7. Sauvegarder une autre antenne `antenna2.qta` ou en charger une. Les notes de `antenna.qta` ne doivent pas apparaître dans le carnet de `antenna2.qta`. Fermer/reprendre l'application, recharger les deux projets puis vérifier les sidecars et l'évaluation de leur feuille.
8. Importer un `.txt` dans un carnet non vide : vérifier la confirmation et Restore backup pour ce carnet. Tester un dossier de projet en lecture seule : la sauvegarde doit échouer visiblement, sans changer de carnet et sans perdre la saisie courante.
9. Antenna sizing : calculer un dipôle 144 MHz et cliquer sur Create in Antenna designer. La prévisualisation doit montrer fréquence, principales dimensions et avertissement de remplacement ; Cancel ne doit rien effacer. Confirmer doit générer la géométrie attendue, avec Undo fonctionnel.
10. Après création d’une antenne depuis Calculator, vérifier le retour au carnet Global et l’absence d’écriture dans les anciens sidecars ; sauvegarder le nouveau `.qta` puis sélectionner ses nouvelles notes de projet.
11. Vérifier les thèmes clair/sombre, zoom, navigation clavier Tab/Entrée, copier/coller dans les champs et notes. L'interface ne doit pas masquer les actions critiques.
