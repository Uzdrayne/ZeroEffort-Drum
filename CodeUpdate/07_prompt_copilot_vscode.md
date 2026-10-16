# Prompts de codage pour GitHub Copilot dans VS Code

**Document 7** | Version 0.2 | Basé sur les documents 01 à 06 et 08

Ce fichier contient **tout ce qu'il faut pour lancer le codage dans VS Code avec Copilot** : la préparation du dépôt, un fichier d'instructions Copilot (règles permanentes), un prompt de démarrage et un prompt par jalon. Le projet reste dans VS Code : aucun autre outil ni agent externe.

Le dépôt a déjà `.github/copilot-instructions.md` (build `windows-vs2019`, trois suites CTest, règles temps réel, IDs de paramètres à préserver, pas de lecture audio sans accord). **Ces règles restent valables** ; la section 2 y ajoute seulement les règles propres à ce chantier.

Constats vérifiés sur le dépôt (v0.10.0) qui modifient la préparation :

- Système de build : **CMake** (pas de Projucer), C++17, JUCE 8.0.6. Tests existants dans `tests/` : à **étendre** (ne pas créer une cible `drumtest` en double, voir J-T1).
- `DrumAnalysisSnapshot.h` (cité par J1) **n'existe pas** dans le dépôt : J1 l'écrit à partir de zéro, d'après la section 7 du document 01.
- `analysisVersion` et `toSnapshot()` **n'existent pas** : J1 les introduit. Toute évolution de l'analyse doit aussi invalider les caches ARA (`Source/ARA/CacheValidity.h`, `RegionCache.h`) : à vérifier en phase 0.
- `Kit::Role` ne connaît que Kick In/Out, Snare Top/Bottom, Tom, Tom basse, Hi-hat, Overheads, Room. Kick Sub, Hi-hat Bottom et les sous-bus du document 08 demandent une extension préalable de Kit / Phase.
- Le corpus (`wav_test_file/`, 5 WAV) est trop petit pour **calibrer** les valeurs H du document 05 : jusqu'à constitution d'un corpus, les jalons restent sur des signaux synthétiques.

---

## 1. Préparation (à faire une fois)

1. **Git** (déjà fait) : tag `v0.10.0` sur la version de départ (retour arrière possible avec `git checkout v0.10.0`), `MajorUpdate/` supprimé et commité.
2. Branche de travail : `feature/analyse-v1` (le travail ne touche pas `main` tant qu'il n'est pas validé).
3. Les documents restent dans `CodeUpdate/` (source de vérité). Si le chantier est validé, on pourra les déplacer vers `docs/spec/` en mettant à jour les chemins.
4. Ajouter le fichier d'instructions de la section 2 : `.github/instructions/analyse-v1.instructions.md`. VS Code le charge automatiquement pour les fichiers ciblés par `applyTo`.
5. Vérifier que **le projet compile et que les tests existants passent** : tâche VS Code « Build and Test Release » (preset `windows-vs2019`). Noter la durée et le résultat de référence.
6. **Une conversation Copilot (mode Agent) par jalon** : ouvrir un nouveau chat, joindre `CodeUpdate/07_prompt_copilot_vscode.md` et `docs/PLAN_IMPLEMENTATION.md`, un commit (ou plusieurs petits) par étape.

## 2. Instructions Copilot du chantier (`.github/instructions/analyse-v1.instructions.md`)

Ce fichier **complète** `.github/copilot-instructions.md` sans le remplacer.

```markdown
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
   fichiers ou de fonctions que tu n'as pas vus (ex. ProfileAssembly.h, DrumControlNames.h).
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
```

## 3. Prompt de démarrage : phase 0 (lecture et plan, aucun changement de code)

À coller en début de première session :

