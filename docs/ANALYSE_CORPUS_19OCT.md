# Analyse du corpus du 19 octobre 2026 (Track 1 à 4)

Premier passage complet de la chaîne `scripts/RunCorpus.ps1` sur les quatre nouveaux dossiers, avec la nouvelle commande `--vs-plugin` (le plug-in contre l'ingénieur). **Rien n'a été changé dans les profils ni dans le son** : ce document dit ce que les données permettent de conclure, et surtout ce qu'elles ne permettent pas encore.

Sorties : `Database_drum\out_v6\` (`pairs\`, `buses\`, `versus\`, `snapshots\`, `summary.md`). Lecture : `python scripts\CorpusTables.py versus|pairs|shapes|buses Database_drum\out_v6`.

## 1. Ce que contient chaque track et ce qui est exploitable

| Track | Contenu | Exploitable ? |
|---|---|---|
| **1** metal moderne (A.H.) | 4 stems (Kick, Snare, Toms, OH), `BUS_IN` / `BUS_OUT`, 43 s, 48 kHz. Addictive Drums 2, **kick et snare augmentés par des samples dans les MIX** | **Non en paires** : cohérence 0,24 à 0,46 partout, même sur `BUS_IN` → `BUS_OUT` (0,24) ; la Snare n'a que 12 s de jeu (20 s requises). Confirme ta mise en garde. Utile seulement comme statistiques (crest, plancher), pas comme courbes |
| **2** post punk (A.H.) | 10 stems + `BUS_IN` / `BUS_OUT`, 57 s, 48 kHz, vrai studio, sans sample, compression parallèle dans `BUS_OUT` | **Oui** : 8 paires acceptées (Kick In, Snare Top, Snare Bottom, Snare Top+Bottom, HH, OH, Room, bus). Écartées : Kick Out (cohérence 0,79, tout juste sous 0,8), Kick In+Out (0,74), Toms (10 s de jeu seulement) |
| **3** (ancien kit 1, sans `INFO.txt`) | `raw1` + 3 bus mixés (Kick, Snare, Drums), 44,1 kHz, sample sur la Snare | Bus Kick (0,90) et Snare (0,82) expliqués par leurs micros, comme le 17/10 |
| **4** (nouveau kit, sans `INFO.txt`) | `raw` + 3 bus mixés, 44,1 kHz, 18 pistes (6 toms, ride) | Bus Kick (0,63) et Snare (0,61) **non expliqués** : le bus contient autre chose que les micros fournis (sample ou déclencheur non fourni, réverbe, autre micro). Pas de courbe fiable |

**Deux ingénieurs** : A.H. (Tracks 1 et 2) et un autre (Tracks 3 et 4, précisé par l'utilisateur le 19/10). Mais les paires brut / stem ne viennent que d'A.H. (Track 2) ; l'autre ingénieur n'apporte que des bus, utiles pour le Kick et la Snare. **Les réglages d'EQ des Overheads et du Hi-hat reposent donc sur un seul ingénieur, une chanson, un kit.**

## 2. Plug-in contre ingénieur (Rock 75 %, Metal 75 % pour le Track 1)

`--vs-plugin` fait passer la piste brute dans la proposition Auto du plug-in (même chemin que `--analyse-wav` : analyse, deux passes d'aperçu, rendu), puis mesure la courbe d'égalisation équivalente du résultat **avec le même outil** que celle de l'ingénieur. Colonnes : **rms** = écart plug-in / ingénieur ; **ing** = écart de l'ingénieur à une courbe plate ; **rms < ing** veut dire que le plug-in se rapproche de l'ingénieur plus que ne le ferait un réglage neutre. Bandes fiables de 100 Hz à 8 kHz.

| Piste (rôle) | Paire | Bandes | rms | ing | Même sens | Lecture |
|---|---|---|---|---|---|---|
| Kick In (Kick) | oui | 18 | **3,7** | 5,5 | **16 / 17** | **Bon accord** : même forme, environ deux fois moins d'amplitude |
| Hi-hat | oui | 13 | **5,9** | 7,9 | 10 / 11 | Bon sens, trop doux (coupe des graves à renforcer) |
| Bus | oui | 17 | 1,0 | 1,4 | 5 / 7 | Les deux touchent à peine la courbe |
| Snare Top (Snare) | oui | 15 | 3,1 | 3,0 | 5 / 10 | Neutre : ni mieux ni pire que ne rien faire |
| Snare Top+Bottom | oui | 16 | 2,9 | 3,0 | 8 / 13 | Neutre |
| Snare Bottom (Snare) | oui | 18 | 8,4 | 8,3 | 8 / 17 | Neutre : l'ingénieur monte les aigus de +8 dB, le plug-in non |
| Overheads | oui | 20 | 5,1 | 5,2 | **6 / 17** | **Le plug-in est presque plat, l'ingénieur coupe les graves et monte les aigus** |
| Room | oui | 17 | **2,4** | 1,0 | 0 / 3 | **Pire que ne rien faire, mais pour une raison identifiée** : tout vient du module **Crush** (compression parallèle filtrée). Sans lui l'écart tombe à 1,5 dB et la courbe est plate. C'est un effet voulu, que cet ingénieur n'a pas utilisé |
| Kick Out, Kick In+Out, Toms | non | | | | | Paires écartées : valeurs indicatives seulement |
| Track 1 (tout) | non | 0 à 6 | n/a | n/a | | Non évalué (moins de 6 bandes fiables) |

### Courbes (dB, relatives au gain moyen 100 Hz - 5 kHz ; `?` = bande peu fiable)

| | 63 | 100 | 160 | 250 | 400 | 630 | 1k | 1,6k | 2,5k | 4k | 6,3k | 10k | 16k |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Kick In, ingénieur | −16,9 | −11,6 | −6,8 | −1,7 | −1,1 | +1,5 | +3,9 | +4,3 | +5,1 | +5,4 | +5,2 | +5,3 | +5,2 |
| Kick In, plug-in | −1,8? | −3,1 | −2,1 | −2,7 | −3,7 | −0,6? | +1,1 | +2,2 | +3,1 | +4,8 | +0,8 | −1,8 | −2,4 |
| Overheads, ingénieur | −17,7? | −9,9 | −4,8 | −3,4 | −1,8 | +0,7 | +2,6 | +4,0 | +5,8 | +6,2 | +7,1 | +6,8 | +7,4 |
| Overheads, plug-in | −7,4 | −2,0 | +0,2 | +0,5 | 0,0 | −0,9 | −0,1 | +0,6 | +0,8 | +0,4 | −1,0 | −1,1 | −0,1 |
| Hi-hat, ingénieur | −49? | −36? | −18,6 | −7,8 | +0,7 | +4,2 | +5,4 | +6,0 | +5,2 | +3,2? | +2,0? | −4,6? | −10,1? |
| Hi-hat, plug-in | −19,0 | −12,1 | −4,7 | +0,1 | +1,9 | +1,7 | +2,5 | +3,0 | +2,6 | +2,2 | +1,3 | +1,2 | +1,5 |
| Snare Bottom, ingénieur | −36? | −31? | −16,6 | −11,0 | −2,6 | +1,3 | +3,7 | +6,3 | +8,2 | +8,8 | +8,7 | +7,9 | +5,0? |
| Snare Bottom, plug-in | −3,0? | +0,3? | +0,4 | +1,0 | −3,2 | −1,3 | −0,1 | +0,5 | +1,4 | +1,9 | −1,2 | −4,5 | −4,8 |
| Room, ingénieur | −1,9 | +0,1 | +1,2 | −3,4? | −0,2 | +1,1 | +0,5 | −0,3 | −1,0 | −1,6 | −0,5 | +1,0 | +1,9 |
| Room, plug-in | −6,1 | −3,7 | −0,8 | −1,0 | −2,2 | +1,4 | +1,2 | +1,4 | +1,8 | +1,4 | −1,5 | −4,8 | −4,5 |

## 3. Confirmation par une deuxième source (Kick In)

Les bus du Track 3 (autre kit, autre morceau, ingénieur non confirmé) donnent la courbe appliquée au Kick In : **creux à 250-400 Hz (−3 à −5 dB)** et **bosse de présence à 2,5-4 kHz (+5,5 dB)**. Le Track 2 montre la même bosse (+5 dB à 2,5-4 kHz) et un creux plus léger à 250-400 Hz. Le plug-in fait déjà les deux (boîte −4,5 dB à 320 Hz, attaque +3,4 dB à 3 kHz). **Le profil du Kick est donc cohérent avec deux sources indépendantes ; rien à y changer sur ces deux zones.**

Où les deux sources divergent : les graves. Track 3 garde le grave (+3 dB à 63 Hz), Track 2 le coupe (−17 dB à 63 Hz). C'est un choix de style (post punk, kick serré), pas une règle : ne pas l'intégrer.

Pour la Snare, les deux sources ont un creux autour de 250 Hz (environ −6 dB) et une bosse à 2,5-4 kHz (+3 à +5 dB) ; Track 3 garde le grave de la Snare Up (+8 dB à 100 Hz), Track 2 non. Le plug-in n'a pas ce creux à 250 Hz sur la Snare (son EQ Rock coupe à 650 Hz) : c'est le seul écart que deux sources partagent, et il reste mineur (3 à 5 dB).

## 4. Dynamique : l'ingénieur gate très fort, le plug-in presque pas

Plancher de bruit du mixé par rapport au brut (gain retiré) : **−33 dB** (Kick In), −35 (Kick Out), −52 (Kick In+Out), −42 (Snare Bottom), **−61 et −66 dB** (Snare Top+Bottom, Snare Top). Le plug-in : −2 à +3 dB (expandeur de 8 dB en Rock sur le Kick, 4 dB sur la Snare). Les frappes détectées tombent de 320 à 156 (Kick In) et de 250 à 94 (Snare Bottom) : l'ingénieur coupe les fuites entre les coups.
Un gate profond est un choix de style (post punk, « agressif et crado » selon l'ingénieur, ou déclencheurs en metal) et pas un défaut à corriger : **à ne proposer que dans un style où c'est voulu**, pas dans Natural.

## 5. Ce que ces données ne permettent pas

- **Pas la règle du document 03** (8 paires et 3 mixeurs) : 8 paires acceptées mais **un seul mixeur** et **un seul morceau** ; Snare Top+Bottom dépend de Snare Top et Bottom, le bus est un cas à part. Ce qui est valide ici est un **constat sur un ingénieur**, pas une préférence générale.
- **Aucun jeu de validation** : tout changement réglé sur le Track 2 serait validé sur le Track 2 (surapprentissage). Il faut au moins un autre ingénieur et un autre morceau mis de côté.
- Les mesures sont celles d'une **courbe linéaire équivalente** : elles ne voient ni le gate ni la compression en tant que tels (seulement leurs effets sur les indices), et la couleur non linéaire reste ≈ −12 dB des deux côtés.
- Les mixes du Track 2 sont « agressifs et crados » : tendances fortes (±5 à ±17 dB), peu représentatives d'un réglage prudent.

## 6. Pistes de changement, de la mieux étayée à la moins étayée (**aucune n'est faite**)

| # | Piste | Étayée par | Risque |
|---|---|---|---|
| 1 | **Room** : retirer le Crush ? | Mesuré : le creux vient du Crush seul. Mais un « room crush » est un choix de style courant : **non fait**, à décider à l'écoute | choix de style |
| 2 | **Overheads** : passe-haut 180 Hz et montée d'aigus (+3,6 dB × force à 6 kHz) en Rock et Metal | Track 2 seul ; **FAIT, option désactivée** (section 7) | moyen : change le son des OH si activé |
| 3 | **Hi-hat** : coupe des graves plus forte | Track 2 seul ; le passe-haut est déjà à son maximum (250 Hz, plage du contrôle), la coupe passe donc par la bande Corps (−7,1 dB × force à 200 Hz) ; **FAIT, option désactivée** (section 7) | faible |
| 4 | **Snare Bottom** : traiter la position « Bottom » autrement (coupe des graves plus haute, montée d'aigus) | Track 2 seul (Track 3 : Snare Down ambiguë) | moyen : demande de lire la position du micro dans la proposition |
| 5 | **Gate plus profond** en Rock / Metal | Track 2 seul, style très marqué | élevé : change fortement le son, dépend du morceau |
| – | Kick : EQ | deux sources | rien à changer |

Quand de nouvelles données arriveront (autre ingénieur, autre morceau), `--vs-plugin` donne immédiatement la même table : si la tendance se répète, la piste passe de « un ingénieur » à « deux », et on la propose derrière une option désactivée par défaut, à écouter.

## 7. Ce qui a été implémenté : option `cymbalReference` (**activée le 19/10/2026**, `profileVersion` 3, plug-in 0.11.2)

`ProposalPolicy::cymbalReference` (`ProposalDefaults::cymbalReference = false`) donne aux profils **Overheads** et **Hi-hat** une EQ de référence, **en Rock et Metal seulement** (Natural reste sans coloration imposée) et proportionnelle à la force (intensité nulle : rien). L'utilisateur a demandé de l'activer pour l'essayer en vrai (« si ce n'est pas bien on reviendra en arrière »). Retour arrière : `ProposalDefaults::cymbalReference = false` (et `profileVersion` 2) ; le filet de 270 propositions et le test d'équivalence historique tournent avec l'option coupée et restent identiques.

| | Réglage | Écart plug-in / ingénieur (Rock 75 %) | Même sens | Corrélation des formes |
|---|---|---|---|---|
| Overheads, défaut | passe-haut 100 Hz, EQ neutre | 5,1 dB (ing 5,2) | 6 / 17 | 0,26 |
| Overheads, option | passe-haut 180 Hz, air +4,0 dB à 6 kHz | **2,8 dB** | **14 / 17** | **0,91** |
| Hi-hat, défaut | passe-haut 250 Hz, EQ neutre | 5,9 dB (ing 7,9) | 10 / 11 | 0,97 |
| Hi-hat, option | corps −8,0 dB à 200 Hz | **3,4 dB** | **11 / 11** | **0,99** |

**Ces chiffres sont mesurés sur les données qui ont servi à choisir les réglages (un ingénieur, une chanson) : ils montrent que le réglage reproduit cette courbe, pas qu'il est meilleur pour d'autres sources.** Les valeurs sont volontairement plus douces que l'ingénieur (−12 dB sur le corps du Hi-hat irait encore plus près : 2,3 dB, mais ce serait épouser un style agressif). Mesure : `ZeroEffortDrumTests.exe --vs-plugin <brut> <rôle> <style> <intensité> <mixé> [--no-reference]` ; `--set id=valeur` et `--off id` testent d'autres valeurs sans recompiler. L'option n'est pas enregistrée dans les snapshots.

## 8. Double pédale (Track 1)

Le Kick du Track 1 sert surtout à vérifier la détection des doubles frappes (J4a, active depuis le 17/10). Avec J4a, le Kick compte **263 frappes** contre 179 sans (+47 %), contre +5 à +14 % sur les trois autres Kicks : la double pédale est mieux suivie. **Aucun réglage proposé ne change** sur cette prise (0 réglage différent, Metal 75 %) : c'est sans risque.

## 9. Questions ouvertes

- **Style et nom de l'ingénieur des Tracks 3 et 4** : autre ingénieur que A.H. (confirmé), mais pas de `INFO.txt` ; le manifeste les note « B » en Rock.
- **Toms du Track 4** : 6 toms et un ride. Seuls Tom 1 à 3 sont pris comme snapshots (rôles 2, 3, 4) ; Tom 4 à 6 et Ride attendent ta confirmation (Tom 6 = tom basse ?).
- **Track 4, bus non expliqués** : y a-t-il un sample ou un autre micro dans les bus Kick et Snare ?
- **Dossiers `raw2`, `raw3`, `raw4` introuvables** (voir `ETAT_ACTUEL.md`) : les tests de kit réel du plug-in sont **ignorés** tant que `Kick_12`, `Snare_12`, `OvH L_12` et `OvH R_12` ne sont pas retrouvés.
