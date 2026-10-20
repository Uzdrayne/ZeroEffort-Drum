# Comparer une piste brute et la même piste mixée

<!-- consolidated-archive -->
> **Archive historique.** Référence consolidée : [SPECIFICATIONS](../../SPECIFICATIONS.md).
> Avancement vérifié : [PROJET](../../PROJET.md). Les états et chemins ci-dessous
> sont historiques ; ne plus entretenir ce document comme un suivi actif.
<!-- /consolidated-archive -->

**Document 3 sur 4** | Version du document : 0.1 | Dépend de : `01_cahier_des_charges_mesures.md` | Alimente : `04_calcul_des_presets.md`

---

## 1. Objet

Savoir **ce qu'un mixeur a fait** à une piste de batterie en comparant la prise brute et la même piste après son traitement. Les différences, mesurées sur beaucoup de paires, donnent les **cibles par rôle et par style** utilisées par les presets (document 04).

Ce qu'on cherche à extraire :

- l'**égalisation équivalente** (courbe en dB par bande) ;
- la **compression équivalente** (seuil, rapport, réduction typique) ;
- l'**expansion / gate** (atténuation entre les frappes) ;
- la **couleur ajoutée** (saturation, harmoniques) ;
- la **modification de transitoire** et, pour les overheads et la room, la **modification stéréo**.

## 2. Ce qu'on peut et ne peut pas en tirer

| On peut | On ne peut pas |
|---|---|
| Estimer l'effet moyen d'un traitement sur une piste | Retrouver les réglages exacts de chaque plug-in |
| Dégager une tendance sur 10 à 30 mixes | Dire que cette tendance est « la bonne » : un mixeur règle pour une chanson précise |
| Mesurer des écarts de timbre et de dynamique | Séparer une compression en série d'une compression parallèle |
| Comparer des styles entre eux (Rock contre Metal) | Conclure sur un style avec 3 paires |

Conséquence : les cibles issues de cette méthode sont des **points de départ** (médiane et dispersion), jamais des vérités. Elles sont toujours accompagnées de leur dispersion et du nombre de paires.

## 3. Données nécessaires

### 3.1 Ce qu'est une « paire valide »

- **Brut** : la piste telle qu'enregistrée (après éventuel alignement, avant tout traitement créatif).
- **Mixé** : **la même piste après les inserts du mixeur, avant le bus de batterie et le master.** Il s'agit d'un « stem » de piste, pas du mix complet.

Un mix complet ne convient pas : il contient les autres instruments, la compression du bus et le master, et l'effet d'une piste ne s'y isole plus.

**3.1 bis. Le bus d'instrument (ajouté le 17/10/2026, à partir du premier corpus réel).** Dans les sessions réelles, on dispose souvent d'un **bus d'instrument** (Kick Bus = In + Out + Sub, Snare Bus = Top + Bottom + sample) et non d'un stem par micro. Le modèle de la section 5 (un seul filtre `H = Sxy / Sxx`) ne s'y applique pas : mesuré sur le kit 1, un seul micro n'explique que 0,34 à 0,68 de cohérence du bus (la méthode écarte la paire), alors que **la somme filtrée de tous les micros** l'explique à 0,90 (Kick Bus) et 0,82 (Snare Bus avec son sample), contre 0,77 sans le sample. Le bus est donc traité par une **régression multi-entrées** : `y = Σ Hᵢ xᵢ`, `Hᵢ` estimés par fréquence avec la matrice des spectres croisés (`H = (Sxx + λI)⁻¹ Sxy`, λ = 10⁻⁴ × la trace, pour les micros très corrélés), et on retient la **cohérence multiple** `γ²ₘ = Re(Sxyᴴ H) / Syy`. Un bus est utilisable si la cohérence multiple médiane (100 Hz à 8 kHz) atteint 0,8 : sinon, un micro, un sample ou une réverbe manque dans les entrées fournies. Ce qu'on en tire : le **gain par micro et par bande** dans le bus, et le **décalage de chaque micro** vu par le mixeur (voir 4). Outil : `ZeroEffortDrumTests.exe --bus`. Limite : si deux micros sont presque identiques (même contenu, mêmes instants), les `Hᵢ` individuels ne sont pas identifiables ; on ne garde que leur somme.

### 3.2 Méthode d'identification

Chaque paire est décrite par un fichier `meta.json` : rôle, micro, style, mixeur (identifiant anonyme), source, **licence**, fréquence d'échantillonnage, remarques (samples de remplacement ajoutés, traitement parallèle, reverb sur la piste…).