```text
Tu travailles sur mon plug-in de mixage de batterie (JUCE 8, VST3 + ARA, Windows x64).
Lis d'abord .github/copilot-instructions.md et .github/instructions/analyse-v1.instructions.md,
puis les documents de CodeUpdate/ dans l'ordre (01 à 06, puis 08).

OBJECTIF DE CETTE PHASE : explorer le dépôt et me remettre un plan. NE MODIFIE AUCUN
FICHIER DE CODE. Tu peux seulement créer docs/PLAN_IMPLEMENTATION.md.

1. Explore le dépôt : cibles CMake, suites CTest existantes (tests/) et tâches VS Code pour
   les lancer, conventions de code. Confirme la liste des constats de l'en-tête de ce document.
2. Lis en détail : Source/Drum/DrumAnalysis.h, DrumProposal.h, les profils de Source/Drum/Profiles
   (Kick, Snare, Tom, HiHat, Overheads, Room, MasterBus, ProfileAssembly.h), Source/Kit,
   Source/ARA/CacheValidity.h et RegionCache.h, et Source/UI/DrumControlNames.h.
3. Compare le code actuel aux documents 01 et 04 : pour chaque mesure du document 01,
   dis si elle existe (fichier et fonction), partiellement, ou pas ; pour chaque constante du
   document 04, vérifie que la colonne "Actuel" correspond bien au code et signale les écarts.
4. Vérifie les risques R1 à R6 du document 04 et les problèmes 1 à 6 de la section 8 du
   document 01 en lisant le code ; dis lesquels sont confirmés, infirmés ou incertains, avec
   les lignes de code concernées.
5. Propose un ordre de réalisation en jalons (J1, T1, T2, J2, J3, T3, registre de rôles, …),
   avec pour chacun : fichiers touchés, tests à ajouter, risques, critère de réussite.
6. Liste au plus 5 questions bloquantes qui nécessitent ma décision.

Écris le résultat dans docs/PLAN_IMPLEMENTATION.md, résume-le-moi en 15 lignes maximum,
et ATTENDS MA VALIDATION avant de coder.
```

## 4. Prompts de jalon

Chaque prompt commence par : *« Lis docs/PLAN_IMPLEMENTATION.md et les instructions du chantier (.github/instructions/analyse-v1.instructions.md). »* Je l'ai omis dans les blocs pour les raccourcir ; ajoutez-le en tête.

### J1 : export et rechargement des snapshots (aucun changement de comportement)

```text
Jalon J1. Objectif : sérialiser et recharger une analyse, sans modifier aucune proposition.

1. Écris DrumAnalysisSnapshot.h (il n'existe pas encore) d'après la section 7 du document 01
   et intègre-le à la classe DrumAnalysis : toSnapshot(), restoreSnapshot(), toMeasures().
   Introduis analysisVersion et fais-le entrer dans la validité des caches ARA
   (Source/ARA/CacheValidity.h) : propose le changement avant de le faire si le format de cache évolue.
2. Ajoute un hook d'export : à la fin de chaque analyse, si une option "dossier de developpement"
   est activée (désactivée par défaut, jamais dans un build de production), écrire un JSON
   { meta, analysis, measures, proposal } comme décrit en section 7 du document 01.
   La proposition est écrite avec les réglages par IDENTIFIANT de paramètre, pas par indice.
3. Test de boucle complète : analyse (signal synthétique) -> toSnapshot -> restoreSnapshot ->
   propose() doit donner les mêmes réglages que l'analyse d'origine (tolérance relative 1e-6),
   pour Kick, Snare et Tom.
4. Aucune modification des profils.

Livrable : code, test, résumé selon la règle 9 des instructions du chantier.
```

### T1 et T2 : programme de test `drumtest` et rejeu

