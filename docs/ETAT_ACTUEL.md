# ZeroEffort Drum : état actuel (17 octobre 2026)

Document de situation : **où on en est, ce qui est fait, ce qui manque**. Il résume sans remplacer le journal détaillé ([SUIVI_IMPLEMENTATION.md](SUIVI_IMPLEMENTATION.md)), la passation ([REPRISE_PROCHAINE_SESSION.md](REPRISE_PROCHAINE_SESSION.md)), le plan ([PLAN_IMPLEMENTATION.md](PLAN_IMPLEMENTATION.md)) et les spécifications (`CodeUpdate/01` à `08`). En cas de doute sur un chiffre, le journal fait foi.

## 1. En une page

| | |
|---|---|
| Version du plug-in | **0.11.0** (VST3 + ARA, Windows x64, JUCE 8.0.6) |
| Versions d'analyse | `analysisVersion` **6**, `profileVersion` **2** |
| Branche de travail | `main` directement, un commit par étape (depuis le 17/10) |
| Tests | 3 suites CTest, **toutes vertes** : `RegionCacheTests`, `ZeroEffortDrumTests` (≈ 80-120 s), `ARADiscoveryTests` ; build VS 2019 sans avertissement |
| Corpus (hors dépôt) | `Database_drum` : 4 kits bruts ; mixés : kit 1 (bus Kick, Snare et batterie) et kit 2 (bus de batterie seul), manifeste `corpus.json` |
| Vérifié dans Cubase | l'**export JSON** d'une analyse Auto (Kick, région ARA). **Rien d'autre n'a été écouté** (voir 5) |
| Dernier chantier | interface passée en **anglais** (voir 6) ; avant : radar Auto |

## 2. Ce qui est fait

### Analyse et propositions
| Élément | État |
|---|---|
| Snapshots JSON d'une analyse (J1), rejeu (`--replay`), comparaison (`--compare`), export WAV (`--export-wav`) | ✅ |
| Mesures additives (J2) : niveau actif, plancher, frappes/s, décroissance, tiers d'octave, bandes nommées, écart attaque/corps | ✅ |
| Tests de propriétés et de mesures (A01-A07, A10, A11, E01-E10), cas connus (A08, A09) | ✅ |
| Calibration de la fenêtre active (01-Q1) : seuil relatif 40 → **55 dB** ; décalage de plancher 12 dB étudié et **conservé** | ✅ |
| Registre de rôles : filet de 270 propositions gelées + table `RoleDescriptor` (`DrumRole.h`), `DrumAnalyser` sans branchement sur le rôle | ✅ étape 1 sur 2 |
| Export enrichi : `meta.position`, `meta.channels`, `--position` | ✅ |

