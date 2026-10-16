---
applyTo: "Source/Drum/**,Source/Kit/**,Source/ARA/**,tests/**,CodeUpdate/**"
---
# ZeroEffort Drum : règles du chantier « analyse v1 »

## Projet
Plug-in de mixage de batterie (VST3 + ARA), JUCE 8, Windows x64. Le dépôt est PRIVE :
n'envoie rien hors de la machine, n'ajoute aucune dépendance reseau sans demander.

## Spécifications (source de vérité)
- CodeUpdate/01_cahier_des_charges_mesures.md  : quoi mesurer
- CodeUpdate/02_programme_de_test.md           : comment tester
- CodeUpdate/03_comparaison_brut_mixe.md       : méthode brut/mixé
- CodeUpdate/04_calcul_des_presets.md          : calcul des réglages
- CodeUpdate/05_decisions_questions_ouvertes.md: décisions (remplacent les questions ouvertes de 01-04)
- CodeUpdate/06_extensions_futures.md          : registre de rôles, nouveaux éléments
- CodeUpdate/08_sous_bus.md                    : sous-bus par instrument (Kick Bus, Snare Bus, ...)
En cas de conflit : 05 > 08 > 01-04 > 06. Si une règle manque ou est ambiguë, demande, ne devine pas.

## Règles de travail
1. Lis le code existant AVANT de proposer une modification. Ne suppose pas l'existence de
   fichiers ou de fonctions que tu n'as pas vus.
2. Tests d'abord : écris le test, vois-le échouer pour la bonne raison, puis implemente.
3. Petits pas : une étape = un commit avec un message clair. Ne mélange pas refactor et
   changement de comportement dans le même commit.
4. Aucun changement de comportement audible sans le dire. Pour toute modification qui change
   une proposition de réglage : incrémente analysisVersion, régénère les rapports de comparaison,
   et signale-le clairement. NE METS JAMAIS A JOUR un "cas doré" tout seul : c'est l'utilisateur qui valide à l'oreille.
5. Temps réel : pas d'allocation, pas de verrou, pas d'I/O dans le chemin audio (processBlock).
   Les calculs lourds restent dans le thread d'analyse.
6. Style : suis le style du code existant (namespace drum, identifiants anglais, chaînes
   d'interface sans accents, commentaires brefs en anglais). N'ajoute pas de bibliothèque
   sans demander.
7. Ne supprime ni ne renomme rien d'existant sans accord. Pas de commande git destructive
   (reset --hard, push --force, clean -fd, suppression de branche). Ne lance aucune lecture
   audio, ne copie aucun bundle dans un dossier système, et ne change aucun identifiant de
   paramètre ni code plug-in (règles de .github/copilot-instructions.md).
8. Garde les documents synchronisés : quand une mesure ou une règle est implémentée, mets à jour
   la colonne "État" du document correspondant dans le même commit.
9. Après chaque tâche, donne : ce qui est fait, les fichiers modifiés, les tests ajoutés,
   les commandes pour les lancer, les écarts avec les spécifications, les questions ouvertes.
10. Si une hypothèse chiffrée de la spec (marquée H dans 05) est utilisée, nomme-la en constante
    centralisée avec un commentaire "H: à calibrer", jamais en nombre magique dispersé.

## Définition de "terminé"
Le code compile (tâche « Build and Test Release », preset windows-vs2019), les trois suites CTest
existantes passent, les nouveaux tests passent, aucun avertissement nouveau, la spec est à jour,
et le résumé de la règle 9 est fourni.
