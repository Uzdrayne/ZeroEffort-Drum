# Plan d'implémentation : analyse v1

Phase 0 (lecture seule, aucun code modifié). Base : tag `v0.10.0`, branche `feature/analyse-v1`.
Sources : `CodeUpdate/01` à `08`, code de `Source/`, `tests/`, `CMakeLists.txt`.

## 1. État du dépôt

| Sujet | Constat |
|---|---|
| Build | CMake, preset `windows-vs2019` (générateur Visual Studio 16 2019), C++17, JUCE 8.0.6. Script : `scripts/Build.ps1` (`Verify` = configure + build Release + CTest). |
| Machine actuelle | Seul Visual Studio **2022** est installé : `Build.ps1 -Action Verify` échoue à la configuration. **Aucune base de référence n'a pu être mesurée** (voir question Q1). |
| Tests | 3 exécutables console CTest : `RegionCacheTests`, `ZeroEffortDrumTests` (`ZeroEffortDrumProcessorTests.cpp`, `KitTests.cpp`, `EditorTests.cpp`, `Screenshots.cpp`), `ARADiscoveryTests`. Pas de cadre de test externe : fonctions `require(...)` maison. |
| Filet existant | `tests/LegacyDrumProposalReference.h` (généré par `scripts/FreezeProposalReference.ps1`) : copie figée de l'ancien code de proposition. `ZeroEffortDrumProcessorTests.cpp` (autour des lignes 445-464) vérifie que `propose()` donne **exactement** la même chose. Tout changement de comportement (J3) cassera ce test : voir Q3. |
| Analyse | Dans un thread de session du processeur (`ZeroEffortDrumProcessor.cpp`, autour des lignes 585-681), **pas** dans `processBlock`. Elle lit le canal 0 seulement (`push(getReadPointer(0), …)`). Le calcul lourd est donc déjà hors du chemin audio. |
| Persistance | Seuls les `Settings` sont archivés (`DrumArchive.h`, magiques `DRA1`/`DRA2`/`DRA3`). L'analyse n'est jamais sauvegardée. |
| Corpus | `wav_test_file/` : 5 WAV de 22,7 Mo (Kick, Snare, Tom B, OvH L/R), **suivis par git** (113 Mo). |
| Identifiants de paramètres | Existent (`controlSpecs[i].id` dans `DrumEngine.h`) : l'export « par identifiant » du doc 01 est faisable. |

**Correction d'un constat du doc 07** : `CacheValidity.h` invalide des objets ARA (région, source), il ne contient aucune version. `analysisVersion` n'a **aucun lien** avec les caches ARA ; il ne sert qu'aux snapshots et aux rapports.

## 2. Mesures du document 01 contre le code

| ID | Mesure | État réel | Où |
|---|---|---|---|
| L01, L02, L06, Q01 | pic, RMS global, écrêtage, invalide | Existant | `DrumAnalysis` (`peakDb`, `rmsDb`, `clipped`, `invalid`) |
| L04 | percentiles d'enveloppe | Existant | `percentile`, `percentileOf` (histogrammes 101 cases de 1 dB, fenêtres 10 ms) |
| L05 | plancher de bruit | Partiel (P10 calculable, non exposé) | histogramme |
| D01 | crest global | Existant | `crestDb` |
| D03 | plage dynamique | Partiel (P20/P90 dans les profils) | `ProfileAssembly`, `KickProfile` |
| H01 | frappes | Existant (détecteur grave 25-250 Hz) | `push()`, ligne 77 |
| P01, P03 | spectres onset/tail/bleed, aigu du bleed | Existant | `DrumAnalysis`, `KickProfile` |
| T01 | corps | Kick : `kickDominantBody` ; Snare/Tom : `spectralPeak` appelé dans `DrumAutoPreset::propose` | |
| T03 | résonance | Kick seulement (`kickTemporalResonance`) | |
| K01-K03, R04 | retard, polarité, cohérence, compat. room | Existant | `Source/Kit/KitAnalysis.h` |
| Tout le reste (L03, L07-L09, D02, D04-D06, H02-H09, S01-S06, P02, P04, T02, T04-T06, ST1-ST4, K04-K06, Q02-Q05, O01-O09, R01-R03) | | **À faire** | |

Constantes du doc 04 (colonne « Actuel ») : celles de `KickProfile`, `SnareProfile` et `TomProfile` correspondent au code. Écarts à connaître :

