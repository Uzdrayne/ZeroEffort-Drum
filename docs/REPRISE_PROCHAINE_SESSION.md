# Reprise de session : ZeroEffort Drum, chantier « analyse v1 »

Document de passation écrit le 16 octobre 2026. Il contient un **prompt à coller** (section 1) puis le contexte complet pour qu'un assistant reparte sans rien redécouvrir.

---

## 1. Prompt à coller au début de la prochaine session

```text
Tu reprends le chantier « analyse v1 » du plug-in ZeroEffort Drum (JUCE 8, VST3 + ARA, Windows).

1. Lis d'abord, dans cet ordre : .github/copilot-instructions.md, .github/instructions/analyse-v1.instructions.md,
   docs/REPRISE_PROCHAINE_SESSION.md (ce fichier), docs/SUIVI_IMPLEMENTATION.md, docs/PLAN_IMPLEMENTATION.md.
2. Vérifie l'état du dépôt (git status, git log, branche courante) et dis-moi en 5 lignes où on en est.
3. Ne modifie rien avant que je t'aie dit quel chantier on prend. Propose-moi ensuite 3 options, de la
   plus sûre à la plus risquée, selon ce qui est disponible aujourd'hui (Cubase ? vrai corpus WAV ?).
4. Règles : aucun changement audible sans me le dire, tout correctif de comportement reste désactivé par
   défaut tant que je ne l'ai pas écouté, ne valide jamais un cas doré à ma place, tiens à jour
   docs/SUIVI_IMPLEMENTATION.md à chaque étape, un commit par étape, une branche par chantier.

Aujourd'hui je dispose de : [Cubase : oui / non] [vrai corpus WAV : oui / non] [temps : ...]
Ce que je veux faire : [à compléter, ou « propose-moi »]
```

---

## 2. Où on en est

