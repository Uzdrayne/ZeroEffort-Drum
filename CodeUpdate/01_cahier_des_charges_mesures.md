# Cahier des charges : mesures par piste

**Document 1 sur 4** | Version du document : 0.1 | Concerne : `DrumAnalysis`, l'export JSON/CSV et la future base de données

Documents liés (à écrire) : 02 programme de test, 03 comparaison brut / mixé, 04 calcul des presets.

---

## 1. Objet

Définir **tout ce qu'on mesure sur une piste de batterie isolée** (kick, caisse claire, toms, hi-hat, overheads, room, bus), comment on le calcule, et **à quelle décision chaque mesure sert**. Ce document est la référence pour :

- enrichir `DrumAnalysis` (analyse en temps réel dans le plug-in) ;
- produire la base de données qui sert à fixer les cibles par instrument et par style ;
- rejouer les profils (`KickProfile`, `SnareProfile`, `TomProfile`…) sans DAW.

**Hors périmètre** : la comparaison brut / mixé (doc 03), le calcul des presets (doc 04), le programme de test (doc 02).

## 2. Principes

1. **Une mesure sans décision n'entre pas au catalogue.** Chaque ligne indique la décision qu'elle alimente. Les autres vont en section 11 (« à explorer »).
2. **Actif plutôt que global.** Les silences entre les frappes faussent le RMS et le crest. Toute mesure de niveau existe en version « fenêtres actives ».
3. **Forme et niveau séparés.** Pour comparer deux pistes, on compare des *formes* (spectre normalisé), pas des volumes. Le niveau absolu est stocké à part.
4. **Paramètres de calcul figés et versionnés.** Si une constante change (taille de FFT, durée de fenêtre, période réfractaire…), on incrémente `analysisVersion`. Deux lignes de base avec des versions différentes ne se comparent pas.
5. **On stocke les mesures, jamais l'audio** (RGPD et poids). Les WAV restent sur la machine du développeur pour relancer l'analyse.
6. **L'analyse ne tourne pas dans `processBlock` de façon bloquante** : pas d'allocation, pas de verrou. Le calcul lourd (FFT, résonances) se fait dans le thread d'analyse, comme aujourd'hui.

## 3. Glossaire