```text
Jalons T1 + T2 (document 02, sections 3, 5.1, 5.2, 5.6 et 10).

T1 : étends l'infrastructure de tests existante (tests/, CTest). Ne crée une cible console
"drumtest" que si le rejeu (T2) ne peut pas vivre dans les exécutables de test actuels ; dans ce
cas, justifie-le d'abord. Ajoute les tests A01 à A04 et A10, et les propriétés E01, E02, E06, E07.
A10 doit prouver que les mesures ne dépendent pas de la taille de bloc (64, 512, 4096).

T2 : ajoute la commande "replay <dossier>" qui lit des snapshots JSON, appelle propose()
et écrit un rapport, puis "compare <A> <B>" qui produit le rapport décrit en section 8
du document 02 (taux de refus, écarts par réglage, analyses proches d'un seuil, prises à écouter).

Contraintes : les noms de commandes peuvent s'adapter à l'existant ; les tolérances viennent
de la section 7.2 du document 02 ; la sortie existe en Markdown et en JSON.
Ajoute la commande de lancement au README ou à docs/.
```

### J2 : mesures P1 manquantes (additif seulement)

```text
Jalon J2 (document 01). Ajoute SANS MODIFIER le comportement des profils les mesures P1
marquées "À faire" ou "Partiel" : L03 (rms_actif), L05 (plancher de bruit), D02 (crest_actif),
H02 (hits par seconde), H06 (decay T20/T60), S01 (tiers d'octave), S02 (bandes nommées) et P02
(écart attaque/corps).

Règles : utiliser les constantes centralisées marquées H pour la fenêtre active (décision 01-Q1
du document 05) ; exposer les mesures par des fonctions const de DrumAnalysis et dans toMeasures() ;
un test synthétique par mesure (document 02, famille A) ; aucune allocation dans push().
Les profils ne lisent pas encore ces mesures.
```

### J3 : corrections des problèmes 1 et 2 (changement de comportement contrôlé)

```text
Jalon J3 (document 01, section 8, problèmes 1 et 2 ; document 02, tests A08 et A09).

Étape A (tests d'abord) : écris A09 (crest actif contre crest global avec silence) et un test
qui met en évidence le problème 1 (trames d'attaque classées en bleed). Montre qu'ils ECHOUENT
sur le code actuel. Écris aussi A08 (doubles pédales à 180 bpm) marqué "échec attendu".

Étape B : corrige le problème 1 (exclure ou classer après coup les trames dont le centre précède
la frappe). Incrémente analysisVersion. Régénère le rapport de comparaison sur le corpus de
test et montre-moi l'effet sur airGain du kick.

Étape C : fais lire le crest actif (D02) par KickProfile pour la décision d'activation du
compresseur, derrière une constante nommée, et montre l'effet sur le corpus.

NE METS PAS A JOUR de cas doré. Liste-moi les prises à écouter. Un commit par étape.
```

### T3 : défauts artificiels Kit / Phase

```text
Jalon T3 (document 02, section 5.4, tests D01 à D04).

Crée un générateur de défauts : retard (0,5 à 10 ms), inversion de polarité, retard +
inversion, sur des signaux synthétiques d'abord (kick, caisse claire, overheads simulés),
puis sur 3 à 5 kits réels du dossier de corpus que je te désignerai.
Vérifie que le module Kit / Phase retrouve la bonne réponse dans les tolérances de la
section 7.2. Les cas hors tolérance doivent produire un refus explicite, jamais un mauvais retard.
Ne modifie pas l'algorithme de corrélation : rapporte seulement les écarts.
```

### Registre de rôles (refactor sans changement de comportement)

```text
Jalon "registre de rôles" (document 06, sections 2 et 5).

Objectif : introduire un RoleDescriptor (données) et un moteur commun, sans changer aucune
proposition pour Kick, Snare et Tom.
1. Avant de toucher au code : génère un jeu de cas dorés de référence (snapshots + propositions
   actuelles) pour Kick, Snare, Tom, dans les 3 styles et 3 intensités (0, 50, 100 %).
   Ces cas servent de filet : la sortie après refactor doit être IDENTIQUE (tolérance 1e-6).
2. Introduis RoleDescriptor et migre un rôle à la fois, en commençant par Snare (le plus simple).
3. A chaque étape, lance les cas dorés ; si un écart apparaît, arrête-toi et explique-le.
```