1. **Intensité** : `DrumAnalyser::propose` convertit l'intensité en « dose » non linéaire (`0,5 + 2,5 × (i − 0,5)` au-dessus de 50 %). À 100 %, les valeurs sont multipliées par **1,75**. Le doc 04 décrit une échelle 0-100 % simple.
2. **Ordre plafond / intensité** (doc 04, règle 4.2) : le code fait l'inverse (`maximumReduction = … × intensity`).
3. **Snare/Tom (`ProfileAssembly`)** : le doc 04 ne décrit pas leurs règles réelles : gate si `P90 − P20 ≥ 18` (Kick : 12), seuil de compression au **P75** (Kick : P95), EQ léger (+1,5 / −2 / +2 dB) seulement hors style Naturel, `defaultSettings()` réinitialisé.
4. **Le crest global** sert à trois endroits, pas un : `KickProfile` (refus si < 6, compresseur si > 10), `ProfileAssembly` (`crestMinimum`) et `DrumAutoPreset::configureFinishing` (écrasement et besoin de transitoire).

## 3. Risques R1-R6 (doc 04) et problèmes 1-6 (doc 01, section 8)

| Réf. | Verdict | Preuve |
|---|---|---|
| R1 seuils sur signal brut | **Confirmé** | Seuil de compression calculé sur `envelopeHistograms` du signal brut ; aucune compensation de l'EQ (Kick jusqu'à +4,5 dB de corps). |
| R2 −35 dB fixe multibande | **Confirmé** | `KickProfile.h` ligne 94 : `settings[base] = -35`. |
| R3 constantes à l'oreille | **Confirmé** | `crest > 10`, `bass / total < 0,15`, `loud − quiet ≥ 12/18`. |
| R4 Snare/Tom fixes | **Confirmé** | `SnareProfile`/`TomProfile` : fréquences en dur (sauf corps du Tom), mesure du corps Snare ajoutée seulement dans `DrumAutoPreset`. |
| R5 `defaultSettings()` | **Confirmé** | `ProfileAssembly` et `KickProfile` repartent de `defaultSettings()`. |
| R6 cumul gate + compresseur + drive | **Confirmé** | Aucun plafond global ; chaque fonction est dosée séparément. |
| 1 trames d'attaque en bleed | **Confirmé** | `DrumAnalysis.h` lignes 108-113. `lastHitSample` n'est mis à jour qu'à la détection (fin de la fenêtre de 10 ms) ; les ~4 trames dont le centre précède la frappe tombent dans `bleedSpectrum` (âge négatif) ou sont classées par rapport à la frappe précédente. |
| 2 crest sur toute la durée | **Confirmé** | voir section 2, point 4. |
| 3 période réfractaire fixe | **Confirmé** | `windows − lastHit ≥ 12` (120 ms) et seuil 1,8 × ligne 77. |
| 4 détecteur réglé pour le kick | **Confirmé** | un seul détecteur (bande 25-250 Hz) pour tous les rôles. |
| 5 résolution 11,7 Hz | **Confirmé** | `rate / 4096` à 48 kHz ; pas d'interpolation parabolique. |
| 6 OH/room et détecteur grave | **Probable** | même détecteur ; `OverheadsProfile`/`RoomProfile` sont de petits profils fixes (non relus en détail). |

Autres constats utiles :

- `DrumAnalysis` est mono et ses membres sont privés (amis : `KickProfile`, `ProfileAssembly`, `MasterBusProfile`, `LegacyProposalReference`). Le snapshot devra être un ami de plus. Spectres : 4 tableaux de 4096 doubles ; un JSON lisible dépasse 300 Ko (doc 05, 01-Q3) : coder les spectres en binaire base64 ou ne garder que les bins utiles.
- Kit/Phase : `KitRegistry` échange de l'**audio** (anneaux) et des `Recommendation` (retard, polarité) ; il ne publie **aucune mesure** ni **trim de gain** (doc 08 section 12, réponses 1 et 2 : non). `Kit::Role` ne connaît ni Kick Sub ni Hi-hat Bottom. Les sous-bus exigent donc une extension du registre.
- `DrumInstrument` contient déjà `master` (« Drum Master Bus ») : le mode Bus existe mais est unique (réponse 3 : à généraliser).

## 4. Jalons proposés

Première vague : **J1, T1, T2, J2, J3**. Le reste est en réserve.

