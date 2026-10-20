# Corpus : préparer, lancer, lire

> Archive au 20/10/2026, non maintenue. État et suivi courants : [PROJET.md](../PROJET.md).
> Le texte ci-dessous conserve ses dates, décisions et éventuelles contradictions historiques.

Le corpus reste **hors du dépôt** (aujourd'hui `C:\Users\robin\Desktop\Database_drum`). Un dossier de kits, un fichier `corpus.json` qui les décrit, un script qui lance toute la chaîne. Rien n'est joué, rien n'est écrit dans le dépôt.

## 1. Organisation conseillée

```
Database_drum\
  corpus.json            le manifeste (modèle : docs\corpus.example.json)
  raw1\ raw2\ ...        un dossier par kit, pistes brutes
  mix1\ mix2\ ...        les mêmes pistes mixées (stems) ou les bus d'instrument
  out\                   sortie du script (snapshots, rapports, bus, paires, summary.md)
```

Pour plusieurs mixes du même kit (ou plusieurs ingés), un dossier de mix par ingé et par style : `mix1_ingeA_rock\`, `mix1_ingeB_metal\`, qui partagent le même `raw1\`.

Un guide d'une page à donner à un ingé son qui prépare un kit : [`Guide_Ingenieur_Son.md`](../Guide_Ingenieur_Son.md).

### Ce qu'on attend d'un kit (liste de contrôle)

| À fournir | Pourquoi |
|---|---|
| Pistes brutes **sans aucun insert** (ni EQ, ni compression, ni gate, ni sample) | c'est la source que le plug-in verra |
| Les **mêmes pistes**, avec tous les inserts de piste, **avant** bus de batterie, master et envois d'effets | c'est le résultat de l'ingé, piste par piste |
| Même **nom de fichier** des deux côtés, même **fréquence d'échantillonnage**, même **début** d'export, pas de normalisation | l'appariement et l'alignement restent fiables |
| 20 s de jeu au minimum, avec des frappes variées (pas un seul coup répété) | les mesures (crest, bandes, décroissance) ont besoin de matière |
| Un fichier `INFO.txt` par mix : ingé, style (`natural`, `rock`, `metal`), notes (sample, compression parallèle, polarité inversée, décalage manuel), morceau | le **style** et l'**ingé** décident à quoi une paire sert ; sans eux, on ne regroupe pas |
| Priorité : Kick (In, Out), Snare (Top, Bottom), Overheads, puis Toms, Hi-hat, Room | les premiers rôles apportent le plus |
| Bonus : le bus de batterie en deux fichiers, `BUS_IN` (inserts du bus désactivés) et `BUS_OUT` (actifs) | paire propre qui isole le traitement du bus (rôle 9, `--pair`) ; sépare micro → piste, piste → bus, bus → sortie |
| Bonus : le **même brut mixé par deux ingés**, ou dans **deux styles** | on sépare ce qui est commun de ce qui est personnel |

Ce qui compte : **un stem par piste vaut mieux qu'un bus** (une paire exploitable ; un bus ne compte pas comme paire). Le style et l'ingé sont des étiquettes **par mix**, pas par kit : le manifeste actuel n'a qu'un style global, la prise en charge d'étiquettes par mix viendra avec les premiers dossiers réels.

Règles pour que les mesures soient comparables :

- **Même fréquence d'échantillonnage** des deux côtés d'une paire (sinon l'outil refuse : convertir d'abord).
- **Un stem par piste** *après les inserts, avant le bus de batterie et le master* est idéal (paire). Un **bus d'instrument** est accepté si on fournit **tous ses micros** (et le sample, s'il y en a un) : l'outil `--bus` le traite par régression multi-entrées (doc 03, 3.1 bis).
- Les fichiers mixés peuvent démarrer à un autre instant que les bruts (l'alignement cherche sur tout le fichier). Au moins **20 s de jeu**.
- **Au moins 20 s de jeu réel** par piste : une prise de 43 s avec quelques frappes de snare ou de toms (12 et 10 s de jeu au Track 1 et 2) est refusée. Préférer 1 à 2 minutes de batterie qui joue partout.
- Manifeste : par kit, `style`, `mixer`, `genre`, `samples` et `notes` facultatifs (écrits dans `summary.md`) ; par paire, `role` facultatif (déclenche `--vs-plugin`).
- Noter dans le manifeste le **rôle** (0 Kick, 1 Snare, 2-4 Toms, 5 Tom basse, 6 Hi-hat, 7 Overheads, 8 Room, 9 Bus) et la **position** du micro (`kitMic` : 0 principal, 1 secondaire Out / Bottom, 2 gauche, 3 droite).
- Ne pas mettre dans le manifeste ce qui n'a pas de rôle (Kick Sub, Ride, Cymbals) ni les pistes de sample : elles peuvent servir d'entrées d'un bus, pas de snapshot.

## 2. Lancer

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\RunCorpus.ps1 -Corpus C:\Users\robin\Desktop\Database_drum
# options : -Out <dossier>, -SkipSnapshots, -SkipBuses, -SkipPairs, -Exe <ZeroEffortDrumTests.exe>
```

Le script utilise le build Release de production (`build\ZeroEffortDrumTests_artefacts\Release\`). Il écrit `out\summary.md` : une ligne par piste, bus ou paire, avec le verdict.

## 3. Lire les résultats

| Sortie | Ce que c'est |
|---|---|
| `out\snapshots\<kit>\*.json` | une analyse par piste (mesures + proposition), rejouable |
| `out\reports\replay.md` | rejeu de tous les snapshots : refus, réglages, prises à écouter |
| `out\buses\*.md / .json` | bus : décalage de chaque micro, gain par micro et par bande, **cohérence multiple** (≥ 0,8 : le bus est expliqué par ses micros) |
| `out\pairs\*.md / .json` | paire brut / stem : décalage, polarité, courbe d'égalisation équivalente (forme + gain moyen), cohérence, couleur non linéaire, deltas de crest et de plancher |

| `out\versus\*.md / .json` | **plug-in contre ingénieur** : la même piste brute passée dans la proposition Auto du plug-in, comparée à la courbe de l'ingénieur (écart, corrélation, accord de sens, dynamique). Demande un `"role"` sur la paire dans le manifeste ; le style et l'intensité sont ceux du kit et du manifeste |

Lecture en tableaux : `python scripts\CorpusTables.py pairs|versus|shapes|buses <dossier out>`. Premier passage commenté : [`ANALYSE_CORPUS_19OCT.md`](ANALYSE_CORPUS_19OCT.md).

Sous-bus : `ZeroEffortDrumTests.exe --sub-sum <bus.wav> <préfixe> nom=micro.wav ...` écrit la somme alignée des micros et le bus correspondant (`_sum.wav`, `_mix.wav`), à passer à `--vs-plugin` avec le rôle 10 (Kick Bus), 11 (Snare Bus) ou 12 (Toms Bus). <brut> <rôle> <style> <intensité> <mixé> [préfixe] [--reference] [--off id,...] [--set id=valeur,...]`, `--pair <brut> <mixé> [préfixe]`, `--bus <mixé> <préfixe> nom=fichier ...`, `--export-wav`, `--replay`, `--compare`, `--kit` (voir `docs\REPRISE_PROCHAINE_SESSION.md`).

## 4. Ce qui n'est pas encore fait

Étiquettes par mix (style, ingé, notes) lues par `RunCorpus.ps1` et regroupement des résultats par rôle × style × ingé (à faire avec les premiers dossiers réels), estimation de la compression équivalente (doc 03, 6.1), stéréo des overheads et de la room (7.4), `paires.csv` et agrégation par rôle × style (8 et 9). Les cibles des presets ne se calculent qu'à partir de **8 paires et 3 mixeurs** (doc 03, 9) : un bus ne compte pas pour une paire de stem sans l'étiquette de son mixeur.
