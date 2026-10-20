# Calcul des presets, fonction par fonction

<!-- consolidated-archive -->
> **Archive historique.** Référence consolidée : [SPECIFICATIONS](../../SPECIFICATIONS.md).
> Avancement vérifié : [PROJET](../../PROJET.md). Les états et chemins ci-dessous
> sont historiques ; ne plus entretenir ce document comme un suivi actif.
<!-- /consolidated-archive -->

**Document 4 sur 4** | Version du document : 0.1 | Dépend de : 01 (mesures), 03 (cibles issues des paires brut / mixé) | Testé par : 02

Les valeurs de la colonne **Actuel** viennent de `KickProfile.h`, `SnareProfile.h` et `TomProfile.h`. Je n'ai pas vu `ProfileAssembly.h` : pour la caisse claire et les toms, je ne connais que les paramètres qu'ils lui passent, pas ce qu'il en fait.

---

## 1. Objet

Décrire, pour **chaque fonction du plug-in**, comment passer d'une mesure (document 01) et d'une cible (document 03) à un réglage, de façon à ce que :

- chaque constante ait une **origine** (mesure, cible de la base, ou choix de style) ;
- la **calibration** d'une constante suive une procédure claire ;
- le **Bilan** puisse expliquer chaque réglage ;
- un nouveau style ou un nouveau rôle s'ajoute sans réécrire du code.

## 2. Modèle général

Pour chaque réglage :

```
mesure  ->  écart à la cible  ->  correction brute  ->  zone morte  ->  bornes  ->  × intensité  ->  réglage
                                                              ^                  ^
                                                         confiance            style
```

| Étape | Règle |
|---|---|
| Mesure | Définie dans le document 01 (identifiant L, D, H, S, P, T, O, R, K). |
| Cible | Valeur ou courbe par `rôle × style` (section 3). |
| Écart | `cible − mesure`, en dB pour les niveaux, en octaves pour les fréquences. |
| Correction brute | `k × écart`, avec `k` entre 0 et 1 (on ne corrige qu'une part de l'écart ; hypothèse de départ : `k = 0,5`). |
| Zone morte | Pas de correction sous un petit écart (hypothèse : 1 dB, ou 5 % en fréquence). |
| Bornes | Plafonds par fonction (section 5). |
| Intensité | Multiplie les gains (`× intensité`), de 0 à 100 %. À 0 %, tout traitement ajouté est éteint. |
| Confiance | Si la mesure est jugée peu fiable (peu de frappes, cohérence faible), la correction est réduite ou annulée, et le Bilan le dit. |
| Refus | Si une condition d'arrêt est vraie, aucun réglage n'est proposé et la raison est donnée. |

**Principe directeur** : *refuser plutôt que deviner*. Un réglage faux est pire que pas de réglage.

## 3. Cibles par rôle et par style

### 3.1 Contenu

Une cible est un tableau de valeurs avec leur dispersion, tirées du document 03 (médiane, P25, P75, nombre de paires, nombre de mixeurs).

| Famille de cible | Contenu | Utilisée par |
|---|---|---|
| Forme spectrale | écart de timbre en dB par tiers d'octave (31 valeurs) | EQ, filtres |
| Dynamique | réduction visée (P50 et P95), plafond, rapport typique | compresseur, multibande, glue |
| Expansion | plage et atténuation entre frappes | expander, nettoyage |
| Transitoire | `delta_ratio_attaque_sustain` | punch, impact |
| Couleur | `couleur_non_lineaire_db` | saturation, drive |
| Stéréo | largeur et corrélation (OH, room) | réglages stéréo, ambiance |

### 3.2 Format

```json
{
  "role": "Kick", "style": "rock", "version": "2026-10",
  "nb_paires": 14, "nb_mixeurs": 5, "statut": "publie",
  "forme_spectrale_db": { "p50": [ ... 31 valeurs ... ], "p25": [ ... ], "p75": [ ... ] },
  "dynamique": { "gr_p50": 3.1, "gr_p95": 6.0, "plafond": 8.0, "rapport": 3.0 },
  "expansion": { "range_db": 8.0 },
  "transitoire_db": 2.0,
  "couleur_db": -28.0
}
```

