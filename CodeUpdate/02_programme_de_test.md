# Programme de test

**Document 2 sur 4** | Version du document : 0.1 | Dépend de : `01_cahier_des_charges_mesures.md` (identifiants L01, D02, O03…)

Documents liés : 03 comparaison brut / mixé (à écrire), 04 calcul des presets (à écrire).

---

## 1. Objet

Pouvoir répondre à trois questions **sans réécouter chaque prise** :

1. **Les mesures sont-elles justes ?** (une sinusoïde à −6 dBFS donne bien −6 dBFS)
2. **Un changement de code ou de seuil améliore-t-il les propositions, ou les dégrade-t-il ?**
3. **Les propositions respectent-elles les garde-fous ?** (jamais de réduction au-delà du plafond, intensité 0 % = tout éteint, etc.)

L'oreille reste le juge final, mais elle n'intervient que sur les cas que ce programme signale (section 9).

## 2. Principes

1. **La vérité vient d'ailleurs que du code testé.** Signaux synthétiques à valeur connue, défauts artificiels avec réponse connue, cas validés à l'oreille une fois pour toutes.
2. **Rejeu sans DAW ni audio.** Un snapshot (`DrumAnalysisSnapshot.h`) suffit pour rejouer `propose()`. Les WAV ne servent que quand l'*analyse* change.
3. **Tolérances, jamais d'égalité exacte.** Les FFT en virgule flottante varient d'un compilateur à l'autre. Les comparaisons utilisent une tolérance relative (voir 7.2).
4. **Séparer réglage et validation.** Les seuils se règlent sur un jeu A. Un jeu B, jamais utilisé pour régler, mesure la généralisation. Sinon on optimise sur les mêmes fichiers et on se croit bon.
5. **Un test qui échoue dit pourquoi.** Chaque échec affiche la mesure, la valeur attendue, la valeur obtenue et le fichier concerné.
6. **Les tests tournent en une commande** et sortent un rapport lisible (section 8).

## 3. Architecture

Une application console JUCE (`drumtest`) qui compile les **mêmes sources** que le plug-in (`DrumAnalysis`, profils, module Kit). Elle s'ajoute aux tests déjà présents (par exemple `tests/KitTests.cpp`).

```
drumtest unit                      tests unitaires + signaux synthétiques
drumtest replay  <dossier>         rejoue des snapshots JSON dans les profils
drumtest analyse <dossier wav>     lance l'analyse sur des WAV, écrit les snapshots
drumtest faults  <dossier kit>     fabrique les défauts artificiels et vérifie les réponses
drumtest golden                    cas validés à l'oreille
drumtest compare <A> <B>           rapport de différences entre deux versions
drumtest perf                      coût CPU et mémoire
```

Les noms de commandes sont une proposition.

**État (T1 + T2, 16/10/2026)** : pas de cible `drumtest` séparée (décision du plan) ; les commandes sont des options de `ZeroEffortDrumTests.exe` (dans `build*/ZeroEffortDrumTests_artefacts/<Config>/`) :

```
ZeroEffortDrumTests.exe --export-wav <wav> <dossier snapshots> <role 0-9> [natural|rock|metal] [intensite 0-1]
ZeroEffortDrumTests.exe --replay  <dossier snapshots> [prefixe de sortie]   # rejoue, compare a l'enregistre
ZeroEffortDrumTests.exe --compare <rapport A.json> <rapport B.json> [prefixe de sortie]
```

`--replay` rejoue tous les snapshots (récursivement) dans les profils actuels, compare avec la proposition enregistrée dans le snapshot, imprime le rapport Markdown et, avec un préfixe, écrit `<préfixe>.json` (rapport de la version courante) et `<préfixe>.md`. Workflow entre deux versions du code : `--replay` avec la version A, `--replay` avec la version B, puis `--compare A.json B.json`. Les rôles sont numérotés comme la liste `DrumInstrument` (0 Kick, 1 Snare, 2-4 Toms, 5 Tom basse, 6 Hi-hat, 7 Overheads, 8 Room, 9 Bus). Sans option, l'exécutable lance la suite de tests complète. `faults`, `golden` et `perf` restent à faire.

## 4. Corpus de test

