# Reprise de session : ZeroEffort Drum, chantier « analyse v1 »

Document de passation, mis à jour le 17 octobre 2026. **Pour l'état global (fait, désactivé, manquant), lire d'abord `docs/ETAT_ACTUEL.md`.** L'interface du plug-in est maintenant entièrement en anglais (documents et rapports développeur en français). Il contient un **prompt à coller** (section 1) puis le contexte complet pour qu'un assistant reparte sans rien redécouvrir.

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
   défaut tant que je ne l'ai pas écouté (ou validé), ne valide jamais un cas doré à ma place, tiens à jour
   docs/SUIVI_IMPLEMENTATION.md à chaque étape, un commit par étape, travail directement sur main.

Aujourd'hui je dispose de : [Cubase : oui / non] [vrai corpus WAV : oui / non] [temps : ...]
Ce que je veux faire : [à compléter, ou « propose-moi »]
```

---

## 2. Où on en est

*(Mis à jour le 17 octobre 2026, fin de la session de travail sur `main`.)*

- **Dépôt** : `Uzdrayne/ZeroEffort-Drum`. Version du plug-in : **0.11.1**. On travaille **directement sur `main`**, un commit par étape (décision du 17/10/2026).
- **Versions d'analyse** : `analysisVersion` **6**, `profileVersion` **2**.
- **Suivi détaillé** : `docs/SUIVI_IMPLEMENTATION.md` (jalons, journal chiffré, écarts, questions ouvertes). Plan : `docs/PLAN_IMPLEMENTATION.md`. Spécifications : `CodeUpdate/01` à `08`.
- **Corpus (hors dépôt)** : `C:\Users\robin\Desktop\Database_drum` : `raw1` à `raw4`, `mix1`, `mix2`, `LISTING.md`, `snapshots\`, `reports\`. Variables d'environnement utilisateur : `ZED_WAV_DIRECTORY` (= `raw3`, pour les tests de kit réel) et `ZED_ANALYSIS_EXPORT_DIR`.

### Ce qui est fait

| Jalon | Résultat |
|---|---|
| Build | `scripts/Build.ps1` (VS 2019 si installé, sinon 2022). `build/` = production (export désactivé) ; `build-export/` = build d'export pour Cubase (`-DZED_ANALYSIS_EXPORT=ON`, ignoré par git). |
| J1, T1, T2, J2, T4 | Snapshot JSON, tests de propriétés, rejeu `--export-wav` / `--replay` / `--compare`, mesures additives, tests de mesure. |
| Export réel | Vérifié dans Cubase le 17/10/2026 ; `meta.position` et `meta.channels` ajoutés ; `ph_bleed_treble_ratio` exporté pour le Kick seulement. |
| J3 | **Activé par défaut** (17/10/2026, validé par l'utilisateur) : trames d'attaque écartées du bleed, crest actif pour le compresseur du Kick. |
| Calibration 01-Q1 | Seuil de la fenêtre active 40 → **55 dB** (script `scripts/CalibrateActiveWindow.py`). Décalage de plancher 12 dB étudié et **conservé** (A09 ≤ 1 dB l'impose). |
| T3, K04, garde-fous Kit / Phase | Mesure K04 (`crosstalkDb`), garde-fous `KitPolicy` (hors fenêtre, diaphonie) **désactivés par défaut**, commande `--kit`. |
| Registre de rôles | Étape 1 faite : `Source/Drum/DrumRole.h` (table de 10 descripteurs), filet `tests/RoleReferenceTests.cpp` (270 propositions gelées). |
| J4a | Détection des doubles frappes (`setDoubleHitDetection`, `--no-double-hits` pour l'ancien comportement), **activée par défaut** (17/10/2026). |
| Corpus | 5 WAV retirés de git ; kits 1 à 4 exportés, K04 mesuré sur 3 kits. `scripts/RunCorpus.ps1` + `corpus.json` (manifeste hors dépôt) lancent toute la chaîne ; voir `docs/Corpus.md`. |
| Radar Auto | `Source/Drum/DrumAutoRadar.h` (12 axes, courbes source / visé / obtenu, liste des changements), `Source/UI/AutoRadarPanel.h` (panneau animé dans l'éditeur), `owner.setAutoApplyWhenReady(false)` : la proposition attend **Appliquer / Ignorer**. Captures de test : `AutoRadar_*.png` à côté de `ZeroEffortDrumTests.exe`. Voir le suivi. |
| Brut / mixé (doc 03) | `--pair` (paire à un filtre : alignement, courbe d'égalisation équivalente, cohérence, indices) et `--bus` (bus = somme filtrée de ses micros, régression multi-entrées) : `Source/Drum/DrumPairAnalysis.h`, `tests/PairTests.cpp`. Réel : Kick Bus expliqué à 0,90 par In + Out + Sub, Snare Bus à 0,82 avec son sample. Spec 03 corrigée (3.1 bis, 4, 5.1, 7.1, 12, 13). |

État des tests : build VS 2019 sans avertissement, CTest 3/3 (`RegionCacheTests`, `ZeroEffortDrumTests` ≈ 70-100 s, `ARADiscoveryTests`).

### Ce qui reste non vérifié

- L'écoute (Cubase) des correctifs activés ou en réserve : aucun réglage ne change sur les 38 prises du corpus pour J3, la calibration 01-Q1 et le registre ; J4a ne change qu'un réglage (Kick Out du kit 1, < 9 Hz).
- Les valeurs « H » non calibrées : seuil de 10 dB du compresseur du Kick, 400 ms de trame longue, cohérence 0,8, `k = 0,5`, seuils K04 (−15 dB) et hors fenêtre (rapport 1,2). Elles demandent des paires brut / mixé par piste, que le corpus ne fournit pas (mixés = bus).

---

## 3. Décisions déjà prises (ne pas rediscuter sans raison)

1. Build compatible VS 2019 et VS 2022.
2. Export JSON : option CMake + variable d'environnement, désactivé par défaut, aucun contrôle dans l'interface.
3. Les correctifs qui changent le son restent **désactivés par défaut** derrière une constante nommée, jusqu'à une écoute par l'utilisateur dans un hôte. Le test `LegacyDrumProposalReference` reste vrai tant qu'ils sont désactivés.
4. L'intensité garde sa « dose » non linéaire (×1,75 à 100 %) : on n'y touche pas dans cette vague.
5. Le vrai corpus de WAV reste **hors du dépôt**. Les 5 WAV de `wav_test_file/` ont été **retirés de git et du projet le 17/10/2026** (sans réécrire l'historique) ; les tests de kit réel lisent le dossier `ZED_WAV_DIRECTORY` (option CMake ou variable d'environnement, ex. `Database_drum\raw3`) et sont ignorés s'il manque.
6. Pas de cible `drumtest` séparée : les commandes sont des options de `ZeroEffortDrumTests.exe`.
7. Ne jamais valider un « cas doré » à la place de l'utilisateur.

## 4. Constats ouverts, à décider avec l'utilisateur

**Kit / Phase** (issus de T3) :
1. Le retard du kick est lu avec un biais de +0,05 ms (2-3 échantillons) ; la caisse claire est exacte. Spécification : ±1 échantillon. **Non traité** (demande de reprendre la corrélation du Kick).
2. Hors fenêtre de recherche : garde-fou `KitPolicy::refuseOutsideWindow` écrit, **désactivé par défaut**.
3. Diaphonie : mesure K04 faite ; garde-fou `KitPolicy::refuseCrosstalk` (seuil −15 dB, H fragile : le Kick In du kit 1 est à −16 dB) écrit, **désactivé par défaut**. Aucun des deux n'est relié à l'interface.
4. Le kit 4 (autre jour) donnait une cohérence de 59 : saut de cycle du Kick sur les overheads, **corrigé et activé** (`KitPolicy::reconcileOverheads`, cohérence 96,6) ; voir le suivi.

**Analyse** :
5. Le détecteur de frappes a un seuil absolu (niveau grave 0,0003, environ −70 dBFS) : un bruit de fond rose à −54 dBFS ajoute des frappes fantômes (risque R3).
6. La puissance S01 absolue dépend de la taille de FFT (+6 dB à 96 kHz) : seules les mesures relatives (S02, S04) se comparent entre fréquences d'échantillonnage.
7. Problème 3 (doubles pédales) : correctif J4a **activé par défaut**.
8. Le Hi-hat est refusé par K04 (−1,4 dB) : sa mesure Kit / Phase par les frappes du kit est douteuse de toute façon.

## 5. Menu pour la prochaine session

Choisir selon ce qui est disponible.

### Avec Cubase (écoute)
0. **Essayer le radar Auto** : Analyser sur une piste, regarder les 3 temps, comparer « visé » et « obtenu », Appliquer / Ignorer. Idées déjà notées : A/B à l'écoute (réglages d'origine contre proposition), vue spectre avant / après pour l'EQ, raisons au survol de chaque réglage.
1. **Écouter J4a** (déjà activé) : le Kick Out du kit 1 est la seule prise du corpus dont un réglage change (< 9 Hz) ; retour arrière possible via `doubleHitsByDefault`.
2. **Garde-fous Kit / Phase** : décider si on les active et les relier à l'interface (aujourd'hui seulement `KitPolicy` dans `KitSession`).

### Quand le nouveau corpus arrive
0. Déclarer les kits dans `corpus.json` (modèle : `docs/corpus.example.json`), lancer `scripts/RunCorpus.ps1`, lire `out/summary.md` ; terminer les étapes manquantes du doc 03 (compression équivalente 6.1, stéréo 7.4, `paires.csv`, agrégation).

### Sans écoute (sûr)
3. **Registre de rôles, étape 2** : migrer les réglages propres aux profils (`Profiles/*.h`), les bandes de mesure par rôle (`DrumAnalysis::attackBodyGapDb`), le détecteur Kick de `DrumReplay`, puis l'interface ; le filet de 270 propositions doit rester vrai.
4. **Seuil de 10 dB du compresseur du Kick** et autres constantes H : il faut des prises sparses (Kick joué par salves) ; aucun Kick du corpus n'est à moins de 7 dB du seuil.
5. **Kit / Phase** : valider `reconcileOverheads` sur d'autres kits (un seul est concerné, tolérance 1,5 ms = H) ; `--kit --detail` affiche les paires.

### Plus lourd (à planifier, pas à improviser)
6. **Constat 1** (biais du kick de +0,05 ms) : reprendre la corrélation du Kick (30-250 Hz).
7. **J4b** : détecteurs de frappes par rôle, analyse continue Overheads / Room avec entrée stéréo (débloque A12 et A13).
8. **Sous-bus SB1-SB7** (doc 08) : prérequis = étendre le registre Kit (publier des mesures, relayer des trims : les réponses aux questions de la section 12 du doc 08 sont **non**, **non**, **oui mais unique**) et ajouter les rôles Kick Sub / Hi-hat Bottom. Demande des décisions de conception (sélecteur « Piste / Sous-bus / Bus batterie », budget de traitement) et des essais dans Cubase.

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
