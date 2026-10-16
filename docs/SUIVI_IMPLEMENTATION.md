# Suivi de l'implémentation : analyse v1

Mis à jour à chaque étape. Plan et décisions : [PLAN_IMPLEMENTATION.md](PLAN_IMPLEMENTATION.md).
Spécifications : `CodeUpdate/01` à `08`. Branche : `feature/analyse-v1` (départ : tag `v0.10.0`).

Légende : ⬜ à faire · 🔄 en cours · ✅ terminé · ⏸ en réserve

## Jalons

| Jalon | Contenu | État | Commit | Validation |
|---|---|---|---|---|
| Phase 0 | Lecture du code, plan, décisions | ✅ | `8e25dd8`, `6736b8c` | Plan validé par l'utilisateur |
| Build | Compatibilité VS 2019 / VS 2022 | ✅ | `6736b8c` | CTest 3/3, 0 avertissement (VS 2022) |
| J1 | Snapshot JSON, `analysisVersion`, export développeur, test de boucle | ✅ | `20cd588` | CTest 3/3, 0 avertissement (VS 2022) ; 540 rejeux identiques à 1e-6 ; export réel non testé (pas de Cubase) |
| T1 | Tests A01-A04, A10, E01, E02, E06, E07 (+E08, E09, E10) | ✅ | `79c196e` | CTest 3/3 ; 7 200 propositions ; mutation de E07 détectée |
| T2 | Rejeu `--replay` / `--compare` / `--export-wav` | ✅ | `79c196e` | CTest 3/3 ; essai sur les 4 WAV du dépôt (Kick, Snare, Tom, OvH) |
| J2 | Mesures P1 additives (L03, L05, D02, H02, H06, S01, S02, P02) | ✅ | `35a4b32` | CTest 3/3, 0 avertissement ; profils inchangés (équivalence legacy) ; `push()` sans allocation (compteur vérifié) |
| J3 | Corrections 1 et 2 (désactivées par défaut) | ✅ | `4ce081b` | CTest 3/3, 0 avertissement ; test du problème 1 d'abord rouge (bleed/onset 0,307), vert avec le correctif (6e-6) ; équivalence legacy inchangée par défaut ; **aucun cas doré mis à jour** |
| Registre de rôles | `RoleDescriptor` | ⏸ | | |
| T3 | Défauts Kit / Phase (D01-D10) | ✅ | (ce commit) | CTest 3/3, 0 avertissement ; 4 écarts de Kit / Phase **rapportés, non corrigés** ; mutation fine du retard (+3 %) détectée |
| Sous-bus SB1-SB7 | | ⏸ | | |

## Journal

### J1 : snapshot et export

Fichiers : `Source/Drum/DrumAnalysisSnapshot.h` (nouveau), `DrumAnalysis.h` (`analysisVersion`, friend), `DrumProposal.h` (`profileVersion`), `DrumAutoPreset.h` (`editableAnalysis()`), `Source/ARA/ZeroEffortDrumProcessor.cpp` (hook), `CMakeLists.txt` (option + test), `tests/SnapshotTests.cpp` (nouveau), `tests/ZeroEffortDrumProcessorTests.cpp` (appel).

1. ✅ `analysisVersion = 1` / `profileVersion = 1` (constantes centralisées, commentaire listant ce qui les fait changer).
2. ✅ `DrumAnalysisSnapshot.h` : `toSnapshot`, `restoreSnapshot`, `measuresToVar`, réglages par identifiant de paramètre, validation stricte à la lecture (version, schéma, plages, taille des tableaux, valeurs non finies).
3. ✅ Test de boucle : Kick, Snare, Tom × 48 et 96 kHz × 10 rôles × 3 styles × 3 intensités = 540 rejeux (522 valides) identiques à 1e-6 ; mesures ; fichier ; export ; 7 cas de rejet ; taille < 300 Ko.
4. ✅ Hook d'export dans l'analyse Auto, derrière `-DZED_ANALYSIS_EXPORT=ON` + variable `ZED_ANALYSIS_EXPORT_DIR`. Compilé avec l'option activée ; la fonction d'export est testée directement.
5. ✅ Build + CTest, doc 01 mis à jour (section 7 et tableau des jalons).

Commandes : `powershell -File scripts\Build.ps1 -Action Verify` (ou la tâche VS Code « Build and Test Release »). Les messages `PASS: snapshot …` apparaissent en lançant directement `build-vs2022\ZeroEffortDrumTests_artefacts\Release\ZeroEffortDrumTests.exe`.

