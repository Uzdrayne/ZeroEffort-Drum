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
| T1 | Tests A01-A04, A10, E01, E02, E06, E07 | ⬜ | | |
| T2 | Rejeu `replay` / `compare` | ⬜ | | |
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

## Écarts avec les spécifications

- **Emplacement des fonctions** : `toSnapshot` / `restoreSnapshot` / `measuresToVar` sont des fonctions statiques de `struct DrumAnalysisSnapshot` (ami de `DrumAnalysis`), pas des méthodes de `DrumAnalysis` : la classe de mesure reste inchangée. Le doc 07 disait « dans la classe DrumAnalysis ».
- **Cinquième bloc `input`** (réglages de départ) ajouté au format (le doc 01 en prévoyait quatre) : sans lui, `propose()` ne peut pas être rejoué. Doc 01 mis à jour.
- **Double garde de l'export** : option CMake **et** variable d'environnement (la décision validée ne prévoyait que la variable) ; c'est ce que demande le doc 01 (« jamais dans un build de production »). Pour l'utiliser : configurer avec `-DZED_ANALYSIS_EXPORT=ON`.
- **Tests d'abord** : le test et le code ont été écrits ensemble (le seul « rouge » constaté est un échec de compilation) ; les rejets sont vérifiés par une garde qui prouve que le texte modifié existe bien.
- **Mesures exportées** : seulement celles qui existent déjà (L01, L02, L04, L06, D01, H01, trames par phase). Les nouvelles viendront avec J2.

## Questions ouvertes

- L'export réel (session Auto dans Cubase) n'a pas été exercé : à tester sur la machine avec Cubase, avec un build `-DZED_ANALYSIS_EXPORT=ON`.
- Le snapshot enregistre la proposition issue de `propose()` (phase 0), pas la version finale après les étapes de calibration (`refineThreshold`, `finish`) : c'est ce que le rejeu compare.

## Prochaine action

T1 : tests A01-A04, A10, E01, E02, E06, E07 (document 02).