| Jalon | Fichiers touchés | Tests à ajouter | Risques | Critère de réussite |
|---|---|---|---|---|
| **J1** snapshot | `Source/Drum/DrumAnalysisSnapshot.h` (nouveau), `DrumAnalysis.h` (friend + `analysisVersion`), hook d'export dans le thread d'analyse | boucle `analyse → snapshot → restore → propose()` identique à 1e-6 (Kick, Snare, Tom) | taille des spectres ; ne rien écrire hors option développeur | aucune proposition modifiée ; les 3 suites CTest passent |
| **T1** tests A/E | `tests/` (nouveau fichier de test ou extension de `ZeroEffortDrumProcessorTests.cpp`), `CMakeLists.txt` | A01-A04, A10 (blocs 64/512/4096), E01, E02, E06, E07 | A10 peut révéler une dépendance à la taille de bloc existante | tests verts ou écarts documentés |
| **T2** rejeu | outil de rejeu `replay` / `compare` (sur JSON), idéalement dans les exécutables de test | rejeu d'un dossier de snapshots ; rapport Markdown + JSON | dépend de J1 ; ne pas créer de cible superflue | rapport conforme au doc 02, section 8 |
| **J2** mesures P1 | `DrumAnalysis.h` (additif), constantes H centralisées | un test synthétique par mesure : L03, L05, D02, H02, H06, S01, S02, P02 | coût CPU de `push()` ; zéro allocation | profils inchangés ; mesures dans `toMeasures()` |
| **J3** corrections 1 et 2 | `DrumAnalysis.h`, `KickProfile.h`, `ProfileAssembly.h`, `DrumAutoPreset.h` | A09, test du problème 1 (échouent d'abord), A08 « échec attendu » | casse le test `LegacyProposalReference` (Q3) ; change l'audio | rapport avant/après ; **aucun cas doré mis à jour sans toi** |
| Registre de rôles | `Source/Drum/Profiles`, nouveau `RoleDescriptor` | cas dorés Kick/Snare/Tom × 3 styles × 3 intensités | refactor large ; dépend d'un filet figé | sorties identiques à 1e-6 |
| T3 défauts Kit/Phase | `tests/KitTests.cpp` | D01-D04 | corpus réel limité à 5 WAV | refus explicite hors tolérance |
| Sous-bus SB1-SB3 | `Source/Kit/*`, UI | S01-S08 | registre sans mesures ni trims ; rôles manquants | doc 08 section 12 résolu **avant** de coder |
| J4/J4b, J5, SB4-SB7 | | | dépendent de la calibration (corpus) | en réserve |

## 5. Questions bloquantes

1. **Q1 Build** : VS 2019 est absent de cette machine (seulement VS 2022). Faut-il (a) installer VS 2019, (b) ajouter un preset `windows-vs2022` à `CMakePresets.json` et un paramètre à `Build.ps1`, ou (c) un `CMakeUserPresets.json` local (ignoré par git) ? Je recommande (c) : aucun fichier du projet ne change.
2. **Q2 Option développeur** : comment activer l'export JSON ? Proposition : variable d'environnement `ZED_ANALYSIS_EXPORT_DIR` lue dans le thread d'analyse, désactivée par défaut, aucun contrôle dans l'interface.
3. **Q3 `LegacyProposalReference`** : J3 change volontairement l'audio. Proposition : le correctif est derrière une constante nommée (activée par défaut après ton écoute) et le test legacy reste vrai tant qu'elle est désactivée ; on fige ensuite une nouvelle référence.
4. **Q4 Intensité** : conserver la « dose » 1,75× à 100 % (comportement actuel, à documenter) ou l'aligner sur le doc 04 ? Je recommande de **ne pas toucher** dans cette vague.
5. **Q5 WAV dans git** : 113 Mo suivis alors que le doc 03 (03-Q5) les veut hors dépôt. Les laisser (usage actuel des tests) ou les sortir (Git LFS / dossier externe) ?

## 6. Points hors périmètre signalés

- Le doc 08 suppose Kick Sub et Hi-hat Bottom, absents de `Kit::Role` et de `positionNames`.
- Les valeurs H (40 dB, 400 ms, 0,8, k = 0,5…) ne se calibrent pas avec 5 WAV : jalons en synthétique, calibration plus tard.
- Le dossier `build/` se trouve dans un répertoire OneDrive : il est ignoré par git mais synchronisé par OneDrive (lent, risque de conflits) ; à envisager : `binaryDir` hors OneDrive.