### 3.3 Où trouver des paires

- Des sessions pédagogiques fournissant les pistes brutes et les stems traités (vérifier la licence : souvent limitée à un usage éducatif ou de recherche).
- Vos propres mixes : vous gardez la piste brute et vous exportez la piste après vos inserts.
- Des mixeurs ou bêta-testeurs qui acceptent de partager des paires (consentement écrit, mesures seulement si possible).
- Des défauts fabriqués (section 11) : pas des paires réelles, mais un banc d'essai de la méthode.

## 4. Étape 1 : alignement

La comparaison n'a de sens que si les deux signaux sont superposés. Tout écart d'alignement crée de faux résultats (peignes dans la courbe d'égalisation, mauvais ratio de compression).

| Aspect | Procédure | Contrôle |
|---|---|---|
| Fréquence d'échantillonnage | Même fréquence des deux côtés ; sinon rééchantillonner la plus basse vers la plus haute | Refuser une paire avec des fréquences différentes sans conversion documentée |
| Temps | Corrélation croisée sur l'enveloppe (blocs de 1 ms, **sur tout le fichier** : les exports mixés commencent à des instants arbitraires, 12,25 s après le brut sur le corpus réel, ce qui dépasse largement la latence d'un plug-in), puis affinage sur le signal (±128 échantillons, sur la partie la plus forte) pour trouver le décalage ; corriger par décalage d'un nombre entier d'échantillons | Décalage résiduel < 1 échantillon ; **le décalage est mémorisé par micro** : brut / mixé = latence + alignement de phase fait par le mixeur (Kick In, Out et Sub : −540 221, −540 263 et −540 361 échantillons sur le kit 1, soit 1,0 et 3,2 ms d'écart entre micros) |
| Voies | Un fichier stéréo est replié en mono par la **moyenne** des canaux (pas par le maximum de l'analyse ARA : la comparaison doit rester linéaire) ; l'analyse stéréo des overheads et de la room (L / R, mid / side, section 7.3) est à faire | Noter le repli dans la ligne de la paire |
| Polarité | Signe du pic de corrélation ; mémoriser s'il y a inversion | Noter dans la ligne de la paire |
| Longueur | Recouper les deux fichiers sur la même durée utile | Au moins 20 s de jeu |
| Niveau | **Ne pas** égaliser avant d'estimer : on calcule d'abord le gain réel (section 5) | Le gain estimé doit être cohérent (voir 5.3) |

**Contrôle d'alignement.** Après correction, calculer la **cohérence** entre brut et mixé (section 5.1). Si la cohérence moyenne est trop faible sur les bandes principales, la paire est écartée : le mixeur a ajouté trop de choses (reverb, samples, parallèle) pour qu'on comprenne l'effet. Seuil de départ : cohérence médiane ≥ 0,8 entre 100 Hz et 8 kHz (hypothèse, à calibrer).

## 5. Étape 2 : égalisation équivalente

### 5.1 Estimation de la fonction de transfert

Pour chaque paire, avec x = brut, y = mixé, et des trames de 4096 points (mêmes que l'analyse) :

- spectre croisé `Sxy`, spectre propre `Sxx`, moyennés sur les trames (méthode de Welch, fenêtre de Hann, recouvrement 50 %), **sur les trames actives seulement** (niveau du brut au plus 40 dB sous le P95 : le silence ne porte aucune information et fausse la cohérence) ;
- **réponse estimée** `H(f) = Sxy / Sxx` ; son module en dB est la courbe d'égalisation équivalente. **Dans une bande de 1/3 d'octave, on moyenne les modules** (`Σ|Sxy| / Σ Sxx`), jamais le complexe : la phase de `H` tourne dans la bande et une moyenne complexe s'annulerait ;
- **cohérence** `γ²(f) = |Sxy|² / (Sxx · Syy)`, entre 0 et 1 ;
- **vérifié sur paires synthétiques** (EQ de +6 dB à 3 kHz, gain, retard, inversion de polarité) : courbe retrouvée à 0,12 dB près, retard au échantillon près, cohérence 0,99999.

La cohérence indique où le modèle « filtre linéaire » est fiable. Là où elle chute, le traitement n'est pas linéaire (compression, saturation, gate) ou du bruit domine.

### 5.2 Mise en forme

1. Lisser `|H|` en 1/3 d'octave (31 bandes, comme S01 du document 01).
2. **Ne retenir que les bandes dont la cohérence moyenne est ≥ 0,8** (hypothèse) ; les autres sont marquées « non fiables ».
3. Retirer le **gain moyen** de la courbe entre 100 Hz et 5 kHz pour obtenir une **forme** : on compare des timbres, pas des volumes. Le gain retiré est stocké à part (`gain_moyen_db`).
4. Stocker : courbe de forme (31 valeurs), cohérence par bande, gain moyen.

### 5.3 Contrôles de vraisemblance

- Écart de gain entre le début et la fin du fichier > 3 dB : automation de niveau, paire à examiner.
- Courbe avec des pics étroits (> 10 dB en moins d'un tiers d'octave) : signe d'un défaut d'alignement ou d'un peigne, pas d'une EQ.

## 6. Étape 3 : dynamique équivalente

### 6.1 Courbe statique entrée / sortie

1. Calculer l'enveloppe (pics de fenêtres de 10 ms, comme L04) du brut et du mixé, **après compensation du gain** de 5.2.
2. Associer, pour chaque fenêtre, le niveau d'entrée et le niveau de sortie.
3. Regrouper par tranches de 2 dB d'entrée ; pour chaque tranche, prendre la **médiane** du niveau de sortie.
4. Lire la courbe obtenue :
   - sous un coude, pente proche de 1 : pas de traitement ;
   - au-dessus, pente `1 / rapport` : compression ;
   - très bas niveaux, pente > 1 : expansion ou gate.

| Grandeur estimée | Lecture |
|---|---|
| `seuil_db` | niveau d'entrée où la pente passe sous 1 (régression par morceaux, à tolérance ± 3 dB) |
| `rapport` | inverse de la pente au-dessus du seuil |
| `gr_p50`, `gr_p95` | réduction de gain médiane et à P95 (entrée moins sortie, normalisée) |
| `plafond_gr_db` | réduction maximale observée sur les frappes fortes |
| `range_expander_db` | atténuation des niveaux sous le plancher de bruit (par rapport au brut) |

### 6.2 Temps de réponse (approximatifs)

- **Attaque** : pour les frappes fortes, temps entre le début de la montée et le moment où la réduction atteint 63 % de sa valeur finale.
- **Release** : temps de retour de la réduction à 37 % de sa valeur après le pic.

Ces deux estimations sont **grossières** (vous mesurez un effet combiné de plusieurs traitements) ; on les utilise comme ordre de grandeur, pas comme réglage.

### 6.3 Indices simples

- `delta_crest_db` : crest du mixé moins crest du brut (en version active, D02).
- `delta_plage_dyn_db` : plage dynamique du mixé moins celle du brut.
- `delta_compression_estimee` : dispersion des pics de frappes (D05) après moins avant.

### 6.4 Limites

- Un traitement **multibande** produit une courbe moyenne qui ne correspond à aucun réglage unique.
- Un traitement **parallèle** s'estime comme une compression plus faible.
- Un limiteur ou un clipper se confond avec un compresseur à fort rapport. L'indice de coupe des crêtes (écrêtage du mixé, L06) aide à les distinguer.

## 7. Étape 4 : expansion, couleur, transitoire

### 7.1 Expansion / gate

- Comparer le plancher de bruit (L05) du brut et du mixé : `delta_plancher_db`, **après compensation du gain moyen de 5.2** (sans cela un simple changement de niveau se lit comme un gate ; corrigé le 17/10/2026).
- Comparer le niveau des trames bleed : atténuation de la diaphonie et du bruit entre les frappes.
- Repérer le **coupe-queue** : chute brutale de niveau après chaque frappe (gate), à partir de la décroissance H06 du mixé comparée à celle du brut.

### 7.2 Couleur ajoutée

La part du mixé qui n'est **pas** expliquée par un filtre linéaire du brut mesure la couleur : `1 − γ²(f)` par bande, pondérée par le niveau. On l'appelle `couleur_non_lineaire_db`. Elle regroupe saturation, harmoniques, bruit de fond ajouté et reverb. À interpréter avec la dynamique : une forte non-linéarité sans compression suggère de la saturation.

### 7.3 Transitoire

`delta_ratio_attaque_sustain_db` : H07 du mixé moins H07 du brut. Une valeur positive indique un renforcement de l'attaque ; négative, une attaque adoucie.

### 7.4 Stéréo (overheads et room)

`delta_correlation_lr`, `delta_largeur_ms_db` : modification de la largeur. Pour les overheads, vérifier aussi que le retard G/D est inchangé.

## 8. Sortie : une ligne par paire

Le fichier `paires.csv` (et un JSON complet par paire) contient, en suivant la convention du document 01 :

| Groupe | Colonnes |
|---|---|
| Identité | `paire_id`, `kit_id`, `role`, `style`, `mixeur`, `source`, `licence`, `analysisVersion` |
| Alignement | `decalage_ech`, `polarite_inversee`, `coherence_med`, `valide` ; pour un bus : un décalage et une polarité **par micro**, `coherence_multiple_med` |
| Égalisation | `eq_gain_moyen_db`, `eq_31bandes` (JSON), `eq_coherence_31` (JSON) ; pour un bus : `gain_db` par micro et par bande |
| Dynamique | `dyn_seuil_db`, `dyn_rapport`, `dyn_gr_p50`, `dyn_gr_p95`, `dyn_plafond_gr_db`, `dyn_attaque_ms`, `dyn_release_ms` |
| Expansion | `exp_delta_plancher_db`, `exp_range_db` |
| Couleur | `couleur_non_lineaire_db` |
| Transitoire | `delta_ratio_attaque_sustain_db` |
| Stéréo | `delta_correlation_lr`, `delta_largeur_ms_db` |
| Indices | `delta_crest_actif_db`, `delta_plage_dyn_db` |

Les valeurs peu fiables sont écrites `null` et jamais interpolées.

## 9. Agrégation : de beaucoup de paires à une cible

1. **Filtrer** : ne garder que les paires `valide = true`.
2. **Grouper** par `role × style`.
3. **Un mixeur, un vote.** Si un mixeur fournit 8 paires et un autre 2, pondérer pour que chaque mixeur pèse pareil (moyenne des médianes par mixeur).
4. **Résumer** chaque colonne : médiane, intervalle interquartile (P25-P75), nombre de paires, nombre de mixeurs.
5. **Écarter les aberrants** par la déviation absolue médiane (MAD) : une valeur au-delà de 3 MAD est mise de côté et listée.
6. **Seuil de confiance** : une cible n'est publiée que si elle repose sur **au moins 8 paires et 3 mixeurs** différents (hypothèse de départ). En dessous, elle reste marquée « provisoire ».
7. **Séparer réglage et validation** : les cibles se calculent sur le jeu de réglage ; le jeu de validation (document 02) sert à vérifier qu'elles généralisent.

Exemple de lecture (valeurs fictives) : « Kick / Rock, bande 60-120 Hz : médiane +3,2 dB, P25-P75 de +1,5 à +4,6, 14 paires, 5 mixeurs ». Une dispersion large dit que la correction est **peu déterminée** : le preset devra être prudent sur cette bande.

## 10. Procédure pas à pas

**État de la procédure (17/10/2026)** : les étapes 2 et 3 sont **implémentées** (`ZeroEffortDrumTests.exe --pair`, `--bus` ; `Source/Drum/DrumPairAnalysis.h`) ainsi que les indices simples de 6.3 et 7.1 ; l'estimation de la compression (6.1), la stéréo (7.4), l'écriture de `paires.csv` (8) et l'agrégation (9) restent à faire.

1. Rassembler les paires, remplir `meta.json`, vérifier les licences.
2. Aligner en temps, polarité et durée (section 4) ; calculer la cohérence ; écarter ce qui est invalide.
3. Estimer l'égalisation équivalente (section 5).
4. Estimer la dynamique équivalente (section 6).
5. Mesurer expansion, couleur, transitoire, stéréo (section 7).
6. Écrire la ligne de la paire (section 8).
7. Agréger par `role × style` (section 9).
8. Livrer les cibles au document 04, avec leur dispersion et leur nombre de paires.

## 11. Validation de la méthode

Avant de faire confiance aux résultats sur de vraies paires, vérifier que la méthode retrouve ce qu'on y met. Boucle fermée : prendre une prise brute, lui appliquer un traitement **connu**, puis lancer la méthode et comparer.

| Test | Traitement appliqué | Résultat attendu (tolérance de départ) |
|---|---|---|
| M01 | EQ : +4 dB à 100 Hz (cloche, Q 1), −3 dB à 3 kHz | courbe retrouvée à ± 1 dB là où la cohérence est ≥ 0,8 |
| M02 | Compresseur : seuil −24 dB, rapport 4:1, attaque 10 ms, release 100 ms | seuil ± 3 dB, rapport ± 1, attaque et release dans un facteur 2 |
| M03 | Gate : seuil −45 dB, range 12 dB | `exp_range_db` ± 3 dB |
| M04 | Saturation douce | `couleur_non_lineaire_db` en hausse, courbe d'EQ plate |
| M05 | Retard de 0,5 ms non corrigé | la détection d'alignement le retrouve ; la paire n'est pas validée sans correction |
| M06 | Reverb ajoutée à 30 % | cohérence en baisse, paire éventuellement écartée |
| M07 | Compression parallèle (mix 50 %) | rapport estimé nettement plus faible que le rapport réel (limite connue, à documenter) |

Ces tests M sont à ajouter au programme de test (document 02) comme une famille `T-M`.

**État (17/10/2026)** : `tests/PairTests.cpp` couvre sur signaux synthétiques : M01 (variante : cloche +6 dB à 3 kHz et −3 dB de gain, courbe retrouvée à 0,12 dB), M04 (saturation : couleur non linéaire en hausse de plus de 6 dB, crest en baisse), M05 (retard au échantillon près, polarité inversée, début tronqué, décalage de 12 s), le refus de deux prises différentes, des prises de moins de 20 s, la dérive de niveau, et le bus à deux micros (filtres retrouvés à 0,12 dB). Restent : M02 et M03 (dynamique équivalente, section 6), M06 (réverbe), M07 (compression parallèle).

## 12. Pièges fréquents

- **Comparer à des volumes différents** donne l'impression d'un boost de timbre. Toujours retirer le gain moyen avant de lire une courbe.
- **Un mixé qui contient des samples** de remplacement n'a plus de rapport linéaire avec le brut : cohérence basse, paire à écarter. **Mais** si le sample est fourni comme entrée supplémentaire d'un bus, il fait partie de la somme linéaire : le bus Snare du kit 1 passe de 0,77 à 0,82 de cohérence multiple en ajoutant `SnareSample`.
- **Un bus mesuré avec un seul micro** donne une cohérence trompeusement basse (0,34 à 0,68 sur le kit 1 contre 0,90 avec les trois micros du Kick Bus) : fournir tous les micros du bus (3.1 bis).
- **Les exports mixés ne commencent pas au même instant que les bruts** (12,25 s de décalage sur le corpus réel) : chercher le décalage sur tout le fichier, pas seulement autour de zéro.
- **Le master et le bus** contaminent la lecture si le stem est pris trop tard dans la chaîne.
- **Les styles se mélangent** : une prise « Metal » mixée en « Naturel » est une paire mal étiquetée.
- **Une période courte** (< 20 s) ou trop peu de frappes donne des spectres moyens instables.
- **Le mixeur n'est pas la référence absolue** : si tous les mixeurs d'un corpus partagent une mode (cymbales brillantes), la cible l'hérite.

## 13. Questions ouvertes

- **Q1** : seuil de cohérence (0,8) et nombre minimal de paires (8) : à calibrer sur les premières données. Premiers chiffres réels (kit 1, 217 s) : cohérence multiple médiane 0,90 (Kick Bus, 3 micros) et 0,82 (Snare Bus, 3 entrées) ; cohérence à une entrée 0,34 à 0,68. Le seuil de 0,8 sépare ces cas sans marge : à recalibrer sur plusieurs mixeurs. Remarque : une compression ou un gate **varient dans le temps** et font baisser la cohérence même sans bruit ; le seuil d'acceptation doit donc se lire avec `couleur_non_lineaire_db`.
- **Q2** : faut-il exiger le stem *avant* le bus ou accepter une piste mixée avec insert de bus (à documenter par paire) ? Réponse partielle : le **bus d'instrument** est accepté (3.1 bis, régression multi-entrées) ; le bus de batterie complet et le master restent exclus.
- **Q3** : comment traiter les paires avec samples de remplacement : exclusion totale, ou analyse séparée « kit hybride » ? Réponse partielle : si la piste du sample est disponible, l'ajouter comme entrée du bus (3.1 bis) ; sinon exclure.
- **Q4** : méthode d'estimation des temps d'attaque et de release : suffisamment fiable pour entrer dans les presets, ou information seulement ?
- **Q5** : stockage des paires brutes (poids, licence) : conserve-t-on le WAV ou seulement les mesures ?