### 3.3 Les trois styles aujourd'hui

Aujourd'hui, Naturel, Rock et Metal sont des branches de code (`if (style == metal) …`). La cible est d'en faire **trois jeux de cibles** dans un tableau, plus une **interpolation** : un curseur `s` entre 0 (Naturel) et 1 (Metal) donne `valeur = a + s × (b − a)`, ce qui permet des styles intermédiaires (jazz, pop) sans écrire de nouvelle branche.

## 4. Règles communes

1. **Seuils exprimés relativement au signal** (percentiles), jamais en dB absolus. Les seuils absolus actuels (par exemple −35 dB pour chaque bande du multibande) sont à remplacer par des valeurs relatives.
2. **Plafonds avant intensité.** Plafond appliqué d'abord, intensité ensuite, de façon à ce que l'intensité ne puisse pas dépasser un plafond.
3. **Pas de réglage sans mesure.** Un réglage dont la mesure est absente garde sa valeur par défaut (et le Bilan l'indique).
4. **Une seule source par réglage.** Un réglage est calculé par une seule règle ; pas de superposition de deux règles sur le même paramètre.
5. **Ordre du signal** : Filtres → Expander → Focus / Nettoyage → EQ → Compresseur → Bus → Précision → Impact. Les mesures sont faites sur le **signal brut** ; l'effet des fonctions en amont sur le niveau n'est pas encore pris en compte (voir risque R1, section 8).

## 5. Fiche par fonction

Légende : **Actuel** = valeur trouvée dans le code ; **Cible de calibration** = comment la remplacer par une valeur issue des données.

### 5.1 Filtres (passe-haut, passe-bas)

| Réglage | Entrées | Actuel | Cible de calibration |
|---|---|---|---|
| Passe-haut (Kick) | `corps_hz` (T01) | `clamp(20, 35, corps × 0,4)` | médiane de la coupure observée sur les paires |
| Passe-haut (Snare) | rôle | 70 Hz fixe | `coupure_utile_hz` (O06) ou fréquence de corps × 0,4 |
| Passe-haut (Toms) | rôle | 35 Hz fixe | idem |
| Passe-haut (OH, room) | `coupure_utile_hz`, `fuite_grave_db` | — | O05 et O06 |
| Passe-bas | rôle | 18 kHz fixe | `rolloff95_hz` (S03) si la prise est déjà filtrée |
| Pente | | 12 dB / octave | fixe |

### 5.2 Expander / Gate

| Réglage | Entrées | Actuel (Kick) | Cible de calibration |
|---|---|---|---|
| Activation | P20 et P90 de l'enveloppe gate (L04), `plage_dyn` (D03) | actif si P90 − P20 ≥ 12 dB | seuil de contraste observé dans les paires qui utilisent un gate |
| Seuil | P35, P90 | `clamp(−80, −12, min(P35 + 6, P90 − 12))` | rester sous le niveau des ghost notes (H04) moins 3 dB |
| Rapport | style, intensité | `1 + (1,5 / 1 / 0,5) × intensité` (Metal / Rock / Naturel) | rapport estimé (doc 03, 6.1) |
| Range | style, intensité | `(12 / 8 / 6) × intensité` dB | `exp_range_db` par style |
| Release | style | 80 / 100 / 160 ms (Metal / Rock / Naturel) | proche de `decay_t60` (H06) × facteur à calibrer |
| Hold | style | 15 (Metal) ou 25 ms | 20-30 ms |
| Hystérésis | | 4 dB | fixe |
| Caisse claire / toms | — | release 180 ms / 350 ms (via profil) | `decay_t60` du rôle |

Protection des ghost notes : le seuil ne doit pas dépasser `P_ghost − 3 dB` quand le jeu en comporte (mesure H04).

### 5.3 Focus / Présence / Nettoyage

| Réglage | Entrées | Actuel (Kick) | Cible de calibration |
|---|---|---|---|
| Détecteur grave, bas | `corps_hz` | `clamp(25, 55, corps × 0,3)` | fixe par rapport au corps |
| Détecteur grave, haut | `corps_hz` | `clamp(140, 300, corps × 2)` | idem |
| Nettoyage aigu, profondeur | style, intensité, `diaphonie_db` (K04) | `(18 / 12 / 6) × intensité` dB | atténuation hors frappe observée (doc 03, 7.1) |
| Renfort accordé, fréquence | `corps_hz` | `clamp(40, 80, corps × 0,75)` | fréquence de corps du kick mixé |
| Renfort, quantité | style, intensité | `(45 / 35 / 15) × intensité` % | à relier à l'écart de corps (P02) |
| Renfort, extinction | style | 140 / 180 / 220 ms | `decay_t60` du kick |

### 5.4 Égalisation à 4 bandes

Bandes du kick : Poids, Carton, Batte, Air. Pour chaque bande :

```
gain = clamp( k × (cible_bande − mesure_bande), min, max ) × intensité      (si |écart| > zone morte)
```

| Bande (Kick) | Fréquence | Gain actuel | Q actuel | Cible de calibration |
|---|---|---|---|---|
| Poids | `corps_hz × 0,75`, bornée 45-90 Hz (styles non naturels) | +4,5 / +3,5 dB (Metal / Rock) | 2,5 / 2,0 | médiane de `delta` 40-120 Hz |
| Carton | résonance mesurée (T03), sinon 400 / 320 Hz | −(0,5 à 3) dB mesuré, puis −6 / −4 dB de style, plancher −8 dB | 1,8 / 1,4 | médiane de `delta` 200-500 Hz |
| Batte | 4500 / 3000 Hz | +4,5 / +3,0 dB | 2,0 / 1,5 | médiane de `delta` 2-5 kHz |
| Air | fixe | 0 à −3 dB selon le bleed aigu | 1,0 | `eclat_aigu` (O04) ou bleed aigu (P03) |

- Pour **Snare** (Corps / Boîte / Attaque / Air) : valeurs actuelles venant du profil : corps 180, boîte 650, attaque 4500 Hz. À remplacer par `corps_hz` et par la **bague** mesurée (T03 pour la caisse claire).
- Pour **Toms** : corps = pic spectral 45-200 Hz (mesure existante), boîte 450, attaque 3500 Hz. La boîte devrait dépendre du tom (aigu ou basse).
- Pour **Overheads et room** : les bandes Grave / Boue / Présence / Brillance se calculent à partir de O01 à O07 (document 01, 5.10) et de la forme spectrale cible.
- **Q global** : relié au style (0,85 Naturel ; 1,0 Rock ; 1,2 Metal) ; à calibrer sur l'étroitesse des corrections observées.

### 5.5 Compresseur

| Réglage | Entrées | Actuel (Kick) | Cible de calibration |
|---|---|---|---|
| Activation | `crest` (aujourd'hui global), intensité | crest > 10 dB et intensité > 0 | `crest_actif` (D02) ; pas d'activation si `compression_estimee` (D05) montre déjà une prise très comprimée |
| Rapport | style | 4 / 3 / 2 (Metal / Rock / Naturel) | `dyn_rapport` des paires |
| Attaque | style | 4 / 8 / 25 ms | `dyn_attaque_ms` (ordre de grandeur) |
| Release | style | 80 / 100 / 140 ms | `dyn_release_ms` (ordre de grandeur) |
| Réduction visée | style, intensité | `(6 / 4 / 1,5) × intensité` dB | `dyn_gr_p50` |
| Plafond de réduction | style, intensité | `(10 / 8 / 3) × intensité` dB | `dyn_plafond_gr_db` |
| Seuil | P95 de l'enveloppe, réduction visée, rapport | `clamp(−60, −6, P95 − visée / (1 − 1/rapport))` | formule conservée |
| Mélange | style | 70 % (Naturel), sinon 100 % | proportion de compression parallèle estimée |
| Caisse claire / toms | profil | rapport 4 / 3 ; attaque 20 ms ; release 180 / 250 ms ; visée 3 dB ; plafond 6 dB | idem |

### 5.6 Multibande

| Réglage | Entrées | Actuel (Kick) | Cible de calibration |
|---|---|---|---|
| Activation | compresseur actif | égale à celle du compresseur | idem |
| Croisement grave / médium | `corps_hz` | `clamp(100, 180, corps × 1,4)` | fixe par rapport au corps |
| Croisement médium / aigu | style | 2500 Hz (Metal), 2000 Hz sinon | fréquence séparant corps et attaque |
| Réduction visée par bande | réduction visée globale | `{ t, t × 0,5, t × 0,25 }` | répartition issue des paires multibandes (si disponibles) |
| Plafonds par bande | plafond global | `{ m, m × 0,6, m × 0,4 }` | idem |
| Rapports | style | `{ rapport, 2,5 (Metal) ou 2, 1,5 }` | idem |
| Attaques | style | `{ 25 (Naturel) ou 12, 12, 15 }` ms | idem |
| Releases | | `{ release, 110, 80 }` ms | idem |
| Seuil par bande | | **−35 dB fixe** | **à remplacer par un seuil relatif au P95 de chaque bande** |

### 5.7 Bus : glue et ambiance

| Réglage | Entrées | Valeurs vues à l'écran (exemple) | Cible de calibration |
|---|---|---|---|
| Glue : seuil | P95 de l'enveloppe du bus | −32,1 dB | `P95 − réduction visée / (1 − 1/rapport)` |
| Glue : rapport | | 2:1 | 2:1 à 3:1 |
| Glue : attaque / release | | 30 ms / 100 ms | à calibrer sur le bus |
| Glue : réduction maximale | | 4,1 dB | 3 à 4 dB visés |
| Glue : sidechain passe-haut | | 80 Hz | fixe |
| Ambiance : dose, taille, pré-délai | `decay_t60` (R01), `niveau_vs_oh` (R03) | 12 %, 22 %, 8 ms | cible de room par style |
| Ambiance : coupe-bas, coupe-haut | | 180 Hz, 6,5 kHz | fixe |

Valeurs relevées sur une capture d'écran de l'onglet Bus, à titre d'exemple, pas comme préréglage officiel.

### 5.8 Précision (EQ dynamique) et Impact (finitions)

| Réglage | Actuel | Cible de calibration |
|---|---|---|
| EQ dynamique, fréquence | 650 Hz (grave) et 6,5 kHz (aigu), vus à l'écran | résonance mesurée (T03), bande d'aigu agressive (O03) |
| EQ dynamique, seuil | — | P75 du niveau de la bande |
| EQ dynamique, profondeur | — | écart observé dans les paires, borné |
| Punch (transitoire) | `(3 / 2 / 0,5) × intensité` dB | `delta_ratio_attaque_sustain` |
| Drive grave (saturation) | `(6 / 4 / 1) × intensité` dB | `couleur_non_lineaire_db` |
| Crush, couleur, clip | désactivés à 0 % ; absents sur Hi-hat et Overheads | idem |

### 5.9 Kit / Phase

| Réglage | Entrées | Règle | Calibration |
|---|---|---|---|
| Retard proposé | corrélation croisée contre les overheads (K01) | argmax de la corrélation, dans une fenêtre autorisée (bornes 0 à 10 ms, à vérifier) | tests D01 à D04 du document 02 |
| Polarité | corrélation (K02) | inversion si corrélation négative **et** suffisamment forte | seuil à régler sur le corpus |
| Force | mode, force 0-100 % | retard appliqué = retard proposé × force | choix utilisateur |
| Mode | | Analyse (mesure) / Suggérer (par défaut) / Auto | |
| Refus | confiance de la corrélation (`correlation_max`) | pas de proposition sous un seuil de confiance | à régler sur le corpus |

## 6. Procédure de calibration d'une constante

1. **Identifier** la constante, sa fonction et son origine actuelle (colonne « Actuel »).
2. **Choisir la source de la cible** : forme spectrale, dynamique, etc. (doc 03).
3. **Vérifier** que la cible repose sur au moins 8 paires et 3 mixeurs (doc 03, section 9). Sinon, garder la valeur actuelle.
4. **Écrire la nouvelle règle** (formule et bornes) dans la fiche de la fonction.
5. **Rejouer** sur le corpus (document 02, `drumtest replay`) et lire le rapport de comparaison.
6. **Vérifier** les propriétés E (document 02, 5.6), les cas dorés et les refus.
7. **Écouter** les prises signalées par le rapport, en A/B aveugle à niveau égal.
8. **Valider sur le jeu de validation** (document 02, principe 4).
9. **Consigner** : date, ancienne valeur, nouvelle valeur, origine, résultat des tests.

Une seule constante change à la fois, sauf si elles forment un ensemble inséparable (par exemple seuil, rapport et réduction visée).

## 7. Format de description d'un preset calculé

Chaque réglage produit s'accompagne de son explication, ce qui alimente le Bilan et la base de données.

```json
{
  "fonction": "EQ", "parametre": "bodyGain", "valeur": 4.8, "unite": "dB",
  "regle": "k * (cible - mesure), borne, x intensite",
  "mesure": { "id": "S01", "valeur": -4.1 },
  "cible": { "valeur": 2.4, "p25": 0.9, "p75": 3.6, "nb_paires": 14 },
  "intensite": 0.85, "confiance": 0.82,
  "raison_bilan": "Poids +4,8 dB à 79 Hz : le grave est 6,5 dB sous la cible Rock."
}
```

## 8. Risques et points à vérifier

| # | Risque | Précision |
|---|---|---|
| R1 | Les seuils (percentiles) sont mesurés sur le signal **brut**, alors que le compresseur reçoit le signal **après** filtres, expander et EQ | Un +4,8 dB d'EQ sur le corps déplace le niveau vu par le compresseur. À vérifier si `ProfileAssembly` compense ; sinon, estimer le gain moyen apporté par l'EQ et l'ajouter au seuil |
| R2 | Seuil de −35 dB fixe pour chaque bande du multibande | Valeur absolue : inadaptée aux prises faibles ou fortes |
| R3 | Beaucoup de constantes sont posées à l'oreille (`crest > 10`, `bass / total < 0,15`, `loud − quiet >= 12`) | À calibrer sur la base (document 02, rapport de sensibilité) |
| R4 | Profils Snare et Toms presque entièrement fixes | La mesure du corps (caisse claire) et de la bague manque |
| R5 | `defaultSettings()` réinitialise les réglages manuels | Comportement voulu d'après la documentation ; à bien signaler dans l'interface |
| R6 | Interactions entre fonctions : un gate agressif, une compression et un drive peuvent se cumuler | Prévoir un plafond global de « quantité de traitement » et un test de propriété (E) pour le vérifier |

## 9. Ajouter un style ou un rôle

**Nouveau style (par exemple Jazz)**

1. Réunir 8 paires brut / mixé au minimum, de 3 mixeurs différents (doc 03).
2. Calculer les cibles de ce style (section 3).
3. Ajouter le jeu de cibles au tableau ; l'interpolation entre styles existe déjà.
4. Ajouter 3 cas dorés, lancer les propriétés E (en particulier E05, ordre des styles : à étendre).
5. Écouter en A/B aveugle sur 5 prises avant publication.

**Nouveau rôle (par exemple Hi-hat)**

1. Définir les mesures du rôle dans le document 01 (section 6).
2. Définir ses fonctions actives et inactives (pas de drive, par exemple).
3. Ajouter une fiche par fonction dans ce document.
4. Écrire les tests de défauts propres au rôle (document 02).

## 10. Questions ouvertes

- **Q1** : valeur de `k` (part de l'écart corrigée) et zone morte : à régler par rôle, ou fixes ?
- **Q2** : le Bilan peut-il afficher la dispersion de la cible (« entre +1,5 et +4,6 dB chez les mixeurs ») pour montrer l'incertitude à l'utilisateur ?
- **Q3** : doit-on proposer à l'utilisateur de choisir parmi plusieurs variantes (Précis / Doux) plutôt qu'un seul preset ?
- **Q4** : plafond global de « quantité de traitement » (R6) : comment le mesurer simplement ?
- **Q5** : comment gérer les prises hybrides (kick avec samples) : désactiver certaines corrections, ou une détection dédiée ?