```
corpus/
  jeu_reglage/            # A : sert à régler les seuils
  jeu_validation/         # B : jamais utilisé pour régler
  synthetique/            # signaux générés par le programme
  golden/                 # cas validés à l'oreille
  <kit_id>/
    meta.json             # style, micros, source, licence
    Kick_In.wav  Snare_Top.wav  OH_L.wav  OH_R.wav  Room_L.wav  Room_R.wav …
    snapshots/            # JSON produits par drumtest analyse
```

Règles : un kit entier va dans un seul jeu (jamais un micro dans A et un autre dans B). La licence figure dans `meta.json`. Premier objectif : 5 kits, puis 15 (voir document 01, section 10).

## 5. Familles de tests

### 5.1 Mesures sur signaux synthétiques (`T-A`)

Le programme génère des signaux dont les mesures sont connues par calcul.

| Test | Signal | Attendu | Mesure |
|---|---|---|---|
| A01 | Sinusoïde 1 kHz, amplitude 0,5 | pic −6,02 dBFS, RMS −9,03 dBFS, crest 3,01 dB | L01, L02, D01 |
| A02 | Silence | refus « signal trop faible » | L01, H01 |
| A03 | Sinusoïde à pleine échelle écrêtée | `clipped_ratio` > 1 %, refus « signal saturé » | L06 |
| A04 | Échantillon NaN inséré | `invalid`, refus | Q01 |
| A05 | Bruit rose, 10 s | pente spectrale proche de −3 dB/octave (tolérance à fixer) | S04 |
| A06 | Bruit blanc filtré en bandes (octaves) | énergie dans la bonne bande, écart < 1 dB | S01, S02 |
| A07 | Sinus amorti 60 Hz (kick synthétique), 24 frappes | `hits` = 24, corps = 60 Hz ± 6 Hz (résolution de bin) | H01, T01 |
| A08 | Train de doubles pédales à 180 bpm (83 ms) | `hits` = nombre réel (**échec attendu aujourd'hui**, problème 3 du document 01) | H01 |
| A09 | Kick synthétique entouré de silence long, puis le même avec peu de silence | `crest_actif` identique à ±1 dB ; `crest_global` différent (problème 2) | D01, D02 |
| A10 | Même signal envoyé par blocs de 64, 512, 4096 échantillons | mesures identiques à la tolérance près | toutes |
| A11 | Même signal à 44,1, 48 et 96 kHz | mesures cohérentes (écart < tolérance) | toutes |
| A12 | Sinus 3 kHz avec ajout de 6 dB en shelf aigu | `eclat_aigu` augmente de 6 dB ± 1,5 dB | O04 |
| A13 | Queue exponentielle de bruit avec T60 connu (0,4 s, 0,8 s, 1,5 s) | `decay_t60` à ± 15 % | R01 |

A10 est essentiel : une analyse qui dépend de la taille de bloc de l'hôte donnera des résultats différents d'un DAW à l'autre.

**État (T1, 16/10/2026)** : A01, A02, A03, A04 et A10 sont implémentés dans `tests/PropertyTests.cpp` (exécutés par `ZeroEffortDrumTests`). A10 compare le bloc `analysis` du snapshot à l'identique (blocs de 64, 257, 512 et 4096, à 44,1, 48 et 96 kHz) : aucune dépendance à la taille de bloc. A09 est implémenté en J2 (`tests/MeasureTests.cpp`, avec une tolérance de 1,5 dB au lieu de 1 dB : voir doc 01, 5.1) et A08 en J3 (`tests/KnownProblemTests.cpp`) comme **échec attendu** : il passe tant que le détecteur sous-compte les doubles pédales et échoue dès qu'il sera corrigé. A05-A07 et A11-A13 restent à faire.

### 5.2 Rejeu de snapshots (`T-B`)

Entrée : un dossier de JSON. Sortie : pour chaque snapshot, la proposition de la version courante.

- **Déterminisme** : deux exécutions donnent le même résultat.
- **Diff entre versions** : pour chaque réglage, valeur ancienne, valeur nouvelle, écart (section 8).
- **Taux de refus** : part des analyses refusées (« analyse insuffisante », « signal saturé »), par rôle et par style.
- **Sensibilité aux seuils** : pour chaque seuil du code (`crest < 6`, `bass/total < 0,15`, `loud − quiet >= 12`, `crest > 10`…), nombre d'analyses situées à ± 5 % de ce seuil. Un seuil avec beaucoup d'analyses proches est fragile.

### 5.3 Rejeu depuis les WAV (`T-C`)

Quand l'analyse change (détecteur, FFT, règle de fenêtre active), on relance `drumtest analyse` sur le corpus pour régénérer les snapshots, puis `replay`. Les nouveaux snapshots portent un nouvel `analysisVersion` ; les anciens ne sont plus comparables.

### 5.4 Défauts artificiels (`T-D`)

On prend une bonne prise, on l'abîme d'une façon connue, et on vérifie que l'outil trouve la bonne réponse. Chaque cas est une paire (entrée, réponse attendue).

| Test | Défaut appliqué | Réponse attendue |
|---|---|---|
| D01 | Retarder le kick de 3,0 ms par rapport aux overheads | retard proposé = 3,0 ms ± 1 échantillon (signal pur) ; ± 0,2 ms sur un kit réel |
| D02 | Idem avec la caisse claire, valeurs 1 / 2,5 / 6 / 10 ms | retard retrouvé ; au-delà de 10 ms, refus clair plutôt qu'une mauvaise valeur |
| D03 | Inverser la polarité du kick | polarité inversée proposée |
| D04 | Retard + inversion simultanés | les deux retrouvés (cas du kick à +7 ms inversé, document KitPhase10) |
| D05 | Ajouter de la diaphonie (caisse claire à −12 puis −6 dB dans le micro du kick) | retard du kick toujours correct, `diaphonie_db` en hausse |
| D06 | Ajouter du bruit de fond (−50, −40, −30 dBFS) | l'analyse refuse ou garde le bon retard, jamais un mauvais retard silencieux |
| D07 | Overheads décalés de 0,4 ms entre G et D | écart G/D retrouvé, score stéréo en baisse |
| D08 | Baisser le niveau d'une prise de 20 dB | mesures de forme (spectre) inchangées, mesures de niveau décalées de 20 dB |
| D09 | Seulement 3 pistes (kick, caisse claire, un overhead) | résultat correct, pas de plantage (public amateur) |
| D10 | Kick Out seul, sans Kick In | mesuré directement contre les overheads (comportement documenté) |

### 5.5 Overheads et room (`T-J`)

| Test | Défaut appliqué | Réponse attendue |
|---|---|---|
| J01 | Shelf aigu +6 dB sur un overhead | `cymbales_vs_corps` et `eclat_aigu` augmentent d'environ 6 dB (± 1,5) |
| J02 | Bosse de +6 dB à 3,5 kHz | `durete_db` augmente ; la proposition crée un creux de présence |
| J03 | Ajouter du grave (fuite de kick) sous 120 Hz | `fuite_grave_db` augmente ; le high-pass proposé monte |
| J04 | Overhead avec un crash isolé fort | médianes de bandes quasi inchangées (preuve que médiane et P90 résistent) |
| J05 | Room avec T60 connu (voir A13) | décroissance estimée dans la tolérance |
| J06 | Room inversée en polarité par rapport aux overheads | alerte de polarité (R04) |
| J07 | Overheads stéréo mis en mono | `correlation_lr` = 1, `largeur_ms` au minimum |

### 5.6 Propriétés des propositions (`T-E`)

Vérifications qui doivent être vraies pour **toute** analyse valide, quel que soit le fichier. On les lance sur tout le corpus et sur des milliers d'entrées aléatoires.

| Propriété | Énoncé |
|---|---|
| E01 | Aucune valeur NaN ou infinie dans les réglages |
| E02 | Chaque réglage reste dans les bornes de son paramètre |
| E03 | `intensité = 0` : tous les traitements ajoutés sont éteints (comportement documenté) |
| E04 | Monotonie : quand l'intensité monte, le gain absolu de chaque bande d'EQ ne baisse jamais |
| E05 | Ordre des styles : réduction visée Naturel ≤ Rock ≤ Metal |
| E06 | Réduction visée ≤ plafond de réduction |
| E07 | `output` ≤ −3 dB (valeur actuelle de `KickProfile`) et `bypass` conservé |
| E08 | Déterminisme : mêmes entrées, mêmes réglages |
| E09 | Refus explicite : une analyse refusée renvoie `valid = false` et un message, jamais des réglages partiels |
| E10 | Une analyse insuffisante ne modifie pas les réglages d'origine |

**État (T1, 16/10/2026)** : E01, E02, E06, E07, E08, E09 et E10 sont implémentés (`tests/PropertyTests.cpp`) sur 7 200 propositions issues de 48 signaux générés (3 fréquences d'échantillonnage, 10 rôles, 3 styles, 5 intensités, réglages d'origine aléatoires). E03, E04 et E05 restent à faire.

### 5.7 Cas dorés (`T-F`)

Un cas doré est une analyse dont la proposition a été **validée à l'oreille**. On enregistre le snapshot et la proposition acceptée. Le test échoue si une version ultérieure s'en écarte au-delà d'une tolérance par réglage (par exemple ± 1 dB sur un gain, ± 5 % sur une fréquence). Un échec n'est pas forcément une erreur : il oblige à écouter et à dire « on accepte le nouveau résultat » (on met à jour le cas) ou « on refuse » (on corrige le code).

Cible : 3 cas par rôle et par style une fois la base remplie.

### 5.8 Régression de la base (`T-G`)

Après chaque changement de l'analyse, comparer la distribution de chaque mesure par rôle et par style (médiane, P10, P90) avec la version précédente. Alerte si la médiane d'une mesure bouge de plus de la tolérance prévue (voir 7.2) sans que le changement ait été annoncé.

### 5.9 Performance et temps réel (`T-H`)

| Test | Énoncé |
|---|---|
| H01 | Temps de `push()` par bloc de 10 ms, médiane et maximum, pour 1 puis 8 instances |
| H02 | Aucune allocation dans le chemin audio (vérification par un allocateur surveillé en mode test) |
| H03 | Mémoire par instance (les tableaux de `DrumAnalysis` pèsent plusieurs centaines de Ko) |
| H04 | Aucune pointe CPU lors de la FFT (pas de dépassement du budget de bloc à 64 échantillons) |

Seuil à fixer après la première mesure (voir question ouverte Q3).

## 6. Garde-fous mesurables à ajouter au code

Ils ne sont pas des tests, mais des vérifications faites par le plug-in lui-même avant d'appliquer une proposition. Les tests de la famille E les vérifient.

- Ne jamais appliquer une réduction de gain supérieure au plafond défini.
- Ne pas inverser la polarité sans corrélation suffisante (seuil à régler, voir doc 04).
- Refuser plutôt que deviner : analyse trop courte, trop peu de frappes, trop de bruit.
- Garder la proposition précédente restaurable (Revenir).

## 7. Critères de réussite

### 7.1 Niveaux de gravité

| Niveau | Signification | Action |
|---|---|---|
| Bloquant | Une propriété E échoue, une mesure synthétique s'écarte de plus que la tolérance, un cas doré change sans validation | Ne pas publier |
| Alerte | Le taux de refus varie de plus de 5 points, une médiane de base bouge, trop d'analyses proches d'un seuil | Examiner avant de publier |
| Information | Écarts mineurs, changements de formulation du Bilan | Noter dans le rapport |

### 7.2 Tolérances (hypothèses de départ, à calibrer)

| Grandeur | Tolérance |
|---|---|
| Niveau, signal synthétique | ± 0,1 dB |
| Spectre par bande, signal synthétique | ± 1 dB |
| Fréquence de corps | ± 1 bin de FFT (environ ± 12 Hz à 48 kHz) |
| Retard (signal pur) | ± 1 échantillon |
| Retard (kit réel) | ± 0,2 ms |
| Taux de refus | ± 5 points |
| Comparaison de rejeu, par réglage | ± 1e-6 relatif entre exécutions identiques |
| Cas doré | à définir par réglage (voir 5.7) |

## 8. Rapport de sortie

`drumtest compare A B` écrit un fichier lisible (Markdown) et un JSON.

```
Comparaison : profil kick-2026-10 (A) contre kick-2026-11 (B)
Corpus : jeu_validation, 42 analyses, 18 Kick, 12 Snare, 6 Toms, 6 OH

Refus          A: 3 (7 %)   B: 5 (12 %)   différence +5 points   ALERTE
Cas dorés      12 sur 12 dans la tolérance                        OK
Propriétés E   toutes vraies                                      OK

Réglages modifiés (Kick, Rock) :
  bodyGain       moyenne +0,3 dB   max +1,8 dB   (kit_07 : +1,8 dB)
  gateThreshold  moyenne −2,1 dB   max −6,0 dB   (kit_03 : −6,0 dB)

Analyses proches d'un seuil (± 5 %) :
  crest < 6        7 analyses
  crest > 10       4 analyses

À écouter : kit_03 (Kick, seuil de l'expander −6 dB), kit_07 (Kick, Body +1,8 dB)
```

La dernière rubrique est le point clé : le programme désigne **quelles prises écouter**, vous n'écoutez que celles-là.

**État (T2)** : implémenté dans `Source/Drum/DrumReplay.h`. Rubriques produites : refus (total, par rôle, par style, alerte au-delà de 5 points), propriétés (déterminisme E08), changements d'état valide / refus, réglages modifiés (moyenne, maximum, fichier), analyses proches d'un seuil (± 5 % : crest 6 et 10, bass/total 0,15, contraste d'enveloppe 12 et 18), prises à écouter (10 au plus, triées ; seuils d'écoute : 1 dB, 5 % en fréquence, 10 % autrement, constantes `ReplayThresholds`, hypothèses H). La rubrique « Cas dorés » n'existe pas encore (T6). Le rapport est écrit en Markdown et en JSON.

## 9. Tests humains (hors programme)

Un test automatique ne dit pas si c'est *bien*. Pour cela :

1. **Écoute A/B à niveau égal.** Original contre traité, après compensation du niveau (le niveau A/B du plug-in est fait pour ça).
2. **À l'aveugle.** L'ordre est tiré au hasard et l'auditeur ne sait pas quelle version est laquelle.
3. **Au moins 5 auditeurs** pour une première lecture ; plus pour une conclusion. Consigner les réponses par prise (colonne `labels.quality` du JSON).
4. **Un seul changement à la fois** entre deux versions comparées.
5. **Consentement et RGPD** pour toute donnée recueillie auprès de bêta-testeurs : mesures et réglages uniquement, jamais l'audio, finalité expliquée.

Les résultats alimentent le jeu de cas dorés (5.7) et les cibles du document 04.

## 10. Plan d'implémentation par jalons

| Jalon | Contenu | Livrable |
|---|---|---|
| T1 | Squelette `drumtest`, famille A (A01 à A04, A10) et E (E01, E02, E06, E07) | Premiers tests au vert |
| T2 | Export / rechargement des snapshots, commande `replay`, diff et taux de refus | Premier rapport de comparaison |
| T3 | Défauts artificiels pour Kit / Phase (D01 à D04) sur 3 à 5 kits réels | Test de non-régression de Kit / Phase |
| T4 | Signaux de A05 à A09 et A11 ; tests marqués « échec attendu » pour les problèmes connus | Les problèmes 2 et 3 du document 01 deviennent visibles |
| T5 | Famille J (overheads, room) | Couverture des rôles sans frappe |
| T6 | Cas dorés, sensibilité aux seuils, performance | Rapport complet |

Les tests A08 et A09 sont **volontairement écrits avant la correction** : ils échouent aujourd'hui, ce qui prouve qu'ils détectent le problème, et passent après.

## 11. Questions ouvertes

- **Q1** : générer les défauts artificiels à partir de vraies prises (plus réaliste) ou de signaux synthétiques (plus propre) ? Probablement les deux.
- **Q2** : comment tester l'aspect « réglage attendu » d'un profil sans cible externe ? Dépend du document 04 (cibles par style).
- **Q3** : budget CPU admis par instance (valeur à fixer après les premières mesures de H01).
- **Q4** : faut-il tester dans un hôte réel (Cubase, Reaper) pour la partie ARA et l'ordre des événements, ou seulement en console ? Les deux, mais la partie hôte reste manuelle.
- **Q5** : combien de kits suffisent pour que le jeu de validation soit significatif ? Au moins 10 dans chaque jeu avant de tirer des conclusions sur les seuils.