### T1 : tests de mesures et propriétés

Fichier : `tests/PropertyTests.cpp` (nouveau, appelé depuis `main` des tests).

- ✅ A01 (pic -6,02, RMS -9,03, crest 3,01 dB à ±0,05), A02 (silence refusé avec message, 10 rôles), A03 (sinusoïde écrêtée : ratio > 1 %, refus « sature » sur Snare, Tom, Hi-hat, Bus), A04 (NaN : refus sur 10 rôles).
- ✅ A10 : analyses identiques bit à bit (bloc `analysis` du snapshot) avec des blocs de 64, 257, 512 et 4096, à 44,1, 48 et 96 kHz.
- ✅ E01, E02, E06, E07, plus E08 (déterminisme), E09 (refus avec message) et E10 (refus = réglages d'origine inchangés) : 7 200 propositions (48 signaux générés, 10 rôles, 3 styles, 5 intensités, `output`/`input`/`bypass` d'origine aléatoires) ; 4 560 valides et 2 640 refusées.
- Vérification par mutation : avec E07 durci à -4 dB, le test échoue (message « E07 output above -3 dB »), puis le fichier a été restauré.
- Aucune anomalie du code existant révélée : A10 et E01-E10 passent sans modification des profils.

### T2 : rejeu et comparaison

Fichiers : `Source/Drum/DrumReplay.h` (nouveau), `Source/Drum/DrumAnalysis.h` (`envelopeContrastDb`, accesseur const), `tests/ReplayTests.cpp` (nouveau, tests et commandes), `tests/ZeroEffortDrumProcessorTests.cpp` (aiguillage des options), `CMakeLists.txt`.

- ✅ `--replay <dossier> [préfixe]` : rejoue tous les snapshots, compare à la proposition enregistrée, rapport Markdown + JSON ; les snapshots illisibles ou d'une autre `analysisVersion` sont listés comme ignorés avec la raison.
- ✅ `--compare <A.json> <B.json> [préfixe]` : refus par rôle et par style (alerte > 5 points), changements valide/refus, réglages modifiés (moyenne, max, fichier), proximité des seuils du code (± 5 %), liste « à écouter » (10 prises au plus).
- ✅ `--export-wav <wav> <dossier> <rôle> [style] [intensité]` : fabrique un snapshot depuis un WAV (T-C, utile dès que le vrai corpus sera sur une machine) ; même repli de canaux que l'analyse ARA.
- ✅ Tests : dossier de 5 snapshots + 2 fichiers invalides, rapport sans différence, différences simulées (gain de 3 dB, fréquence de 2 % sous le seuil, bascule refus/valide), entrées présentes d'un seul côté, aller-retour JSON, codes de sortie des commandes, WAV synthétique exporté puis rejoué.
- Essai réel (analyse seule, aucune lecture audio) sur `wav_test_file` : 4 snapshots de 104 Ko, tous valides, rejeu déterministe, aucune différence.
- Commandes : voir le doc 02, section 3 (état).
### J2 : mesures P1 additives

Fichiers : `Source/Drum/DrumAnalysis.h` (état + accesseurs const + `MeasureConstants`), `Source/Drum/DrumAnalysisSnapshot.h` (état et mesures exportés), `tests/MeasureTests.cpp` (nouveau), `CMakeLists.txt`, `tests/ZeroEffortDrumProcessorTests.cpp`, doc 01.