- **Dépôt** : `Uzdrayne/ZeroEffort-Drum`. Version du plug-in : **0.10.0** (inchangée, rien d'audible n'a changé par défaut).
- **`main`** contient tout le travail des deux premières pull requests (#1 et #2) : phase 0, build VS 2019 / 2022, J1, T1, T2, J2, J3, T3, T4. Tag `v0.10.0` = version de départ.
- **Aucune branche de travail ouverte.** Pour continuer : `git switch main`, *Pull*, puis `git switch -c feature/<nom>`.
- **Suivi détaillé** : `docs/SUIVI_IMPLEMENTATION.md` (jalons, journal, écarts avec les spécifications, questions ouvertes). Plan et décisions validées : `docs/PLAN_IMPLEMENTATION.md`. Spécifications : `CodeUpdate/01` à `08`.

### Ce qui est fait

| Jalon | Résultat |
|---|---|
| Build | `scripts/Build.ps1` choisit VS 2019 si installé, sinon VS 2022 (`-Toolchain VS2019\|VS2022` pour forcer). Presets `windows-vs2019` et `windows-vs2022` (dossier `build-vs2022/`, ignoré par git). |
| J1 | `Source/Drum/DrumAnalysisSnapshot.h` : snapshot JSON d'une analyse (réglages par identifiant de paramètre), rechargement strict. `analysisVersion` (actuellement **3**) et `profileVersion` (1). Export développeur : `-DZED_ANALYSIS_EXPORT=ON` **et** variable `ZED_ANALYSIS_EXPORT_DIR`. |
| T1 | `tests/PropertyTests.cpp` : A01-A04, A10, propriétés E01, E02, E06-E10 sur 7 200 propositions. |
| T2 | `Source/Drum/DrumReplay.h`, `tests/ReplayTests.cpp` : options `--export-wav`, `--replay`, `--compare` de `ZeroEffortDrumTests.exe` (rapport Markdown + JSON, liste « à écouter »). |
| J2 | Mesures additives dans `DrumAnalysis.h` : L03, L05, D02, H02, H06, S01, S02, P02 (valeurs indisponibles = `null`, jamais 0). `push()` ne fait aucune allocation (test avec compteur). |
| J3 | Deux correctifs **désactivés par défaut** : trames d'attaque écartées du bleed (problème 1) et crest actif pour le compresseur du Kick (problème 2). A08 (doubles pédales) est un « échec attendu ». |
| T3 | `tests/KitTests.cpp` : défauts artificiels D01-D10 (synthétique + kit réel de `wav_test_file`). Aucun fichier de `Source/` modifié. 4 constats sur Kit / Phase, **non corrigés**. |
| T4 | `tests/SignalTests.cpp` : A05, A06, A07, A11 ; mesure S04 (`spectralSlopeDbPerOctave`). |

État des tests à la fin de la session : build VS 2022 sans avertissement, CTest 3/3 (`RegionCacheTests`, `ZeroEffortDrumTests` ≈ 80 s, `ARADiscoveryTests`).

### Ce qui n'a PAS pu être vérifié (aucun Cubase, aucun vrai corpus sur le PC de travail)

- L'export JSON réel depuis une session Auto dans Cubase.
- L'écoute des deux correctifs J3.
- Toute calibration des valeurs « H » (hypothèses à calibrer) : fenêtre active 40 dB / 12 dB, seuil de compresseur du Kick à 10 dB de crest, 400 ms de trame longue, cohérence 0,8, `k = 0,5`, etc. Seuls 5 WAV de test sont dans `wav_test_file/` (usage du test précédent, pas le corpus définitif).

---

## 3. Décisions déjà prises (ne pas rediscuter sans raison)

1. Build compatible VS 2019 et VS 2022.
2. Export JSON : option CMake + variable d'environnement, désactivé par défaut, aucun contrôle dans l'interface.
3. Les correctifs qui changent le son restent **désactivés par défaut** derrière une constante nommée, jusqu'à une écoute par l'utilisateur dans un hôte. Le test `LegacyDrumProposalReference` reste vrai tant qu'ils sont désactivés.
4. L'intensité garde sa « dose » non linéaire (×1,75 à 100 %) : on n'y touche pas dans cette vague.
5. Le vrai corpus de WAV reste **hors du dépôt**. Les 5 WAV de `wav_test_file/` servent aux tests actuels sans être le corpus définitif ; leur retrait de git reste à décider (sans réécrire l'historique).
6. Pas de cible `drumtest` séparée : les commandes sont des options de `ZeroEffortDrumTests.exe`.
7. Ne jamais valider un « cas doré » à la place de l'utilisateur.

## 4. Constats ouverts, à décider avec l'utilisateur

**Kit / Phase** (issus de T3, tous reproduits par des tests « échec attendu ») :
1. Le retard du kick est lu avec un biais de +0,05 ms (2-3 échantillons) ; la caisse claire est exacte. Spécification : ±1 échantillon.
2. Au-delà de la fenêtre de recherche (caisse claire > 10 ms, kick entendu 5 ms avant son micro), l'outil propose une correction **fausse sans refuser**.
3. Diaphonie de la caisse claire dans le micro du kick à partir de −16 dB : le retard du kick suit la caisse claire.
4. La mesure `diaphonie_db` (K04) n'existe pas.

**Analyse** :
5. Le détecteur de frappes a un seuil absolu (niveau grave 0,0003, environ −70 dBFS) : un bruit de fond rose à −54 dBFS ajoute des frappes fantômes (risque R3).
6. La puissance S01 absolue dépend de la taille de FFT (+6 dB à 96 kHz) : seules les mesures relatives (S02, S04) se comparent entre fréquences d'échantillonnage.
7. Problème 3 du doc 01 (doubles pédales à 180 bpm sous-comptées : 24 frappes pour 48 jouées) : A08 le matérialise, la correction est en J4.
8. A09 : écart de crest actif de 1,2 dB au lieu de ±1 dB (seuil relatif au P95 sensible à la densité du jeu) : à régler avec la calibration de 01-Q1.

## 5. Menu pour la prochaine session

Choisir selon ce qui est disponible.

### Si Cubase est disponible (le plus utile)
1. Compiler avec `-DZED_ANALYSIS_EXPORT=ON`, définir `ZED_ANALYSIS_EXPORT_DIR`, lancer une analyse Auto, vérifier qu'un `snapshot_*.json` apparaît, le rejouer avec `--replay`.
2. Écouter les prises avec `--exclude-lead-frames --active-crest` (export) puis `--compare` : si c'est bon, activer par défaut `MeasureConstants::excludeLeadFramesByDefault` et `ProposalDefaults::compressorUsesActiveCrest`, incrémenter `profileVersion` / `analysisVersion`, **régénérer la référence figée** (`scripts/FreezeProposalReference.ps1`, `tests/LegacyDrumProposalReference.h`) et documenter. C'est un changement audible : une branche et un commit dédiés.

### Si le vrai corpus est disponible (hors dépôt)
3. Générer les snapshots avec `--export-wav` (un dossier par kit), rejouer, lire le rapport de comparaison. Constituer les jeux de réglage et de validation (doc 02, section 4).
4. Calibrer les constantes H une par une (doc 04, section 6) en commençant par la fenêtre active (courbe `crest_actif` / seuil) ; une constante à la fois, tout noté dans le suivi.

### Sans Cubase ni corpus (sûr, aucun changement audible)
5. **Registre de rôles** (`RoleDescriptor`, doc 06 sections 2 et 5) : d'abord figer des cas de référence Kick / Snare / Tom × 3 styles × 3 intensités (snapshots + propositions actuelles), puis migrer un rôle à la fois en vérifiant des sorties identiques à 1e-6. Commencer par Snare.
6. **Retirer les 5 WAV de git** (sans réécrire l'historique : `git rm --cached`, mise à jour de `.gitignore` et de `ZED_WAV_DIRECTORY`) si l'utilisateur le décide.
7. **Mesure K04** `diaphonie_db` (additive) pour objectiver le constat 3 de Kit / Phase.
8. Ouvrir une branche de correctifs Kit / Phase **désactivés par défaut** (refus hors fenêtre, protection contre la diaphonie), sur le modèle de J3, si l'utilisateur le demande.

### Plus lourd (à planifier, pas à improviser)
9. **J4 / J4b** : détecteurs de frappes par rôle (corrige A08), analyse continue Overheads / Room avec entrée stéréo (débloque A12 et A13).
10. **Sous-bus SB1-SB7** (doc 08) : prérequis = étendre le registre Kit (publier des mesures, relayer des trims) et ajouter les rôles Kick Sub / Hi-hat Bottom. Répondre d'abord aux trois questions de la section 12 du doc 08.

---

## 6. Commandes utiles

```powershell
# Build + tests (détecte VS 2019 ou 2022) : équivaut à la tâche VS Code « Build and Test Release »
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Build.ps1 -Action Verify

# Compiler seulement
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Build.ps1 -Action Build

# Exécutable de tests (la suite complète tourne sans option, ≈ 80 s)
build-vs2022\ZeroEffortDrumTests_artefacts\Release\ZeroEffortDrumTests.exe
#   chemin du dossier build/ si VS 2019 ; variable ZED_KIT_VERBOSE=1 affiche les mesures de Kit / Phase

# Outils de rejeu
ZeroEffortDrumTests.exe --export-wav <wav> <dossier> <role 0-9> [natural|rock|metal] [intensite 0-1] [--position 0-3] [--no-exclude-lead-frames] [--no-active-crest]
#   --position = kitMic : 0 principal (Kick In, Snare Top), 1 secondaire (Kick Out, Snare Bottom), 2 gauche, 3 droite
#   J3 est actif par defaut ; les deux options --no-... redonnent l'ancien comportement
ZeroEffortDrumTests.exe --replay  <dossier> [prefixe] [--active-crest|--no-active-crest]
ZeroEffortDrumTests.exe --compare <rapportA.json> <rapportB.json> [prefixe]
ZeroEffortDrumTests.exe --kit [--guards] [--start s] [--seconds s] <role>=<wav mono> ...   # analyse Kit / Phase seule, avec K04
#   rôles : 0 Kick, 1 Snare, 2-4 Toms, 5 Tom basse, 6 Hi-hat, 7 Overheads, 8 Room, 9 Bus

# Export développeur (build avec l'option, jamais en production)
cmake --preset windows-vs2022 -DZED_ANALYSIS_EXPORT=ON      # puis définir ZED_ANALYSIS_EXPORT_DIR
```

## 7. Pièges rencontrés (à éviter)

- **PowerShell et backticks** : dans une chaîne entre guillemets doubles, `` `f `` `` `a `` `` `n `` sont des caractères de contrôle. Plusieurs textes de doc ont été abîmés ainsi. Pour éditer les `.md`, utiliser l'outil d'édition de fichier, ou des chaînes entre guillemets simples. Après une édition de doc, chercher les caractères de contrôle (`[\x00-\x08\x0B\x0C\x0E-\x1F]`).
- **Un test qui passe du premier coup ne prouve rien** : vérifier sa capacité à échouer (mutation temporaire du code ou du seuil, puis `git checkout` du fichier).
- **Les premiers essais de configuration CMake peuvent échouer** sur le téléchargement de JUCE (réseau) : relancer.
- **Windows** : `build-vs2022/` est dans un dossier OneDrive (lent) ; envisager un `binaryDir` hors OneDrive.
- **Git** : l'assistant ne peut pas pousser (pas d'identifiants dans son terminal). L'utilisateur publie la branche (*Publish Branch*), ouvre la pull request sur GitHub, la fusionne, puis fait *Pull* sur `main`.
- **`check()` de `KitTests.cpp`** accepte maintenant `const char*` et `juce::String` ; les autres fichiers de test ont leur propre `check`.
- **Trailer de commit** : ajouter `Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>`.
- **Ne pas lancer de lecture audio** sans confirmation de l'utilisateur ; ne pas copier de bundle dans un dossier système ; ne pas changer d'identifiants de paramètres ni de codes de plug-in.

## 8. Fichiers clés

| Sujet | Fichier |
|---|---|
| Mesures et analyse | `Source/Drum/DrumAnalysis.h` |
| Réglage des profils | `Source/Drum/Profiles/*.h`, `Source/Drum/DrumAutoPreset.h`, `Source/Drum/DrumProposal.h` |
| Snapshots et rejeu | `Source/Drum/DrumAnalysisSnapshot.h`, `Source/Drum/DrumReplay.h` |
| Kit / Phase | `Source/Kit/KitAnalysis.h`, `KitRegistry.*`, `KitTypes.h` |
| Tests | `tests/*.cpp`, appelés depuis `main` de `tests/ZeroEffortDrumProcessorTests.cpp` |
| Référence figée des propositions | `tests/LegacyDrumProposalReference.h`, `scripts/FreezeProposalReference.ps1` |
| Spécifications | `CodeUpdate/01` à `08` (en conflit : 05 > 08 > 01-04 > 06) |
| Suivi, plan, passation | `docs/SUIVI_IMPLEMENTATION.md`, `docs/PLAN_IMPLEMENTATION.md`, ce fichier |
