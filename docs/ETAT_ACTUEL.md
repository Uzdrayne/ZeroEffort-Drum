# ZeroEffort Drum : état actuel (17 octobre 2026)

Document de situation : **où on en est, ce qui est fait, ce qui manque**. Il résume sans remplacer le journal détaillé ([SUIVI_IMPLEMENTATION.md](SUIVI_IMPLEMENTATION.md)), la passation ([REPRISE_PROCHAINE_SESSION.md](REPRISE_PROCHAINE_SESSION.md)), le plan ([PLAN_IMPLEMENTATION.md](PLAN_IMPLEMENTATION.md)) et les spécifications (`CodeUpdate/01` à `08`). En cas de doute sur un chiffre, le journal fait foi.

## 1. En une page

| | |
|---|---|
| Version du plug-in | **0.11.5** (VST3 + ARA, Windows x64, JUCE 8.0.6) |
| Versions d'analyse | `analysisVersion` **6**, `profileVersion` **4** |
| Branche de travail | `main` directement, un commit par étape (depuis le 17/10) |
| Tests | 3 suites CTest, **toutes vertes** : `RegionCacheTests`, `ZeroEffortDrumTests` (≈ 80-120 s), `ARADiscoveryTests` ; build VS 2019 sans avertissement |
| Corpus (hors dépôt) | `Database_drum` : 4 tracks (`Track_1`, `Track_2`, `track_3`, `track_4`) ; Track 2 = 8 paires brut / mixé acceptées (ingé A.H., post punk) ; Track 1 inexploitable en paires (samples) ; Tracks 3 et 4 = bus ; manifeste `corpus.json` (ancien dans `archive_v6`). **`raw2`, `raw3`, `raw4` ont disparu** (voir 5) |
| Vérifié dans Cubase | l'**export JSON** d'une analyse Auto (Kick, région ARA). **Rien d'autre n'a été écouté** (voir 5) |
| Dernier chantier | **sous-bus SB1, SB2 et SB3** (rapport de groupe en lecture seule, profil de groupe) (profil de groupe Kick Bus / Snare Bus, activé) (Kick Bus, Snare Bus, Toms Bus dans le menu d'instrument) ; avant : presets utilisateur (Presets : enregistrer / charger par instrument, voir 2) ; avant : interface en anglais (6), radar Auto |

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

### Presets utilisateur (0.11.1, 18/10/2026)
Bouton **Presets** en bas de l'éditeur (à droite de Report) : *Save current settings as…* (petite fenêtre de nom, le bouton devient « Overwrite » si le nom existe), liste des presets de l'instrument en cours (un clic charge), *Delete* (vers la corbeille), *Open the presets folder*. Un Kick ne voit que les presets Kick ; les **4 toms partagent** les leurs. Fichiers JSON `.zedpreset` dans `%APPDATA%\ZeroEffort Drum\Presets\<famille>\`, réglages enregistrés par identifiant de paramètre. Un preset contient le **son** (tous les réglages de traitement) ; il ne contient pas le rôle, le bypass, le Kit / Phase (retard, polarité), les options Auto (style, intensité) ni le Mono. Le style et l'intensité Auto choisis à l'enregistrement sont gardés **à titre indicatif** et affichés dans la liste (« Crack   (Rock 75 %) ») ; charger ne les réapplique pas. **Charger est annulable** avec Revert. Logique dans `Source/Drum/DrumPresetLibrary.h`, tests dans `tests/PresetTests.cpp`. Non essayé dans Cubase.

### Correctifs actifs par défaut (validés par l'utilisateur, sans écoute dédiée)
| Correctif | Effet |
|---|---|
| J3 problème 1 : trames d'attaque écartées du bleed | `excludeLeadFramesByDefault = true` |
| J3 problème 2 : crest **actif** pour le compresseur du Kick | `compressorUsesActiveCrest = true` |
| J4a : détection des doubles frappes (60 ms au lieu de 120 ms si l'attaque est 4 × plus forte) | `doubleHitsByDefault = true` ; un seul réglage change sur le corpus (Kick Out du kit 1, < 9 Hz) |
| Kit / Phase : saut de cycle du Kick sur les overheads (kit 4 : cohérence 59,5 → 96,6) | `KitPolicy::reconcileOverheads = true` |
| Style et intensité Auto d'une nouvelle instance : **Rock, 75 %** (avant : Natural, 100 %), demandé par l'utilisateur le 18/10/2026 | `controlSpecs` (`autoStyleOption` 1, `autoIntensityOption` 75) et valeurs initiales de `AutoSession` ; les projets déjà enregistrés gardent leur valeur ; les 270 propositions du filet de rôles sont identiques |

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

### Corpus du 19/10/2026 : analysé, rien appliqué
`--vs-plugin` compare ce que fait la proposition Auto à ce qu'a fait l'ingénieur sur la même piste. Bon accord sur le **Kick In** (même sens sur 16 bandes sur 17, confirmé par une 2e source), le plug-in est trop doux sur le **Hi-hat**, neutre sur la **Snare**, presque plat sur les **Overheads** (l'ingénieur coupe les graves, monte les aigus), et **pire que ne rien faire sur la Room**. L'ingénieur gate beaucoup plus fort. **Un seul ingénieur, un seul morceau** : pistes de changement et leur force de preuve dans [ANALYSE_CORPUS_19OCT.md](ANALYSE_CORPUS_19OCT.md). **Une seule est implémentée et ACTIVÉE (19/10, à ta demande, pour essai)** : `ProposalDefaults::cymbalReference` (EQ de référence Overheads et Hi-hat en Rock et Metal ; Natural inchangé) : accord 6 → 14 bandes sur 17 aux Overheads, 10 → 11 sur 11 au Hi-hat, `profileVersion` 3, plug-in **0.11.2**. Retour arrière : la passer à `false`. La Room vient du module Crush (choix de style, non touché). Double pédale (Track 1) : J4a compte +47 % de frappes sans changer aucun réglage.

### Dépend du nouveau corpus (raw vs mix, plusieurs kits)
| Chantier | Détail |
|---|---|
| Document 03, suite | estimation de la compression équivalente (6.1), stéréo des overheads et de la room (7.4), `paires.csv` (8), **agrégation** par rôle × style (9) |
| Cibles des presets | exigent **8 paires et 3 mixeurs** (doc 03, 9) ; un bus sans étiquette de mixeur ne compte pas |
| Constantes H à calibrer | 400 ms de trame longue, cohérence 0,8 (premiers chiffres réels : 0,82 à 0,90 pour les bus), `k = 0,5`, zone morte 1 dB, seuils K04 et hors fenêtre, constantes de J4a (60 ms, facteur 4), tolérance de 1,5 ms de `reconcileOverheads`, plages des axes du radar |
| Tests M02, M03, M06, M07 | dynamique équivalente, gate, réverbe, compression parallèle |
| **Axe Attack du radar et attaque du Kick** (constat du 18/10/2026, en attente du nouveau corpus) | voir ci-dessous |

**Constat Attack (18/10/2026, rien modifié).** Kick Rock 80 %, deux vrais Kicks (`raw1\01_KickIn`, `raw3\Kick_12`) : source 9,8 / 11,1 dB, cible 13,4 / 14,7, résultat mesuré 4,8 / 2,6. (1) La cible ajoute le réglage nominal de l'Impact (+3,6 dB) alors que son effet réel sur cet axe est d'environ +0,3 dB (le détecteur ne voit la transitoire que quelques ms par frappe). (2) La cible ignore la compression : le compresseur multibande retire 2,5 à 6 dB, le Crush 1 à 2,5 dB. L'axe mesure un rapport d'énergie (0-70 ms contre 70-230 ms), pas le pic : le clic peut passer (attaque de 8 ms) alors que le corps des 70 premières ms est écrasé. À décider avec le nouveau corpus et à l'écoute : (a) marquer la cible Attack « non modélisée » quand compression ou Crush sont actifs (sans effet sur le son) ; (b) revoir le compresseur ou le Crush du Kick (change le son, désactivé par défaut tant que non écouté).

## 5. Ce qui n'a pas été vérifié

- **Aucune écoute dans Cubase** des correctifs activés (J3, J4a, seuil à 55 dB, saut de cycle) : ils sont validés par les tests et sur le corpus, pas à l'oreille. Sur les 38 prises du corpus, seul un réglage change (Kick Out du kit 1, J4a).
- **Tests de kit réel ignorés** : `raw3` (`Kick_12`, `Snare_12`, `OvH L_12`, `OvH R_12`) n'existe plus ; D01-D04 sur le kit réel et le test des garde-fous affichent « SKIP ». Les 3 suites passent quand même, mais ces deux contrôles ne tournent plus. À retrouver ou à remplacer.
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
