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
| T1 | Tests A01-A04, A10, E01, E02, E06, E07 (+E08, E09, E10) | ✅ | voir ci-dessous | CTest 3/3 ; 7 200 propositions ; mutation de E07 détectée |
| T2 | Rejeu `--replay` / `--compare` / `--export-wav` | ✅ | voir ci-dessous | CTest 3/3 ; essai sur les 4 WAV du dépôt (Kick, Snare, Tom, OvH) |
| J2 | Mesures P1 additives (L03, L05, D02, H02, H06, S01, S02, P02) | ⬜ | | |
| J3 | Corrections 1 et 2 (désactivées par défaut) | ⬜ | | |
| Registre de rôles | `RoleDescriptor` | ⏸ | | |
| T3 | Défauts Kit / Phase | ⏸ | | |
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
## Questions ouvertes

- L'export réel (session Auto dans Cubase) n'a pas été exercé : à tester sur la machine avec Cubase, avec un build `-DZED_ANALYSIS_EXPORT=ON`.
- Le snapshot enregistre la proposition issue de `propose()` (phase 0), pas la version finale après les étapes de calibration (`refineThreshold`, `finish`) : c'est ce que le rejeu compare.

## Prochaine action

J2 : mesures P1 additives (L03, L05, D02, H02, H06, S01, S02, P02), sans modifier les profils.
