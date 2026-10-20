---
description: "Règles du chantier analyse, mesures, presets et Kit ; références documentaires consolidées."
applyTo: "Source/Drum/**,Source/Kit/**,Source/ARA/**,tests/**,docs/PROJET.md,docs/SPECIFICATIONS.md"
---
# ZeroEffort Drum : règles du chantier « analyse v1 »

## Projet
Plug-in de mixage de batterie (VST3 + ARA), JUCE 8, Windows x64. Le dépôt est PRIVE :
n'envoie rien hors de la machine, n'ajoute aucune dépendance reseau sans demander.

## Spécifications (source de vérité)
- [SPECIFICATIONS.md](../../docs/SPECIFICATIONS.md) : mesures, export, brut/mixé,
  presets, rôles, sous-bus, tests, décisions et hypothèses.
- [PROJET.md](../../docs/PROJET.md) : état vérifié, problèmes, validations,
  feuille de route, versions et point de reprise.
Les anciens CodeUpdate et plan.md sont dans docs/archives : références historiques,
pas des listes de travaux à rejouer. La spécification consolidée distingue les
décisions des propositions et conserve l'ordre de priorité historique.
Si une règle manque, se contredit ou est ambiguë, demande, ne devine pas.

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
   l'état dans PROJET.md et, si sa portée change, l'exigence dans SPECIFICATIONS.md
   dans le même commit. Ne crée pas de suivi parallèle et ne réécris pas les archives.
9. Après chaque tâche, donne : ce qui est fait, les fichiers modifiés, les tests ajoutés,
   les commandes pour les lancer, les écarts avec les spécifications, les questions ouvertes.
10. Si une hypothèse chiffrée de SPECIFICATIONS.md (marquée H) est utilisée, nomme-la en constante
    centralisée avec un commentaire "H: à calibrer", jamais en nombre magique dispersé.

## Définition de "terminé"
Le code compile (tâche « Build and Test Release », preset windows-vs2019), les trois suites CTest
existantes passent, les nouveaux tests passent, aucun avertissement nouveau, la spec est à jour,
et le résumé de la règle 9 est fourni.