### Sous-bus SB1 à SB3 (document 08) : identification, mesures, tests

À lancer **après** le registre de rôles et la prise en charge de plusieurs micros par instrument dans Kit / Phase.

```text
Jalons SB1 à SB3 (document 08, sections 3, 6, 8, 9 et 10).

Avant de coder : lis le module Kit / Phase et le mécanisme de partage entre instances, puis
réponds aux trois questions de la section 12 du document 08 (le registre peut-il publier des
mesures ? lire des trims proposés ? le mode Bus actuel se généralise-t-il ?). Si une réponse
est non, propose une solution AVANT de continuer et attends ma validation.

SB1 : introduis GroupDescriptor, le rattachement des micros à un sous-bus par groupe de kit et
famille d'instrument, et le type d'instance "Sous-bus" (selecteur Piste / Sous-bus / Bus batterie).
Un sous-bus sans micros enregistres doit fonctionner comme un role ordinaire (decision 08-Q3).

SB2 : ajoute en LECTURE SEULE les mesures SB01, SB02, SB03, SB06, SB07, SB08, calculees
a partir des mesures publiees par les instances micro. Aucun effet sur les presets.

SB3 : ecris les tests S01 a S08 du document 08. Utilise des signaux synthetiques d'abord.
S01 doit donner +4,77 dB pour trois micros identiques alignes ; S07 (un seul micro) ne doit
produire ni erreur ni valeur inventee.

Ne modifie pas le comportement des instances micro existantes. Un commit par jalon.
```

### Modèle pour les jalons suivants

```text
Jalon <nom> (documents <liste>, sections <liste>).
Objectif : <une phrase>.
Etapes : tests d'abord, puis implémentation, puis rapport de comparaison.
Contraintes : <comportement inchangé / changement contrôlé>, instructions du chantier.
Livrable : code, tests, spec à jour, résumé selon la règle 9.
```

Les jalons suivants (J4 détecteurs par rôle, T5 overheads et room, calcul des cibles, Snare Bottom et Kick Sub, sous-bus SB1 à SB7) suivent ce modèle ; leur contenu se déduit des documents 01 (section 10), 02 (section 10) et 06 (section 5).

## 5. Conseils d'utilisation

- **Un chat Copilot (mode Agent) par jalon.** Une conversation longue accumule du contexte et finit par oublier les règles ; ouvrez un nouveau chat et repartez des instructions du chantier et du plan à chaque fois.
- **Demandez un plan avant de coder** pour tout jalon qui touche à plus de trois fichiers.
- **Relisez les diffs** avant de valider, surtout dans `DrumAnalysis.h` et les profils : une erreur de seuil ne casse pas la compilation.
- **Ne laissez pas le modèle valider seul** un « cas doré » ou une calibration : l'oreille reste la vôtre.
- **Gardez le plan à jour** (`docs/PLAN_IMPLEMENTATION.md`) en fin de session : c'est la mémoire entre les sessions.
- **Les hypothèses chiffrées** (40 dB, 400 ms, 0,8, k = 0,5…) restent des points de départ : elles doivent être des constantes centralisées, faciles à changer.

## 6. Points restant à confirmer en phase 0

- Si `ProfileAssembly.h` compense l'effet de l'EQ sur les seuils du compresseur (risque R1, document 04).
- Si le format de cache ARA (`RegionCache.h`) doit changer avec `analysisVersion`.
- Si le registre Kit (`KitRegistry`) peut publier des mesures et relayer des trims (document 08, section 12).
- Constitution d'un corpus de WAV (10 kits minimum) pour sortir du synthétique et calibrer les valeurs H.

Le prompt de la phase 0 est fait pour confirmer ces points avant d'écrire une seule ligne.