- ✅ **L03 / D02** : énergie de chaque fenêtre de 10 ms rangée dans le même histogramme de pics (`windowEnergyByPeak`, 101 cases) ; le seuil actif est calculé à la lecture (le P95 n'est connu qu'à la fin). `activeRmsDb()`, `activeCrestDb()`, `activeThresholdDb()`, `activeWindowCount()`.
- ✅ **L05** : `noiseFloorDb()` (P10). **H02** : `hitsPerSecond()`, `hitCount()`.
- ✅ **H06** : suivi des chutes après chaque frappe sans allocation (référence = pic des 4 premières fenêtres, chute de 20 dB, au plus 1 s) ; histogramme des durées, médiane à 10 ms près, il faut 3 frappes ; frappes interrompues par la suivante comptées à part (`decayCensoredHits()`). T60 = 3 × T20 (extrapolé, documenté comme tel).
- ✅ **S01** : `thirdOctaveDb(0..30)` (31 colonnes `sp_third_oct_<fréquence nominale>`). **S02** : `bandRelativeDb()` et 7 colonnes `band_*` (partition de 20 Hz à min(20 kHz, 0,45 × fréquence)). **P02** : `attackBodyGapDb(rôle)`.
- ✅ Valeurs indisponibles = NaN dans le code, `null` dans le JSON (jamais 0 inventé).
- ✅ `analysisVersion` passe à **2** (le snapshot contient de nouveaux champs d'analyse, obligatoires à la lecture).
- ✅ Tests : L03/D02 (sinusoïde −9,03 dBFS / 3,01 dB), L05 (lit un lit de bruit à −50,5 dBFS), A09 (kick isolé contre dense), silence = indisponible, H02, H06 à 44,1/48/96 kHz (T20 attendu 82 et 164 ms, tolérance ±30 ms), S01 (sinusoïde 1 kHz dans la bonne bande, voisines à > 25 dB, bruit blanc +10 dB par décade à 1 dB près), S02 (partition = 1 à 3 %), limite 0,45 × fréquence, P02 (+6 dB pour un clic doublé), snapshot (colonnes, restauration identique, champs endommagés refusés), H02 de la section performance : aucune allocation dans `push()` (compteur d'allocations du thread, auto-vérifié).
- Les profils ne lisent aucune de ces mesures : `testProposalEquivalence` (référence figée) passe sans modification.
### J3 : corrections des problèmes 1 et 2 (désactivées par défaut)

Fichiers : `Source/Drum/DrumAnalysis.h` (politique des trames d'attaque, `phasePower`, `bleedTrebleRatio`, `analysisVersion` = 3), `Source/Drum/DrumProposal.h` (`ProposalPolicy`, `ProposalDefaults`), `Source/Drum/Profiles/KickProfile.h`, `Source/Drum/DrumAutoPreset.h`, `Source/Drum/DrumAnalysisSnapshot.h`, `Source/Drum/DrumReplay.h`, `tests/KnownProblemTests.cpp` (nouveau), `tests/ReplayTests.cpp` (options), doc 01 et 02.

- **Étape A (tests d'abord)** : le test du problème 1 (bande 6-12 kHz, clic d'attaque sur un plancher de bruit très faible) a été écrit contre l'accesseur en lecture seule `phasePower` et a **échoué** sur le code existant : `bleed / onset = 0,307` (le bleed contient 31 % de la puissance du clic). A08 (doubles pédales à 180 bpm) est écrit en « échec attendu » : 24 frappes comptées sur 48.
- **Étape B** : `excludeLeadFrames` (désactivé par défaut, constante `MeasureConstants::excludeLeadFramesByDefault`) : les trames dont le centre précède la frappe de moins d'une demi-FFT (≈ 4 par frappe) ne vont plus dans le bleed ; elles restent dans le spectre global (`spectrumFrames()` les compte, `leadFrameCount()`). Résultat : `bleed / onset = 6,4e-6`. Le comportement par défaut est vérifié (0,307, aucune trame écartée) et l'équivalence avec la référence figée passe toujours.
- **Étape C** : `ProposalPolicy::compressorUsesActiveCrest` (désactivé par défaut, `ProposalDefaults`) : `KickProfile` lit `activeCrestDb()` pour la seule décision d'activation du compresseur (et du multibande qui la suit) ; seuil de 10 dB non recalibré. Test synthétique (salves de 0,15 s par seconde : crest global 11,3 dB / actif 3,0 dB) : ON par défaut, OFF avec la politique, aucun autre réglage ne change, les autres rôles ne sont pas concernés.
- **Snapshots et rejeu** : la politique d'analyse (`analysis.excludeLeadFrames`, `leadFrames`) et la politique de proposition (`meta.policy`) sont enregistrées ; `--export-wav ... --exclude-lead-frames --active-crest` ; `--replay ... --active-crest|--no-active-crest` surcharge la politique enregistrée. Test : surcharge « off » sur un snapshot « on » = 2 réglages modifiés (compresseur et multibande), une prise à écouter.
- **Effet sur les vrais WAV du dépôt** (analyse seule, rock, intensité 1) : Kick 741 frappes, 2 964 trames mises de côté (bleed : 5 408 → 2 444 trames), rapport aigu du bleed 0,0090 → 0,0012 ; Snare 0,92 contre 1,13 ; Tom 0,61 contre 0,69 ; Overheads 10,94 contre 10,99 ; **aucun réglage ne change** (`airGain` = 0 avant et après, le cut d'aigu du Kick ne démarre qu'au-dessus de 1 ; crest du Kick 25,4 dB / actif 24,5 dB : compresseur ON dans les deux cas). Le rapport de comparaison (`--compare`) indique « Rien à écouter ».
- **Prises à écouter** : aucune avec ces 5 WAV. L'effet sur l'`airGain` du Kick ne se verra que sur un enregistrement où le bleed aigu est proche du seuil ; à valider sur le vrai corpus.
### T3 : défauts artificiels Kit / Phase

Branche `feature/t3-kit-phase`. Fichiers : `tests/KitTests.cpp` (`FaultKit`, `testKitFaults`, surcharge de `check` pour `juce::String`), `tests/MeasureTests.cpp` (D08 côté mesures), doc 02 (section 5.4 : tableau des résultats). **Aucun fichier de `Source/` modifié** (consigne du plan : ne pas toucher à l'algorithme de corrélation, rapporter les écarts).

- Générateur synthétique à physique connue : micro de proximité qui entend le coup en premier, overheads qui l'entendent X ms plus tard, polarité, diaphonie de la caisse claire, bruit, gain, overheads décalés.
- ✅ D01-D04 (22 cas dont 7 retards de kick de 0,5 à 10 ms, caisse claire 1 à 10 ms, polarité, +7 ms inversé), D05, D06, D07, D08, D09, D10, et D01-D04 sur le kit réel du dépôt (retard +3 ms, polarité inversée, les deux : exacts à 0,001 ms).
- Les tests sont écrits « juste ou refusé » : un refus propre passe, une mauvaise correction silencieuse échoue.
- Vérification par mutation : retard multiplié par 1,03 dans `KitAnalysis.h` : les tests existants passent, D01 échoue (2,000 → 2,110 ms) ; la mutation a été annulée (`git checkout`).
- **Quatre constats sur Kit / Phase** (à décider avec l'utilisateur, hors périmètre de T3) :
  1. **Biais de +0,05 ms sur le kick** (2-3 échantillons) contre ± 1 échantillon demandé ; la caisse claire est exacte.
  2. **Retards au-delà de la fenêtre de recherche** (caisse claire > 10 ms, kick entendu 5 ms avant son micro) : une correction fausse est proposée sans refus. Le plug-in en mode Suggérer l'afficherait comme une mesure normale (confiance r ≈ 0,3-0,4).
  3. **Diaphonie de la caisse claire à partir de −16 dB dans le micro du kick** : le retard du kick suit la caisse claire. Pistes de correction (non faites) : ne garder que les événements dont le niveau est proche du pic du micro, ou refuser quand les paires de frappes donnent deux retards distincts.
  4. `diaphonie_db` (K04) n'existe pas.
- Ces constats sont des tests « échec attendu » (comme A08) : ils échouent dès que le comportement change.
## Écarts avec les spécifications

- **Emplacement des fonctions** : `toSnapshot` / `restoreSnapshot` / `measuresToVar` sont des fonctions statiques de `struct DrumAnalysisSnapshot` (ami de `DrumAnalysis`), pas des méthodes de `DrumAnalysis` : la classe de mesure reste inchangée. Le doc 07 disait « dans la classe DrumAnalysis ».
- **Cinquième bloc `input`** (réglages de départ) ajouté au format (le doc 01 en prévoyait quatre) : sans lui, `propose()` ne peut pas être rejoué. Doc 01 mis à jour.
- **Double garde de l'export** : option CMake **et** variable d'environnement (la décision validée ne prévoyait que la variable) ; c'est ce que demande le doc 01 (« jamais dans un build de production »). Pour l'utiliser : configurer avec `-DZED_ANALYSIS_EXPORT=ON`.
- **Tests d'abord** : le test et le code ont été écrits ensemble (le seul « rouge » constaté est un échec de compilation) ; les rejets sont vérifiés par une garde qui prouve que le texte modifié existe bien.
- **Mesures exportées** : seulement celles qui existent déjà (L01, L02, L04, L06, D01, H01, trames par phase). Les nouvelles viendront avec J2.

- **T2 sans cible `drumtest`** : les commandes sont des options de `ZeroEffortDrumTests.exe` (décision du plan). Les noms `replay` / `compare` deviennent `--replay` / `--compare` ; `--export-wav` remplace `analyse`. `faults`, `golden`, `perf` non faits.
- **Rubrique « Cas dorés »** du rapport (doc 02, section 8) absente : il n'existe pas encore de cas validé à l'oreille (T6).
- **Mesures de proximité** : `bass/total` utilise `bandEnergy(20, 16000)` (le Kick utilise `min(16000, 0,45 × fréquence)` : identique au-dessus de 35,6 kHz d'échantillonnage).
- **Tests d'abord (T1)** : les tests A et E ont été écrits après lecture du code, mais ils passent tous sans modification des profils ; leur capacité à échouer est prouvée par la mutation de E07, pas par un état rouge initial.
- **J2, règle de la fenêtre active** : le seuil est plafonné au P95 et le silence numérique (≤ −100 dBFS) n'est jamais actif ; sans cela un signal sans dynamique (sinusoïde, bruit constant) n'avait aucune fenêtre active, et le silence donnait −100 dB au lieu de « indisponible ». Ajouts au doc 01.
- **J2, A09** : le doc 02 attend un crest actif identique à ±1 dB ; la mesure synthétique donne 1,18 dB (global : 4,76 dB). Le test exige < 1,5 dB et 3 × plus stable que le global. Cause : le seuil relatif au P95 baisse quand le jeu est épars. À régler par la courbe de calibration de 01-Q1 (nécessite des prises réelles) : voir questions ouvertes.
- **J2, H06** : T20 est un temps de passage direct à −20 dB (pas une régression −5/−25 dB) à 10 ms de résolution, mesuré depuis la détection de la frappe ; T60 = 3 × T20. Détecteur de frappes réglé pour le grave (problème 4 du doc 01) : H06 n'est fiable que pour Kick et Toms.
- **J2, S01** : valeurs en dB de la puissance FFT non étalonnée (pas de forme relative) ; la bande 20 Hz est indisponible à 48 kHz (aucune case FFT dans 17,8-22,4 Hz).
- **J3, périmètre du problème 2** : le crest actif n'est lu que pour l'activation du compresseur du Kick (étape C du prompt). Le refus `crest < 6`, `ProfileAssembly` (`crestMinimum`) et `DrumAutoPreset::configureFinishing` lisent toujours le crest global.
- **J3, mécanisme** : deux réglages à l'exécution (`setExcludeLeadFrames`, `ProposalPolicy`) plutôt que de simples constantes, pour pouvoir tester et comparer les deux comportements ; les constantes nommées fixent les valeurs par défaut, toutes à « désactivé ». Leur activation par défaut reste à décider après écoute.
- **J3, correctif du problème 1** : « exclure » (et non « classer après coup ») ; les trames écartées restent dans le spectre global pour ne pas modifier les mesures de forme.
- **J3, analysisVersion = 3** (nouveaux champs d'analyse dans le snapshot).
- **T3, tolérances** : ± 1 échantillon demandé sur signal pur, non tenu pour le kick (voir constat 1) ; tolérances posées à 0,1 ms (kick) et 0,025 ms (caisse claire), 0,2 ms sur kit réel.
- **T3, kits réels** : un seul kit réel (kick, caisse claire, OH L/R de `wav_test_file`) au lieu de 3 à 5.
- **T3, D01** : interprété comme « les overheads entendent le kick X ms après son micro » (le plug-in ne peut que retarder un micro).
## Questions ouvertes

- L'export réel (session Auto dans Cubase) n'a pas été exercé : à tester sur la machine avec Cubase, avec un build `-DZED_ANALYSIS_EXPORT=ON`.
- **Seuil de la fenêtre active** (40 dB, 12 dB, plafond P95) : à calibrer sur des prises réelles (courbe crest actif / seuil, plateau à ±0,5 dB pour ±10 dB). Le vrai corpus n'est pas sur cette machine.
- **Activation par défaut** des deux correctifs J3 : à décider après une écoute dans un hôte (pas possible sur cette machine). Procédure : exporter les prises avec --exclude-lead-frames --active-crest, --replay / --compare, écouter les prises listées.
- **Kit / Phase** : faut-il corriger les constats 1 à 3 de T3 (refus hors fenêtre, diaphonie, biais du kick) ? Ils changent le comportement de l'alignement : à décider, avec écoute dans un hôte.
- Le snapshot enregistre la proposition issue de `propose()` (phase 0), pas la version finale après les étapes de calibration (`refineThreshold`, `finish`) : c'est ce que le rejeu compare.

## Prochaine action

Vague 1 (J1, T1, T2, J2, J3) fusionnée dans main ; T3 terminé sur eature/t3-kit-phase. Suite possible : T4 (A05-A07, A11-A13), registre de rôles, sous-bus ; ou corriger les constats Kit / Phase après décision.