### Correctifs actifs par défaut (validés par l'utilisateur, sans écoute dédiée)
| Correctif | Effet |
|---|---|
| J3 problème 1 : trames d'attaque écartées du bleed | `excludeLeadFramesByDefault = true` |
| J3 problème 2 : crest **actif** pour le compresseur du Kick | `compressorUsesActiveCrest = true` |
| J4a : détection des doubles frappes (60 ms au lieu de 120 ms si l'attaque est 4 × plus forte) | `doubleHitsByDefault = true` ; un seul réglage change sur le corpus (Kick Out du kit 1, < 9 Hz) |
| Kit / Phase : saut de cycle du Kick sur les overheads (kit 4 : cohérence 59,5 → 96,6) | `KitPolicy::reconcileOverheads = true` |

### Kit / Phase
Mesure **K04** (diaphonie) ; garde-fous « hors fenêtre » et « diaphonie » écrits mais **désactivés** (et non reliés à l'interface) ; commande `--kit` (`--detail`, `--guards`, `--no-reconcile`).

### Brut / mixé (document 03)
`--pair` (paire de stems : alignement, courbe d'égalisation équivalente, cohérence, couleur non linéaire, deltas) et `--bus` (bus = somme filtrée de ses micros, régression multi-entrées) ; `scripts/RunCorpus.ps1` lance toute la chaîne depuis un manifeste ; spécification 03 corrigée (bus d'instrument, recherche du décalage sur tout le fichier, etc.). Réel : Kick Bus expliqué à 0,90, Snare Bus à 0,82.

### Interface
- **Radar Auto** : panneau polaire à 12 axes, en 3 temps (source mesurée en direct, « visé » calculé, « obtenu » mesuré sur le son traité), liste des modules et réglages changés, **Appliquer / Ignorer** : la proposition **attend votre clic** dans l'éditeur (changement de comportement voulu).
- **Tout l'interface en anglais** (voir 6).

### Outillage et documentation
Build VS 2019 / 2022 (`scripts/Build.ps1`), build d'export pour Cubase (`build-export/`), 5 WAV retirés de git (`ZED_WAV_DIRECTORY`), `docs/Corpus.md`, `scripts/CalibrateActiveWindow.py`.

## 3. Ce qui est désactivé ou non relié (volontairement)

| Élément | Où | Pour l'activer |
|---|---|---|
| Garde-fou Kit « hors fenêtre de recherche » | `KitPolicy::refuseOutsideWindow` | décision + écoute ; puis relier à l'interface |
| Garde-fou Kit « diaphonie » (seuil −15 dB, fragile : le Kick In du kit 1 est à −16 dB) | `KitPolicy::refuseCrosstalk` | idem ; seuil à recalibrer sur plus de kits |
| Export d'analyse | option CMake `-DZED_ANALYSIS_EXPORT=ON` **et** variable `ZED_ANALYSIS_EXPORT_DIR` | jamais en production |
| Anciens comportements | `--no-exclude-lead-frames`, `--no-active-crest`, `--no-double-hits`, `--no-reconcile` | outils de rejeu seulement |

## 4. Ce qui manque

### À faire sans corpus ni écoute
| Chantier | Détail | Risque |
|---|---|---|
| Registre de rôles, **étape 2** | migrer les réglages propres aux profils (`Profiles/*.h`), les bandes de mesure par rôle (`attackBodyGapDb`), le détecteur Kick de `DrumReplay`, puis l'interface ; le filet de 270 propositions doit rester vrai | faible |
| Seuil de 10 dB du compresseur du Kick | il faut des prises éparses (Kick par salves) ; aucun Kick du corpus n'est à moins de 7 dB du seuil | faible |
| Radar Auto : suites | A/B à l'écoute, spectre avant / après pour l'EQ, raisons au survol des réglages | faible |

### Demande des décisions ou des essais dans Cubase
| Chantier | Détail |
|---|---|
| **Biais du kick** (+0,05 ms, constat 1 de T3) | reprendre la corrélation du Kick (30-250 Hz) ; derrière une option désactivée |
| **Sous-bus SB1-SB7** (doc 08) | registre Kit à étendre (publier des mesures, relayer des trims), rôles **Kick Sub** et **Hi-hat Bottom** à ajouter, sélecteur « Piste / Sous-bus / Bus batterie », budget de traitement |
| **J4b** | détecteurs de frappes par rôle, analyse continue Overheads / Room avec entrée stéréo (débloque A12 et A13) |
| Activer / relier les garde-fous Kit / Phase | voir 3 |

### Dépend du nouveau corpus (raw vs mix, plusieurs kits)
| Chantier | Détail |
|---|---|
| Document 03, suite | estimation de la compression équivalente (6.1), stéréo des overheads et de la room (7.4), `paires.csv` (8), **agrégation** par rôle × style (9) |
| Cibles des presets | exigent **8 paires et 3 mixeurs** (doc 03, 9) ; un bus sans étiquette de mixeur ne compte pas |
| Constantes H à calibrer | 400 ms de trame longue, cohérence 0,8 (premiers chiffres réels : 0,82 à 0,90 pour les bus), `k = 0,5`, zone morte 1 dB, seuils K04 et hors fenêtre, constantes de J4a (60 ms, facteur 4), tolérance de 1,5 ms de `reconcileOverheads`, plages des axes du radar |
| Tests M02, M03, M06, M07 | dynamique équivalente, gate, réverbe, compression parallèle |

## 5. Ce qui n'a pas été vérifié

- **Aucune écoute dans Cubase** des correctifs activés (J3, J4a, seuil à 55 dB, saut de cycle) : ils sont validés par les tests et sur le corpus, pas à l'oreille. Sur les 38 prises du corpus, seul un réglage change (Kick Out du kit 1, J4a).
- **Le radar Auto** n'a été jugé que sur des captures d'écran produites par les tests, pas dans l'hôte (rythme de l'animation, lisibilité, comportement avec ARA réel). En insert, la source se dessine maintenant pendant la capture de 8 s (lecture nécessaire) ; **validé dans Cubase par l'utilisateur le 17/10/2026**.
- **L'interface en anglais** : vérifiée par les tests et des captures ; les textes longs peuvent déborder dans l'hôte.
- Le build installé dans Cubase est peut-être encore le **build d'export** : recopier le build Release de production (`build\ZeroEffortDrum_artefacts\Release\VST3\`).

## 6. Interface en anglais (17/10/2026)

Tout ce que le plug-in affiche est en anglais : boutons, infobulles, titres de panneaux, noms de réglages et de bandes d'EQ, noms de pages, textes du kit, du radar, du bilan Auto. Les identifiants ne changent pas (paramètres, composants, clés JSON, codes de plug-in) : les projets existants se rechargent comme avant. Les **rapports d'outils développeur** (`--replay`, `--compare`, `--pair`, `--bus`, `--kit`) et les **documents** restent en français.

## 7. Comment travailler

```powershell
# Build + les 3 suites (la tâche VS Code « Build and Test Release » fait la même chose)
$env:ZED_WAV_DIRECTORY = 'C:\Users\robin\Desktop\Database_drum\raw3'   # tests de kit réel (sinon ignorés)
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Build.ps1 -Action Verify

# Tout le corpus (snapshots, bus, paires, rejeu)
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\RunCorpus.ps1 -Corpus C:\Users\robin\Desktop\Database_drum
```

Règles du chantier : aucun changement audible sans le dire ; un commit par étape ; ne jamais valider un cas doré à la place de l'utilisateur ; ne jamais changer un identifiant de paramètre ni un code de plug-in ; ne pas lancer d'audio sans confirmation ; ne pas copier de bundle dans un dossier système.

## 8. Où sont les choses

| Sujet | Fichier |
|---|---|
| Mesures, analyse | `Source/Drum/DrumAnalysis.h` |
| Propositions par rôle | `Source/Drum/Profiles/*.h`, `DrumAutoPreset.h`, `DrumRole.h` |
| Radar Auto | `Source/Drum/DrumAutoRadar.h`, `Source/UI/AutoRadarPanel.h` |
| Kit / Phase | `Source/Kit/*` |
| Snapshots, rejeu, paires et bus | `DrumAnalysisSnapshot.h`, `DrumReplay.h`, `DrumPairAnalysis.h` |
| Tests | `tests/*.cpp` (appelés depuis `ZeroEffortDrumProcessorTests.cpp`) |
| Corpus | `docs/Corpus.md`, `scripts/RunCorpus.ps1`, `docs/corpus.example.json` |
| Spécifications | `CodeUpdate/01` à `08` (en conflit : 05 > 08 > 01-04 > 06) |