| Terme | Définition |
|---|---|
| Frappe (hit) | Attaque détectée par le détecteur de frappes (section 5.3). |
| Onset | Trames dont le centre est 0 à 70 ms après une frappe. |
| Tail | Trames 70 à 230 ms après une frappe (résonance, extinction). |
| Bleed | Trames sans frappe récente : bruit de fond et fuites des autres instruments. |
| Fenêtre | Bloc de 10 ms servant aux histogrammes de niveau. |
| Trame | Fenêtre FFT (4096 points jusqu'à 48 kHz, 8192 au-dessus), avancée de 10 ms. |
| Trame longue | Trame de 400 ms (avance 100 ms) utilisée pour les mesures continues des overheads et de la room. |
| Fenêtre active | Fenêtre de 10 ms dont le pic dépasse un seuil relatif (voir 5.1, à calibrer). |
| Crest | Pic moins RMS, en dB. |
| P_n | Percentile n de la distribution des pics de fenêtre (résolution 1 dB). |
| Cible | Valeur ou courbe de référence par rôle et par style, issue de la base. |

## 4. Pré-traitement commun

| Étape | Règle | État |
|---|---|---|
| Entrée | Une piste = un canal analysé. Les mesures stéréo (section 5.7) demandent les deux canaux. | `push()` est mono aujourd'hui |
| Valeurs invalides | Échantillon non fini ignoré, `invalid = true`. | Existant |
| Écrêtage | Compté si `\|x\| >= 0,999`. | Existant |
| Normalisation | Pour les mesures de **forme** spectrale : spectre divisé par le nombre de trames, puis exprimé relativement à son niveau moyen. | À faire |
| Rééchantillonnage | Aucun. La fréquence d'échantillonnage est stockée avec la ligne. | Existant (`rate`) |

## 5. Catalogue des mesures

Colonnes : **Prio** (P1 = indispensable pour le premier jalon, P2 = utile ensuite, P3 = plus tard) ; **État** (Existant, Partiel, À faire).
Convention de nommage des colonnes de la base : `famille_mesure[_stat]`, avec `_mean`, `_med`, `_p10`, `_p90`, `_sd`.

### 5.1 Niveau (`lvl_`)

| ID | Mesure | Calcul | Décision servie | Prio | État |
|---|---|---|---|---|---|
| L01 | `peak_dbfs` | max des valeurs absolues, en dB | Refus si trop faible (< 0,001) ; marge de sortie | P1 | Existant (`peakDb`) |
| L02 | `rms_global_dbfs` | racine de (énergie / échantillons) | Référence ; crest global | P1 | Existant (`rmsDb`) |
| L03 | `rms_actif_dbfs` | même calcul, sur les fenêtres actives seulement | Crest stable, seuils de compression | P1 | **Fait (J2)** : `DrumAnalysis::activeRmsDb()` ; NaN / `null` sans fenêtre active |
| L04 | `env_p10 … env_p99` | percentiles de la distribution des pics de fenêtre | Seuils du compresseur et de l'expander | P1 | Existant (`percentile`, `percentileOf`) |
| L05 | `noise_floor_dbfs` | P10 de la distribution des pics de fenêtre | Détection du contraste exploitable (expander) | P1 | **Fait (J2)** : `noiseFloorDb()` |
| L06 | `clipped_ratio` | échantillons écrêtés / échantillons | Refus si > 1 % | P1 | Existant (`clipped`) |
| L07 | `true_peak_dbtp` | pic après sur-échantillonnage ×4 | Marge de sortie | P2 | À faire |
| L08 | `lufs_integre`, `lufs_short_max` | pondération K, norme BS.1770 | Normalisation des spectres, comparaison entre pistes | P2 | À faire |
| L09 | `dc_offset` | moyenne des échantillons | Alerte | P3 | À faire |

**Règle de la fenêtre active (décision 01-Q1 du document 05)** : fenêtre de 10 ms dont le pic est supérieur à `max(P95 − 55 dB, plancher de bruit + 12 dB)`. **Calibration du 17/10/2026** : le seuil relatif passe de 40 à **55 dB** (`MeasureConstants::activeBelowP95Db`, `analysisVersion` 5) ; le décalage de **12 dB** au-dessus du plancher a été étudié et conservé (le test A09 du doc 02 impose ≤ 1 dB : 0,71 dB à 12 dB, 1,19 dB à 6 dB ; voir le suivi). Cas limite ajouté en J2 : le seuil ne dépasse jamais le P95 (un signal sans dynamique garde ses fenêtres les plus fortes) et les fenêtres de silence numérique (≤ −100 dBFS) ne sont jamais actives. Constat synthétique : le seuil relatif au P95 bouge un peu avec la densité du jeu (écart de crest actif ≈ 1,2 dB entre un kick isolé et un kick dense, contre 4,8 dB pour le crest global). Calibration : 28 prises brutes de 3 kits (réglage : kits 1 et 3, validation : kit 2), écart de crest actif sur ± 10 dB de seuil : à 40 dB, 15 prises sur 28 sous 0,5 dB (écart max 3,0 dB) ; à 55 dB, 25 sur 28 sous 0,5 dB et 28 sur 28 sous 1,0 dB (écart max 0,9 dB). Reproductible avec `scripts/CalibrateActiveWindow.py`.

### 5.2 Dynamique (`dyn_`)

| ID | Mesure | Calcul | Décision servie | Prio | État |
|---|---|---|---|---|---|
| D01 | `crest_global_db` | L01 − L02 | Garde-fou « signal trop plat » | P1 | Existant (`crestDb`) |
| D02 | `crest_actif_db` | L01 − L03 | Active ou non le compresseur (aujourd'hui sur le crest global) | P1 | **Fait (J2)** : `activeCrestDb()` ; lu par `KickProfile` pour l'activation du compresseur derrière `ProposalPolicy::compressorUsesActiveCrest` (J3, **activé par défaut depuis le 17/10/2026**) |
| D03 | `plage_dyn_db` | P95 − P10 | Active l'expander (contraste ≥ 12 dB aujourd'hui) | P1 | Partiel |
| D04 | `crest_frappe_` `mean, sd` | pic / RMS sur [−5 ms, +100 ms] autour de chaque frappe | Mesure de la compression déjà présente | P2 | À faire |
| D05 | `compression_estimee` | dispersion des pics de frappes (écart-type des pics en dB) : faible = déjà comprimé | Réduit la dose de compression automatique | P2 | À faire |
| D06 | `plr_db`, `lra` | pic − LUFS ; plage de loudness | Comparaison brut / mixé | P3 | À faire |

### 5.3 Frappes et enveloppe (`hit_`)

Détecteur actuel (à conserver tel quel pour le kick) : bande 25-250 Hz (différence de deux filtres à un pôle à 250 Hz et 25 Hz), fenêtres de 10 ms, une frappe si niveau grave > 0,0003 et > 1,8 × fenêtre précédente et pic > 1,3 × pic précédent, période réfractaire de 120 ms.

| ID | Mesure | Calcul | Décision servie | Prio | État |
|---|---|---|---|---|---|
| H01 | `hits` | nombre de frappes détectées | Refus si < 3 | P1 | Existant |
| H02 | `hits_par_s` | `hits` / durée | Qualité de l'échantillon, densité du jeu | P1 | **Fait (J2)** : hitsPerSecond() (détecteur grave, voir problème 4) |
| H03 | `force_frappe_` `mean, sd, p10, p90` | pic de chaque frappe, en dB | Régularité du jeu ; ghost notes | P2 | À faire |
| H04 | `ghost_ratio` | frappes < P50 − 12 dB / total | Réglage de l'expander (ne pas couper les ghost notes) | P2 | À faire |
| H05 | `attaque_10_90_ms` | temps de montée de l'enveloppe de la frappe | Réglage de l'attaque du compresseur | P2 | À faire |
| H06 | `decay_t20_ms`, `decay_t60_ms` | temps de chute de l'enveloppe | Release de l'expander et du compresseur | P1 | **Fait (J2)** : `decayT20Ms()` mesuré (chute de 20 dB de l'enveloppe des pics de 10 ms depuis la détection, médiane, résolution 10 ms, il faut 3 frappes), `decayT60Ms()` extrapolé (× 3) |
| H07 | `ratio_attaque_sustain_db` | énergie 0-20 ms moins énergie 50-300 ms | Attack/Sustain, dose de transitoire | P2 | À faire |
| H08 | Détecteur propre à chaque rôle | bande et période réfractaire adaptées (caisse claire : 150-5000 Hz ; toms : 60-400 Hz) | Fiabilité de `hits` hors kick | P1 | À faire |
| H09 | Détecteur large bande (flux spectral au-dessus de 1 kHz) pour overheads et room | Sert uniquement à segmenter les événements (décroissance, densité) ; son nombre de frappes n'est pas exploité | P2 | À faire |

### 5.4 Spectre global (`sp_`)

Source : spectre de puissance cumulé (`spectrum` = onset + tail + bleed), FFT 4096 ou 8192, fenêtre de Hann, avance 10 ms.

| ID | Mesure | Calcul | Décision servie | Prio | État |
|---|---|---|---|---|---|
| S01 | `sp_third_oct_XXX` | énergie par bande de 1/3 d'octave (31 colonnes, 20 Hz à 20 kHz), normalisée par le nombre de trames | Comparaison à la cible, EQ automatique | P1 | **Fait (J2)** : `thirdOctaveDb(i)`, dB de la puissance FFT non étalonnée par trame ; `null` sous la résolution de la FFT (bande 20 Hz à 48 kHz) et au-dessus de 0,45 × fréquence. **Attention (T4, A11)** : la valeur absolue dépend de la taille de FFT (4096 jusqu'à 48 kHz, 8192 au-dessus) : à 1 kHz, 96 kHz lit 6 dB de plus que 48 kHz pour le même signal. Seules les mesures relatives (S02, S04) se comparent entre fréquences d'échantillonnage |
| S02 | `band_` `sub, grave, basMedium, medium, presence, brillance, air` | énergie en dB relative au total 20 Hz-20 kHz (bandes : 20-60, 60-200, 200-500, 500-2k, 2k-5k, 5k-10k, 10k-20k) | Lecture rapide et règles de texte du Bilan | P1 | **Fait (J2)** : `bandRelativeDb()` ; les 7 bandes sont une partition de 20 Hz à min(20 kHz, 0,45 × fréquence) |
| S03 | `centroide_hz`, `rolloff85_hz`, `rolloff95_hz` | barycentre ; fréquence contenant 85 % et 95 % de l'énergie | Teinte (sombre / brillant), choix du low-pass | P2 | À faire |
| S04 | `pente_db_oct` | pente de la régression du spectre en dB / log fréquence, de 100 Hz à 10 kHz | Écart de timbre global | P2 | **Fait (T4)** : `spectralSlopeDbPerOctave()` (colonne `sp_slope_db_oct`), densité de puissance par tiers d'octave (bruit rose −3 dB/oct, bruit blanc 0) ; `null` si moins de 8 bandes |
| S05 | `platitude` (flatness) | moyenne géométrique / moyenne arithmétique | Distingue tonal et bruité | P3 | À faire |
| S06 | `coupure_basse_hz`, `coupure_haute_hz` | fréquence où le niveau passe sous −60 dB du maximum | Prise déjà filtrée ; réglage des filtres | P2 | À faire |

### 5.5 Spectre par phase (`ph_`)

| ID | Mesure | Calcul | Décision servie | Prio | État |
|---|---|---|---|---|---|
| P01 | `onset_spectrum`, `tail_spectrum`, `bleed_spectrum` | spectres cumulés par phase, plus le nombre de trames de chacune | Base de toutes les mesures ci-dessous ; rejeu des profils | P1 | Existant |
| P02 | `ecart_attaque_corps_db` | énergie onset dans la bande de clic moins énergie onset dans la bande de corps | « Manque de claque » ou « trop mou » | P1 | **Fait (J2)** : `attackBodyGapDb(rôle)` pour Kick, Snare et Toms (bandes de la section 5.5) |
| P03 | `bleed_aigu_vs_onset_grave` | énergie 6-12 kHz du bleed / énergie 35-150 Hz de l'onset | Réduction de l'aigu du kick (`airGain`) | P1 | Existant (dans `KickProfile`) ; exposé en J3 : `bleedTrebleRatio()` (colonne `ph_bleed_treble_ratio`, le cut démarre au-dessus de 1). **Exportée pour le Kick seulement** (17/10/2026) : sur les autres rôles l'onset grave est minuscule et le rapport explose (693 sur un Hi-hat, 3 millions sur un autre) ; la colonne est alors `null` |
| P04 | `contraste_tail_bleed_db` | tail − bleed, par bande | Fiabilité des corrections de résonance | P2 | À faire |

Bandes de référence : **kick** corps 35-150 Hz, clic 2-5 kHz ; **caisse claire** corps 120-250 Hz, claque 3-6 kHz ; **toms** corps 60-250 Hz, attaque 2-5 kHz. À confirmer par la base.

### 5.6 Tonal et résonances (`ton_`)

| ID | Mesure | Calcul | Décision servie | Prio | État |
|---|---|---|---|---|---|
| T01 | `corps_hz` | pic spectral de l'onset dans la plage du rôle (kick 35-140 Hz, toms 45-200 Hz, caisse claire 120-250 Hz) | Fréquence du potard Corps/Poids | P1 | Kick et toms existants, caisse claire à faire |
| T02 | `corps_hz_affine` | idem avec interpolation parabolique autour du pic (la résolution de bin est d'environ 11,7 Hz à 48 kHz) | Précision de la fréquence | P2 | À faire |
| T03 | `resonance_hz`, `resonance_db` | recherche en 1/6 d'octave : tail > 1,5 % du corps, > 12 % de l'onset, > 2× le bleed, > 2× les voisines | Creux de Carton / Box ; bague de la caisse claire | P1 | Kick existant (`kickTemporalResonance`), autres à faire |
| T04 | `resonances_liste` | toutes les résonances trouvées : fréquence, amplitude au-dessus du spectre lissé, largeur (Q), durée (JSON) | Plusieurs creux sur une même piste | P3 | À faire |
| T05 | `glissement_pitch_hz` | écart entre la fréquence du corps à 0-10 ms et à 40-80 ms | Réglage du renfort accordé du kick | P3 | À faire |
| T06 | `desaccord_toms_hz` | écart entre les corps de plusieurs toms du même kit | Information utilisateur | P3 | À faire |

### 5.7 Stéréo (`st_`) — overheads et room

| ID | Mesure | Calcul | Décision servie | Prio | État |
|---|---|---|---|---|---|
| ST1 | `correlation_lr` | corrélation entre G et D | Indice de largeur, compatibilité mono | P2 | À faire (entrée stéréo requise) |
| ST2 | `largeur_ms_db` | niveau latéral moins niveau central | Réglage de la room | P2 | À faire |
| ST3 | `balance_lr_db` | niveau G moins niveau D | Alerte d'équilibre | P3 | À faire |
| ST4 | `compat_mono_db` | perte de niveau en sommant en mono | Alerte phase | P2 | À faire |

### 5.8 Relations entre pistes (`kit_`)

Ces mesures viennent du module Kit / Phase (`kit::KitSession`) et se rattachent à la ligne de chaque piste.

| ID | Mesure | Calcul | Décision servie | Prio | État |
|---|---|---|---|---|---|
| K01 | `retard_vs_oh_ms`, `correlation_max` | corrélation croisée (enveloppe blanchie), première arrivée | Retard proposé | P1 | Existant |
| K02 | `polarite_relative` | signe de la corrélation | Inversion de polarité proposée | P1 | Existant |
| K03 | `coherence_phase_avant`, `coherence_phase_apres` | score 0-100 | Résultat affiché à l'utilisateur | P1 | Existant |
| K04 | `diaphonie_db` | niveau de la piste pendant les frappes des *autres* instruments, moins son niveau pendant ses propres frappes | Réglage de l'expander, qualité de la prise | P2 | **Fait (17/10/2026)** : `KitSession::crosstalkDb()`, champ `Recommendation::crosstalkDb` (NaN si indisponible), commande `--kit`. Une frappe appartient à l'instrument dont le micro est le plus fort par rapport à sa propre frappe la plus forte ; 4 frappes propres et 4 étrangères au moins. Inclut la queue du propre instrument (plancher vers −20 dB sur le kit synthétique). Valeurs réelles : Kick In −16 dB, Snare −21 dB (kit 1) ; kit de référence −19 / −24 dB |
| K05 | `niveau_relatif_oh_db` | niveau actif de la piste moins celui des overheads | Équilibre du kit (information) | P3 | À faire |
| K06 | `recouvrement_spectral` | part de l'énergie 60-300 Hz commune entre kick et caisse claire | Conseil d'EQ complémentaire | P3 | À faire |

### 5.9 Qualité et défauts (`qa_`)

| ID | Mesure | Calcul | Décision servie | Prio | État |
|---|---|---|---|---|---|
| Q01 | `invalid` | présence de valeurs non finies | Refus | P1 | Existant |
| Q02 | `ronflement_db` | niveau à 50 / 60 Hz et harmoniques pendant le bleed | Alerte, high-pass | P3 | À faire |
| Q03 | `souffle_hf_db` | niveau > 10 kHz pendant le bleed | Réglage du nettoyage aigu | P3 | À faire |
| Q04 | `gate_detecte` | silences numériques exacts entre les frappes | Évite de gater deux fois | P3 | À faire |
| Q05 | `distorsion_estimee` | taux d'harmoniques d'un signal tonal | Alerte | P3 | À faire |

### 5.10 Overheads et room (`oh_`, `rm_`)

**Principe.** Sur ces pistes, on ne segmente pas par frappe. Le signal est un mélange continu (cymbales, fuites de kick et de caisse claire, ambiance). L'analyse est **continue** : en plus des trames de 10 ms, on utilise des **trames longues de 400 ms (avance 100 ms)** et on retient des **médianes et percentiles par bande** (P10, P50, P90) plutôt que des moyennes, car un crash isolé domine une moyenne. Le détecteur de frappes basé sur le grave n'est **pas** utilisé pour les décisions de ces rôles : sur un overhead, il compte surtout les fuites du kick.

| ID | Mesure | Calcul | Décision servie | Rôle | Prio | État |
|---|---|---|---|---|---|---|
| O01 | `sp_med_`, `sp_p90_` (par tiers d'octave) | spectre des trames longues, médiane et P90 par bande | Équilibre tonal, EQ à 4 bandes (Grave / Boue / Présence / Brillance) | OH, room | P1 | À faire |
| O02 | `cymbales_vs_corps_db` | énergie médiane 6-16 kHz moins énergie médiane 200-2000 Hz | « Cymbales trop présentes » ou « trop en retrait » | OH | P1 | À faire |
| O03 | `durete_db` | excès local de 2,5-5 kHz par rapport au spectre lissé voisin (médiane et P90) | Creux de présence si les cymbales sont agressives | OH | P1 | À faire |
| O04 | `eclat_aigu_db` | énergie 8-16 kHz relative au total | Shelf aigu, low-pass | OH, room | P2 | À faire |
| O05 | `fuite_grave_db` | énergie médiane 35-150 Hz relative au total | Fréquence du high-pass ; poids des fuites de kick (lien avec Kit / Phase) | OH, room | P1 | À faire |
| O06 | `coupure_utile_hz` | fréquence sous laquelle le spectre médian tombe de 10 dB sous son niveau moyen 200-500 Hz | Réglage du high-pass | OH, room | P1 | À faire |
| O07 | `boue_db` | énergie médiane 200-500 Hz relative au total | Bande « Boue » de l'EQ | OH, room | P1 | À faire |
| O08 | `variabilite_dyn_db` | P90 − P10 du niveau des trames longues | Dose de compression douce / glue | OH, room | P2 | À faire |
| O09 | `crest_actif_db` | voir D02 ; la cible est propre au rôle (un overhead est naturellement très crêté) | Garde-fou, pas un déclencheur de compresseur | OH, room | P1 | À faire |
| R01 | `decay_t60_ms` | détecteur large bande (H09), puis chute de l'enveloppe après les événements les plus forts, extrapolée à −60 dB | Réglage d'ambiance, release | room | P2 | À faire |
| R02 | `densite_queue_db` | énergie 70-500 ms après un événement moins énergie 0-20 ms, médiane sur les événements | Mesure du caractère « room », dosage | room | P2 | À faire |
| R03 | `niveau_vs_oh_db` | niveau actif de la room moins celui des overheads | Dosage de la room dans le kit | room | P2 | À faire (multi-pistes) |
| R04 | `compat_room_oh` | score du module Kit (corrélation retardée room / overheads) | Alerte de polarité de la room | room | P1 | Existant |
| ST1 à ST4 | Mesures stéréo (section 5.7) | | Largeur, compatibilité mono | OH, room | P1 | À faire (entrée stéréo) |

Les overheads reçoivent aussi les corrections de Kit / Phase (K01 à K03) : ce sont eux la référence de temps.

## 6. Mesures par rôle

`●` indispensable, `○` utile, `–` sans objet.

| Famille | Kick | Snare | Toms | Hi-hat | Overheads | Room | Bus |
|---|---|---|---|---|---|---|---|
| Niveau (5.1) | ● | ● | ● | ● | ● | ● | ● |
| Dynamique (5.2) | ● | ● | ● | ○ | ○ | ○ | ● |
| Frappes (5.3) | ● | ● | ● | ○ | ○ (H09 seulement) | ○ (H09 seulement) | – |
| Spectre global (5.4) | ● | ● | ● | ● | ● | ● | ● |
| Spectre par phase (5.5) | ● | ● | ○ | – | – | – | – |
| Tonal (5.6) | ● | ○ | ● | – | – | – | – |
| Stéréo (5.7) | – | – | – | – | ● | ● | ○ |
| Inter-pistes (5.8) | ● | ● | ○ | – | ● | ○ | – |
| Overheads / room (5.10) | – | – | – | – | ● | ● | ○ |
| Qualité (5.9) | ○ | ○ | ○ | ○ | ○ | ○ | ○ |

## 7. Format de sortie

Une analyse = un fichier JSON avec cinq blocs : `meta`, `input` (réglages de départ, par identifiant de paramètre : ajout du 16/10/2026, nécessaire au rejeu de `propose()`), `analysis` (valeurs brutes pour le rejeu), `measures` (valeurs lisibles), `proposal` (réglages produits, par **identifiant de paramètre** et non par indice). Le code de sérialisation est dans `Source/Drum/DrumAnalysisSnapshot.h` (**implémenté en J1**). Les spectres sont écrits en base64 (doubles little-endian, un rejeu est identique bit à bit) ; un snapshot reste sous 300 Ko, y compris à 96 kHz.

**Export développeur** : désactivé par défaut. Il faut compiler avec `-DZED_ANALYSIS_EXPORT=ON` (option CMake, jamais dans un build de production) **et** définir la variable d'environnement `ZED_ANALYSIS_EXPORT_DIR` ; un fichier `snapshot_<date>_<rôle>.json` est écrit à chaque analyse Auto (thread de l'interface, pas `processBlock`). `ZED_ANALYSIS_EXPORT_LICENCE` renseigne `meta.licence` (défaut `private-dev`).

Champs obligatoires de `meta` : `schema`, `analysisVersion`, `profileVersion`, `role`, `style`, `intensity`, `source`, `licence`. Champs facultatifs ajoutés le 17/10/2026 (schéma inchangé, anciens fichiers toujours lisibles) : `position` (position du micro en clair : « Kick In », « Kick Out », « Snare Top », « Snare Bottom », « Overheads L », « Overheads R », « Overheads (stereo) », etc. ; identique au paramètre `kitMic` de `input.settings`, sans sens pour les autres instruments) et `channels` (1 ou 2, canaux de l'audio source ; absent si inconnu).
Un export CSV (une ligne par piste) est généré à partir de `measures` avec la convention de la section 5. Les listes de longueur variable (`resonances_liste`) restent dans le JSON.

**Constantes qui déterminent `analysisVersion`** : fenêtre de 10 ms, avance de 10 ms, FFT 4096 / 8192 (seuil à 48 kHz), onset 0-70 ms, tail 70-230 ms, période réfractaire 120 ms, bande grave du détecteur 25-250 Hz, table des temps d'attaque et de relâchement des six enveloppes, règle de la fenêtre active.

## 8. Problèmes connus à corriger dans l'analyse

À confirmer par un test avant de corriger.

| # | Problème | Effet probable | Piste de correction |
|---|---|---|---|
| 1 | Les trames dont le centre précède la frappe (âge négatif) tombent dans `bleedSpectrum` alors qu'elles contiennent l'attaque (environ 4 trames par frappe) | Aigu du bleed surestimé, atténuation inutile de l'aigu du kick | Exclure ces trames, ou les classer après coup |
| 2 | Le crest utilise le RMS de toute la durée | Un jeu peu dense donne un crest plus élevé et allume le compresseur | Utiliser `crest_actif` (D02) |
| 3 | Période réfractaire fixe de 120 ms et seuil de 1,8 × la fenêtre précédente | Doubles pédales rapides (metal) sous-comptées | Période adaptative, test sur des doubles à 180 bpm |
| 4 | Détecteur réglé pour le kick, fenêtres onset / tail identiques pour tous les rôles | Mesures peu fiables sur caisse claire, toms, cymbales | Détecteur et fenêtres par rôle (H08) |
| 5 | Résolution de 11,7 Hz sur le corps du kick | Fréquence en escalier | Interpolation parabolique (T02) |
| 6 | Sur les overheads et la room, le détecteur grave compte les fuites du kick ; onset / tail / bleed y sont sans signification | Chiffres de frappes et spectres par phase trompeurs pour ces rôles | Ne pas les utiliser pour ces rôles ; analyse continue (5.10) |

**État (J3, 16/10/2026, activé le 17/10/2026)** : les problèmes 1 et 2 ont un correctif, **activé par défaut le 17 octobre 2026 sur validation de l'utilisateur** (`analysisVersion` 4, `profileVersion` 2). Sur les 32 prises du corpus (Rock 80 %), aucun réglage ne change entre les deux comportements ; les anciens comportements restent accessibles par la politique (`setExcludeLeadFrames(false)`, `compressorUsesActiveCrest = false`, `--replay ... --no-active-crest`) :

- **Problème 1** : `DrumAnalysis::setExcludeLeadFrames(true)` met de côté les trames dont le centre précède la frappe de moins d'une demi-FFT (environ 4 par frappe, comptées dans `leadFrameCount()`, colonne `ph_lead_frames`) ; elles restent dans le spectre global. Constante : `MeasureConstants::excludeLeadFramesByDefault`. Synthétique : la puissance du bleed à 6-12 kHz passe de 31 % de celle de l'attaque à 0,0006 %. Sur `wav_test_file` (Kick, rock) : 741 frappes, 2 964 trames mises de côté, rapport aigu du bleed 0,0090 → 0,0012, aucun réglage ne change (le cut d'aigu ne démarre qu'au-dessus de 1).
- **Problème 2** : `ProposalPolicy::compressorUsesActiveCrest` fait lire `activeCrestDb()` par `KickProfile` pour la seule décision d'activation du compresseur (et du multibande). Constante : `ProposalDefaults::compressorUsesActiveCrest`. Le seuil de 10 dB n'est pas recalibré (hypothèse H). Synthétique : un kick en salves de 0,15 s par seconde a un crest global de 11,3 dB et actif de 3,0 dB : compresseur ON par défaut, OFF avec la politique. Sur le Kick réel du dépôt : crest 25,4 dB, actif 24,5 dB, même décision.
- **Problème 3** (J4a, 17/10/2026) : correctif **activé par défaut le 17/10/2026** (`analysisVersion` 6, validé par l'utilisateur) : `DrumAnalysis::setDoubleHitDetection(true)` (constante `MeasureConstants::doubleHitsByDefault`, `doubleHitMinimumWindows` = 6, `doubleHitRise` = 4, hypothèses H) permet à une frappe de suivre la précédente après 60 ms au lieu de 120 ms quand le pic de la fenêtre est au moins 4 fois celui de la fenêtre précédente. A08 : 48 frappes sur 48 par défaut, 24 sur 48 avec `--no-double-hits` (ancien détecteur) ; un jeu ordinaire (périodes de 0,5, 0,35 et 0,25 s) donne le même compte. `--export-wav ... --no-double-hits` redonne l'ancien comportement. Sur les 37 prises du corpus (Rock 80 %) : les frappes comptées augmentent de 1 à 16 % sur les rôles à détecteur grave (Kick +7 à +16 %, Snare +3 à +10 %, Overheads et Room jusqu'à +13 %), les Toms et un Hi-hat ne bougent pas ; **un seul réglage change** (Kick Out du kit 1 : coupe-haut −5,8 Hz, corps −3,5 Hz, renfort −8,5 Hz). 
- Les deux réglages sont enregistrés dans les snapshots (`analysis.excludeLeadFrames`, `meta.policy`) ; depuis l'activation par défaut, `--export-wav ... --no-exclude-lead-frames --no-active-crest` fabrique l'ancien comportement (les options `--exclude-lead-frames` et `--active-crest` restent acceptées), `--replay ... --active-crest|--no-active-crest` surcharge la politique enregistrée. `--export-wav ... --position <0-3>` enregistre le `kitMic` de la prise.

## 9. Contraintes techniques

- Aucune allocation ni verrou dans le chemin audio. Les tableaux de `DrumAnalysis` sont déjà de taille fixe.
- Coût FFT : une transformée toutes les 10 ms par instance, dans le thread d'analyse. Toute mesure nouvelle doit rester dans ce budget (à mesurer sur 8 instances).
- Les mesures stéréo demandent d'étendre `push()` à deux canaux ou de prévoir une seconde instance d'analyse.
- Le plug-in cible Windows x64, JUCE 8 ; l'encodage binaire des tableaux (little-endian) est valable sur cette plateforme.

## 10. Plan d'implémentation par jalons

| Jalon | Contenu | Livrable |
|---|---|---|
| J1 | Export `toSnapshot()` et `toMeasures()` à chaque analyse | Premiers JSON réels | **Fait** (16/10/2026) : `DrumAnalysisSnapshot.h`, test de boucle ; export réel à tester sur une machine avec Cubase |
| J2 | Mesures P1 manquantes : L03, L05, D02, H02, H06, S01, S02, P02 | Colonnes de base remplies sur 20 prises | **Fait** (16/10/2026) : synthétique seulement, 20 prises réelles à faire |
| J3 | Corrections 1 et 2 de la section 8, avec comparaison avant / après sur la base | Rapport de rejeu |
| J4 | Détecteurs par rôle (H08), corps et résonance de la caisse claire (T01, T03) | Profils Snare et Tom basés sur des mesures |
| J4b | Analyse continue pour overheads et room (5.10, mesures P1) et extension stéréo de `push()` | Profils Overheads et Room basés sur des mesures |
| J5 | Mesures P2 | Base enrichie |

## 11. Questions ouvertes et à explorer

- **Q1** : seuil de la fenêtre active : **40 dB calibré à 55 dB le 17/10/2026** (voir section 5.1). Reste à calibrer : le décalage de 12 dB au-dessus du plancher de bruit.
- **Q2** : garde-t-on les WAV des prises de bêta (avec consentement) ou seulement les mesures ?
- **Q3** : mesures par frappe (statistiques sur chaque frappe) ou seulement globales ? Le premier est plus riche mais le JSON grossit.
- **Q4** : fréquence d'échantillonnage de la base : faut-il normaliser les analyses faites à 96 kHz (FFT de 8192) pour les comparer à celles à 48 kHz ?
- **Q5** : longueur des trames longues (400 ms) et seuil d'événement large bande : à calibrer sur des overheads et des rooms de styles différents.
- **Q6** : la room doit-elle être analysée seule ou toujours avec les overheads (R03, R04) ?
- **À explorer** : modèle audio-langage comme second avis en développement ; détection automatique du rôle à partir du spectre ; mesures de plusieurs frappes superposées (flam, doubles).
