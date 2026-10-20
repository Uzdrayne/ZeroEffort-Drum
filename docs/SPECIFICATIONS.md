# ZeroEffort Drum - spécifications de référence

**Consolidation du 20 octobre 2026 · Révision documentaire 2**

Ce document rassemble les exigences utiles des anciens documents 01 à 08 et du
plan produit. Il décrit **ce que le système doit préserver ou permettre**, pas
une liste de fonctions déjà livrées. L'avancement, les versions, les anomalies
et les validations sont tenus uniquement dans [PROJET.md](PROJET.md).

| Document actif | Question traitée |
|---|---|
| [PROJET.md](PROJET.md) | Où en est-on ? Qu'est-ce qui reste à faire ou à valider ? |
| **SPECIFICATIONS.md** | Quelles règles, mesures et preuves faut-il respecter ? |

Les numéros historiques sont conservés pour retrouver les références dans le
code. **Mesure D01**, **test T-D/D01** et **décision 01-Q1** sont des identifiants
différents. Les exemples et formules historiques complets restent consultables
dans les [sources archivées](#sources).

## Sommaire

1. [Périmètre et règles communes](#cadre)
2. [Mesures et export](#mesures)
3. [Comparaison brut / mixé et cibles](#corpus)
4. [Calcul des presets](#presets)
5. [Kit, sous-bus et extensions](#kit)
6. [Programme de validation](#tests)
7. [Décisions et hypothèses](#decisions)
8. [Évolutions produit non engagées](#futur)
9. [Sources et règles de maintenance](#sources)

<a id="cadre"></a>
## 1. Périmètre et règles communes

### 1.1 Comment lire les exigences

- **Cadre acquis** : règle de travail ou décision déjà appliquée/documentée.
- **Cible** : capacité décrite par le cahier des charges ; son inscription ne
  signifie ni livraison, ni autorisation d'un changement sonore immédiat.
- **Proposition** : arbitrage encore à valider, à ne pas transformer en défaut
  produit sans décision.
- **H** : hypothèse numérique à calibrer, même lorsqu'elle est déjà utilisée.

Cette consolidation ne valide aucune ancienne proposition à la place de
l'utilisateur. Les décisions explicites récentes priment sur les anciens textes.
Pour interpréter une exigence non résumée ici, l'ordre historique était :
**décisions 05 > sous-bus 08 > mesures/tests/comparaison/presets 01-04 > extensions 06**.
Il ne transforme pas les lignes encore « proposées » du document 05 en décisions.
En cas de conflit restant, demander avant d'implémenter.

### 1.2 Cadre acquis

- Effet de mixage de batterie acoustique, pas mastering universel, séparation
  de sources ou remplacement automatique par samples.
- Windows x64, C++17, JUCE 8.0.6, VST3 avec ARA pour Cubase. Moteur commun
  insert/ARA/tests ; pas de Standalone, Cubase Gain ou cible prototype à recréer.
- Une analyse n'écrase pas silencieusement les réglages. Dans l'éditeur, la
  proposition attend validation ; un refus explique sa cause et ne modifie pas
  le son. Préserver Apply/Ignore/Revert et les modes explicites de Kit / Phase.
- Le choix de rôle est explicite. Un style est une intention, pas la preuve
  d'un défaut du signal. La nouvelle instance utilise Rock 75 % ; les anciens
  états gardent leurs valeurs.
- Temps réel : aucun verrou, allocation, accès fichier ou journalisation ;
  buffers bornés/préalloués, publication non bloquante. Les calculs lourds restent
  hors callback audio. Ne pas confondre cet objectif avec un worker Auto déjà livré.
- Garder les IDs de paramètres, codes du plug-in et formats existants compatibles.
  Documenter tout changement de plage normalisée et tester les automations hôte.
- Ne pas démarrer de lecture audio sans accord ni installer automatiquement dans
  un dossier système. Les WAV restent hors dépôt ; préserver licences et consentements.
- Tester les changements avec les outils existants. Une preuve synthétique ou
  un build réussi ne remplace pas l'écoute ni le cycle complet dans Cubase.

<a id="mesures"></a>
## 2. Mesures et export

### 2.1 Conventions communes

Une mesure doit servir une décision identifiée. Séparer **niveau absolu** et
**forme spectrale**, signal global et fenêtres actives. Stocker fréquence
d'échantillonnage, rôle, style, provenance et versions avec les résultats.
Une mesure indisponible vaut **NaN en calcul / `null` en JSON**, pas zéro inventé.

| Élément | Convention |
|---|---|
| Fenêtre | 10 ms pour les histogrammes ; percentiles de pics, résolution 1 dB |
| FFT | Hann, 4096 points jusqu'à 48 kHz, 8192 au-dessus ; avance 10 ms |
| Onset / tail / bleed | Centre de trame à 0-70 ms / 70-230 ms après frappe / hors frappe récente ; estimates, pas séparation de sources |
| Trames précédant une frappe | Exclues du bleed avec J3 ; conservées dans le spectre global |
| Fenêtre active | Pic au-dessus de `max(P95 - 55 dB, plancher + 12 dB)`, seuil plafonné à P95 ; silence numérique ≤ -100 dBFS exclu |
| Fréquences exploitables | Bandes exprimées en Hz, au-dessus de `0,45 × rate` indisponibles |
| Analyse stéréo cible | Deux canaux requis ; une analyse mono ou un repli L/R ne fournit pas toutes les mesures ST |
| Trames longues OH/Room | **Cible/H** : 400 ms, avance 100 ms ; comparer 200/400/800 ms avant choix définitif |
| Valeurs invalides | Échantillons non finis signalés ; écrêtage compté à `abs(x) >= 0,999` |

Le seuil actif de 55 dB a été calibré sur un petit corpus, pas universellement.
Les 12 dB au-dessus du plancher restent H. Le détecteur de base est dans
25-250 Hz ; J4a autorise 60 ms au lieu de 120 ms avec une montée de pic ×4.
Cela ne remplace pas le futur détecteur par rôle ni celui de Focus.

### 2.2 Catalogue à conserver

Les IDs ci-dessous définissent le catalogue **existant et cible**. Leur état
opérationnel est résumé dans [la synthèse d'avancement](PROJET.md#synthese-specs).
Ne pas conclure qu'une mesure est livrée parce que son ID apparaît ici.

| Famille / IDs | Définition ou résultat attendu | Décision servie |
|---|---|---|
| L01, L02 | Pic absolu, RMS global | Niveau, refus, marge de sortie |
| L03, L04, L05 | RMS actif, percentiles d'enveloppe P10 à P99, plancher P10 | Seuils et comparaison sans biais des silences |
| L06 | Ratio d'échantillons écrêtés | Refus d'une source trop saturée |
| L07, L08, L09 | True-peak ×4, LUFS intégré/short-term (BS.1770), moyenne DC | Mesures avancées de niveau et alertes |
| D01, D02 | Pic moins RMS global / actif | Dynamique globale / active |
| D03 | P95 - P10 | Plage dynamique ; ne pas confondre avec P90-P20 des gates existants |
| D04, D05, D06 | Crest par frappe (moyenne/dispersion, fenêtre -5 à +100 ms), dispersion des pics, PLR/LRA | Compression déjà présente et comparaison de dynamique |
| H01, H02 | Nombre de frappes, frappes par seconde | Validité et densité |
| H03, H04 | Force des frappes (moyenne, écart-type, P10/P90), proportion sous P50 -12 dB | Régularité et protection des ghost notes |
| H05, H06 | Montée 10-90 %, décroissance T20 et T60 | Attack/release ; T60 actuel = 3 × T20, résolution 10 ms, au moins 3 frappes |
| H07 | Énergie 0-20 ms moins énergie 50-300 ms | Rapport attaque/sustain ; distinct de l'axe Attack actuel du radar |
| H08, H09 | Détecteurs par rôle ; événements large bande/flux spectral >1 kHz | Frappes Snare/Toms et décroissance OH/Room |
| S01 | 31 tiers d'octave 20 Hz-20 kHz, normalisés par nombre de trames | Timbre et EQ |
| S02 | Énergie relative des bandes 20-60, 60-200, 200-500, 500-2k, 2k-5k, 5k-10k, 10k-20k | Lecture du spectre et radar |
| S03, S04 | Centroïde, rolloff 85/95 % ; pente de densité spectrale 100 Hz-10 kHz | Brillance, filtrage et forme globale |
| S05, S06 | Platitude géométrique/arithmétique ; coupures sous -60 dB du maximum | Tonal/bruité et filtrage déjà appliqué |
| P01 | Spectres onset/tail/bleed et comptes de trames | Analyse temporelle et rejeu |
| P02 | Écart clic/corps sur onset | Kick : 2-5k contre 35-150 Hz ; Snare : 3-6k contre 120-250 Hz ; Toms : 2-5k contre 60-250 Hz |
| P03, P04 | Bleed 6-12k / onset 35-150 Hz (Kick seulement) ; contraste tail/bleed par bande | Nettoyage et confiance des résonances |
| T01, T02 | Pic de corps selon rôle ; interpolation parabolique autour du pic | Accord de l'EQ/renfort ; pic spectral, pas fondamentale certifiée |
| T03, T04 | Résonance persistante puis liste fréquence/amplitude/Q/durée | Corrections ciblées ; ne pas retirer tout pic utile |
| T05, T06 | Glissement de corps 0-10 contre 40-80 ms ; écarts entre toms | Renfort Kick et information d'accord |
| ST1, ST2, ST3, ST4 | Corrélation L/R, niveau Side-Mid, balance L-R, perte à la sommation mono | Largeur et compatibilité mono |
| K01, K02, K03 | Retard/corrélation, polarité, cohérence avant/après | Alignement du kit |
| K04, K05, K06 | Diaphonie, niveau actif relatif aux OH, recouvrement spectral Kick/Snare 60-300 Hz | Refus, équilibre et conseils complémentaires |
| Q01 à Q05 | Non-fini ; ronflement 50/60 Hz et harmoniques ; souffle >10k ; silences de gate ; distorsion estimée | Refus et avertissements, pas diagnostics certains sans validation |
| O01 | Médiane/P90 par tiers d'octave sur trames longues | OH/Room sans domination d'un crash isolé |
| O02, O03, O04 | Rapport cymbales 6-16k / corps 200-2k ; excès local 2,5-5k ; éclat 8-16k relatif | Équilibre et dureté des cymbales |
| O05, O06, O07 | Grave 35-150 Hz relatif ; coupure utile (-10 dB vs moyenne 200-500 Hz) ; boue 200-500 Hz | HP et EQ OH/Room |
| O08, O09 | P90-P10 des trames longues ; crest actif avec cible du rôle | Compression prudente et garde-fou |
| R01, R02 | T60 après événements large bande ; queue 70-500 ms moins attaque 0-20 ms | Décroissance et caractère de la Room |
| R03, R04 | Niveau Room vs OH ; compatibilité retardée Room/OH | Dosage et polarité ; indisponibles si référence absente |

**Limites à respecter :**

- S01 actuelle est une puissance FFT non étalonnée : sa valeur absolue varie avec
  la FFT ; utiliser S02/S04 pour comparer les fréquences d'échantillonnage.
- H06 actuel n'est pas la future décroissance R01 de Room ; frappes interrompues
  par la suivante distinguées des décroissances observées.
- Corps Snare déjà estimé dans 100-200 Hz sous condition d'énergie ; la cible
  T01 historique 120-250 Hz n'est pas entièrement réalisée par cette estimation.
- Résonance Kick T03 : recherche tail suffisamment fort (>1,5 % du corps,
  >12 % de l'onset, >2× bleed et voisines). Les extensions Snare/Toms demandent
  leurs propres tests, pas une simple copie de ces seuils.
- OH/Room doivent tendre vers une analyse continue et robuste, pas des décisions
  fondées sur les frappes de Kick qui fuient dans leurs micros.

### 2.3 Contrat d'export

Un snapshot contient **`meta`, `input`, `analysis`, `measures`, `proposal`**.
Les réglages utilisent leurs IDs stables, pas leurs indices. Les spectres binaires
little-endian encodés base64 permettent le rejeu exact sur la plateforme cible.
Objectif vérifié par test : **moins de 300 Ko**, y compris à 96 kHz.

Métadonnées : schéma, versions d'analyse/profil, rôle, style, intensité, source
et licence ; position/canaux facultatifs. L'export depuis le plug-in nécessite
option CMake `ZED_ANALYSIS_EXPORT=ON` et `ZED_ANALYSIS_EXPORT_DIR`, jamais le
callback audio. `ZED_ANALYSIS_EXPORT_LICENCE` renseigne la licence.

**Cibles restantes :** export CSV consolidé des mesures ; statistiques de
frappes agrégées ; proposition de `hits.csv` de développement limité à 200
frappes, séparé du snapshot. Ne pas annoncer ces fichiers comme déjà produits.

Le snapshot actuel conserve la proposition **avant** calibration finale.
Les politiques cymbales/sous-bus ne sont pas toutes enregistrées : documenter
les politiques de comparaison et compléter la traçabilité avant d'assimiler un
rejeu à l'intégralité du résultat Auto. Analyse et profils ont des versions
distinctes ; aucune de ces versions ne désigne le cache ARA.

<a id="corpus"></a>
## 3. Comparaison brut / mixé et cibles

### 3.1 Entrées et alignement

- Paire standard : même piste brute et stem après inserts, **avant bus/master**.
  Rôle, micro, mixeur, style, morceau, licence, samples et effets particuliers
  doivent être identifiés. Même fréquence, au moins **20 s de jeu effectif**.
- Chercher le décalage sur tout le fichier, puis affiner à l'échantillon ;
  conserver temps et polarité. Ne pas normaliser les niveaux avant estimation.
- Le repli stéréo actuel des paires est la moyenne des canaux ; ce n'est pas une
  analyse de largeur. Une différence de fréquence doit être refusée sans
  conversion documentée.
- Bus d'instrument : fournir tous ses micros/samples ; estimer
  `y = somme(H_i × x_i)` par régression multi-entrées régularisée
  (`lambda = 10^-4 × trace`). Conserver retard, polarité et gain par micro.
  Micros presque identiques : gains individuels non identifiables, ne pas leur
  inventer de contribution certaine. L'outil actuel accepte au plus 4 entrées.
- Samples non fournis, réverbération et autres sources peuvent rendre la paire
  inexploitable. Une cohérence faible est un constat, pas une identification du
  traitement exact de l'ingénieur.

### 3.2 Méthode et sorties attendues

| Étape | Exigence |
|---|---|
| EQ équivalente | Welch/Hann 4096, recouvrement 50 %, trames actives ; `H = Sxy/Sxx`, cohérence `abs(Sxy)^2/(Sxx*Syy)` |
| Forme / gain | Moyenne des **modules**, pas des complexes, par tiers d'octave ; retirer et stocker le gain moyen 100 Hz-5 kHz |
| Fiabilité | Cohérence médiane 100 Hz-8 kHz ≥0,8 et cohérence par bande ≥0,8 (**H**) ; bandes non fiables indisponibles, pas interpolées |
| Vraisemblance | Dérive début/fin >3 dB ou pic étroit >10 dB : avertissement ; contrôler alignement et automation |
| Compression cible | Enveloppes 10 ms après compensation de gain, tranches d'entrée 2 dB, sortie médiane ; coude/seuil, inverse de pente/ratio, GR P50/P95/max |
| Temps cibles | Attaque à 63 % et release à 37 % : approximations, information seulement tant que M02 n'est pas probant |
| Expansion cible | Plancher compensé, bleed et décroissance comparés ; ne pas appeler un simple gain négatif « gate » |
| Couleur | Part non expliquée par le filtre linéaire, `1 - cohérence`, pondérée ; peut inclure bruit/reverb, pas seulement saturation |
| Transitoire / stéréo cibles | Deltas H07, corrélation L/R, largeur M/S et retard L/R |

La spec historique utilise un seuil actif relatif au P95 pour l'EQ équivalente ;
le code actuel utilise un seuil relatif au pic le plus fort. **Écart à arbitrer**
par calibration, pas à modifier silencieusement pendant une consolidation.

Sorties cibles : JSON complet et `paires.csv` avec identité/licence/versions,
alignement, validité, cohérence, EQ/gain, dynamique, expansion, couleur,
transitoire et stéréo. Pour un bus, champs par micro et cohérence multiple.
Les champs dynamiques/stéréo complets et le CSV ne doivent pas être déduits
du seul fait que `--pair` produit déjà un rapport.

### 3.3 Agrégation à implémenter

1. Filtrer les paires valides et séparer standard, hybride et bus.
2. Grouper **rôle × style** ; **un mixeur, un poids équivalent**, pas un poids
   proportionnel au nombre de fichiers qu'il a fournis.
3. Produire médiane, P25/P75, nombres de paires et mixeurs.
4. Écarter/lister les valeurs à plus de **3 MAD**.
5. Publier une cible seulement à partir de **8 paires et 3 mixeurs** (seuil H) ;
   en dessous, la marquer provisoire.
6. Séparer réglage et validation **par kit entier**, pas par micro.

Paires après bus : proposition `bus_inclus`, exclusion des cibles de dynamique.
Samples : proposition `hybride`, corpus séparé. Ne pas confondre les étiquettes
libres déjà lues par le manifeste avec ces exclusions automatiques à implémenter.

<a id="presets"></a>
## 4. Calcul des presets

### 4.1 Modèle cible, distinct du moteur actuel

```text
mesure + cible (rôle/style, dispersion, provenance)
  -> écart -> correction partielle -> zone morte -> bornes
  -> intensité/confiance -> réglage et explication
```

Correction candidate : `k × (cible - mesure)`, avec **k = 0,5**, zone morte
**1 dB / 5 % en fréquence** (H). Le modèle doit refuser les données insuffisantes,
éviter les règles concurrentes pour le même réglage et indiquer l'incertitude.
La cible contient ses distributions, ses versions et son effectif, pas une seule
valeur supposée universelle.

**Décisions à ne pas effacer :** le moteur conserve actuellement la dose non
linéaire (1,75 à 100 %), ses choix de style et ses plafonds dosés. Le modèle
historique « plafond puis intensité linéaire » n'autorise pas à les remplacer
sans annonce, comparaison et validation. Les styles intermédiaires par
interpolation sont une **proposition**, pas une capacité déjà livrée.

### 4.2 Règles par fonction

| Fonction | Direction de la cible |
|---|---|
| Filtres | Corps, coupure utile, fuite grave et rolloff ; ne pas imposer une coupe issue d'un seul mixeur |
| Expander | Contraste utile, niveau des ghost notes, décroissance ; protection candidate : seuil ≤ niveau ghost -3 dB ; préserver coups faibles et queues |
| Focus / nettoyage | Détecteur adapté au corps, profondeur liée à la repisse et aux données ; ne pas appliquer la synthèse Kick aux autres rôles |
| Corps synthétique | Fréquence/quantité/extinction explicites ; pas une correction certifiée du pitch original |
| EQ quatre bandes | Écart à la cible par bande, fréquence du corps/résonance, correction bornée ; séparer couleur de style et défaut mesuré |
| Compresseur | Crest actif et compression déjà présente, cible/plafond, temps de style tant que les estimations ne sont pas validées |
| Multibande | Crossovers stables, seuils par niveau de bande, plafonds indépendants ; vérifier phase et recombinaison |
| Glue | Mesure de la somme, sidechain HP, knee, liaison stéréo ; pas une mesure des faders individuels |
| Room | Caractère/décroissance, retour filtré, direct préservé, queues d'export ; pas une vraie plate supposée existante |
| EQ dynamique / Impact | Résonances et excès transitoires mesurés, réductions/doses bornées ; pas de déplacement continu des fréquences par frappe |
| Kit / Phase | Première arrivée et polarité à ce retard, confiance, correction dosée et réversible |

Les anciennes tables « Actuel » et les captures ne sont pas une configuration
de référence actuelle : consulter le code. En particulier :

- `-35 dB` est un seuil initial multibande ; le rendu de calibration recalcule
  les seuils depuis les enveloppes du moteur, **après les traitements amont**.
- Le risque « EQ ignorée par tous les seuils » doit être examiné sur le trajet
  complet, pas seulement dans la fonction qui propose le premier réglage.
- Des estimations de corps Snare/Toms existent ; les résonances spécialisées
  et la base statistique ne sont pas pour autant achevées.
- Le Bilan textuel existe, mais le futur objet par réglage
  `{fonction, parametre, valeur, unite, regle, mesure, cible, intensite, confiance,
  raison_bilan}` avec dispersion/provenance n'est pas entièrement livré.

### 4.3 Calibration et budget

Pour modifier une règle : identifier constante et origine, choisir la cible,
vérifier ses effectifs, modifier un ensemble cohérent, rejouer et comparer,
tester les propriétés, écouter les prises affectées et valider sur le jeu
indépendant. Consigner résultat et version dans le suivi.

**Proposition non implémentée :** budget global de traitement pondérant gains EQ,
GR, range d'expander/2 et drive, avec plafond par style et préservation des
corrections de phase/niveau. La répartition **40 % micro / 40 % sous-bus / 20 %
bus** reste H. Les limites individuelles actuelles ne prouvent pas ce budget.

### 4.4 Presets utilisateur : son et identité

**Décision validée le 20/10/2026, F17 :** un nouveau preset `.zedpreset`
restaure le son, l'instrument et la position de micro sélectionnés à sa sauvegarde.
Charger Kick In depuis Kick Out doit donc afficher **Kick In** ; charger un
Tom 2 depuis Floor Tom restaure **Tom 2**. Le classement par famille reste
inchangé (toms partagés ; pas de chargement transversal Kick/Snare).

- **Format 2** : `controls` conserve `instrument` et `kitMic` par leurs IDs
  stables. Ces deux valeurs sont obligatoires, numériques, entières et dans
  leurs plages ; l'instrument doit correspondre à la famille du fichier.
  Une identité invalide provoque un refus explicite sans application partielle.
- **Format 1** : reste lisible avec son comportement antérieur ; rôle et position
  courants sont conservés. Le nom descriptif `instrument` à la racine n'est pas
  interprété comme une identité complète. Pour mémoriser la position, choisir
  l'identité voulue puis réenregistrer le preset en format 2.
- Groupe Kit, mode/force, retard, polarité, bypass, écoute mono et options du
  prochain Auto ne sont pas importés. Les indications style/intensité restent
  informatives. Revert restaure le son **et l'identité** précédents.
- UI et identité publiée au registre suivent le chargement et Revert. Si une
  identité utilisée par l'analyse Kit change, masquer/invalider son rapport
  de groupe et ses recommandations en attente ; le worker abandonne l'analyse
  obsolète. Les corrections déjà appliquées restent inchangées.
- Critères : aller-retour des rôles/positions, anciens fichiers, refus de format
  ou identité invalide, conservation des paramètres de session, Revert,
  libellés de l'éditeur et invalidation d'une analyse terminée ou en cours.

Ce changement ne modifie ni les règles Auto ni les IDs de paramètres.
Le format des presets est distinct des versions d'analyse/profil et des états hôte.

<a id="kit"></a>
## 5. Kit, sous-bus et extensions

### 5.1 Contrat du kit et des sous-bus

- Référence temporelle OH ; secondaires In/Out, Top/Bottom cohérents avec leur
  primaire. Sans primaire, préserver les cas de mesure directe documentés.
- Première arrivée, pas simple maximum global de corrélation ; polarité au
  retard trouvé. Ne jamais avancer la Room sans mode de latence explicite.
- L'audio et les identités sont échangés par registre ; chaque instance écrit
  son propre membre et applique ses propres paramètres. Pas de modification
  d'une autre piste depuis le thread audio.
- Sous-bus identifié par famille d'instrument et groupe Kit. Une instance
  micro est nécessaire pour chaque contribution inter-micros observée.
  Sans ces instances, le bus peut traiter sa somme, pas inventer les détails.
- Les rapports actuels reconstruisent une somme des captures avec retard et
  polarité ; ne pas l'assimiler sans vérification à tout le mixage réel
  post-fader/post-traitements de l'hôte.

### 5.2 Mesures de groupe

| ID | Exigence cible | Portée actuelle à ne pas surestimer |
|---|---|---|
| SB01 | Niveau actif de chaque micro relatif à la somme | Disponible |
| SB02 | Cohérence interne par bande | Corrélation moyenne dans le grave, pas catalogue multibande complet |
| SB03 | Gain de somme par tiers d'octave | Valeur globale et grave ; pas courbe complète |
| SB04, SB05 | Contributions par bande, recouvrement des micros | À construire |
| SB06 | Polarité vs somme des autres en grave | Disponible |
| SB07 | Retard résiduel après correction | Disponible, référence au micro le plus fort |
| SB08 | Marge avant écrêtage de la somme | Disponible sur la somme reconstruite |

`gain_somme_db = 10 log10(énergie somme / somme des énergies)`.
Micros indépendants : environ 0 dB ; deux identiques : +3,01 dB ; trois :
+4,77 dB ; annulation : négatif. Un groupe d'un seul micro ne fournit pas
de valeurs inter-micros inventées.

### 5.3 Balance et traitement

- Proposer les trims, **jamais les appliquer silencieusement**. Chaque micro
  modifie son gain après validation ; application/annulation et isolation des
  groupes doivent être testées.
- Refuser la balance en cas de polarité douteuse, retard résiduel excessif ou
  annulation grave. Les seuils -0,25 de corrélation, 1 ms et -1,5 dB sont H.
- La règle livrée corrige la **distance à une plage plausible**, pas à un niveau
  unique : facteur 0,5, intensité, bornes ±6 dB, aucune proposition sous 0,5 dB.
  Références Kick/Snare provisoires élargies de 1,5 dB ; pas de référence Toms.
- EQ de groupe sur la somme : Kick/Snare ont une référence provisoire.
  Les objectifs historiques ±4 dB d'EQ, compression 1-2 dB Natural / 2-4 Rock /
  4-6 Metal et demi-dose de punch restent des propositions à calibrer.
- La répartition stricte nettoyage au micro / timbre-dynamique au sous-bus /
  Glue-ambiance au bus est une **architecture cible**. Aucun budget partagé ne
  réduit actuellement tous les traitements des micros à la présence d'un bus.
- Gate lié : détecteur partagé futur ; synchronisation des hold/release et
  alerte d'ouvertures différentes non acquises par le seul rapport de retard audio.
- Paires de sous-bus : somme alignée sans traitement contre bus de l'ingénieur ;
  balance cible avec médiane/dispersion et mêmes règles de confiance du corpus.

### 5.4 Rôles et jalons

Le registre cible centralise famille, micros, détecteur, mesures/fonctions,
bandes/plages, relations de phase et cibles. Sa migration doit conserver les
sorties de référence ; distinguer refactor et changement de profil.

Extensions : Snare Bottom et Kick Out existent comme **positions**, pas comme
nouveaux profils spécialisés. Kick Sub, Hi-hat Bottom, Rack/Floor à plages
distinctes, variantes OH/Room, Ride/Crash, sample/trigger, percussions et bus
parallèle restent des extensions à arbitrer. Les seuls nouveaux sous-bus livrés
sont Kick/Snare/Toms ; HiHat/OH/Room Bus ne sont pas implicitement inclus.

Procédure : définir mesures et descripteur, réunir des prises licites (5 pour
démarrer ; seuil normal des cibles ensuite), ajouter tests spécifiques et UI,
valider les effets et documenter. Détection automatique depuis un nom de piste
ou un modèle : proposition, pas prérequis.

**Correspondance des anciens jalons :**

| Fonction | Spec 08 | Ancien journal |
|---|---|---|
| Rôles et identification | SB1 | SB1 |
| Mesures de groupe | SB2 | SB2 |
| Tests S01-S08 | SB3 | Répartis dans les livraisons ; pas tous équivalents à la spec |
| Profils de groupe | SB4 | SB3 |
| Trims | SB5 | SB4 |
| Gate partagé | SB6 | Futur, parfois appelé SB5 |
| Cibles statistiques | SB7 | Futur, parfois appelé SB6 |

Toujours nommer la **fonction** en plus du numéro dans un nouveau changement.

<a id="tests"></a>
## 6. Programme de validation

Les tests utilisent les mêmes sources que le plug-in et l'exécutable existant
`ZeroEffortDrumTests`. Pas de cible `drumtest` supplémentaire. Les familles
ci-dessous constituent le contrat de couverture, pas une liste de tests verts.

| Famille / IDs | Cas et critères conservés |
|---|---|
| T-A / A01-A04 | Sinus amplitude 0,5 : pic -6,02, RMS -9,03, crest 3,01 dB ; silence, écrêtage et NaN refusés correctement |
| A05-A07 | Bruit rose -3 dB/oct ±0,5, bruit blanc 0 ; bandes connues à ±1 dB ; 24 frappes de 60 Hz et corps ±6 Hz |
| A08-A09 | Doubles 180 bpm, compte attendu ; crest actif épars/dense à ±1 dB ; tester aussi l'ancien détecteur désactivé |
| A10-A11 | Invariance aux blocs 64/257/512/4096 et cohérence 44,1/48/96 kHz ; exclure les absolus FFT non étalonnés de la comparaison relative |
| A12-A13 | Éclat aigu +6 dB ±1,5 ; T60 Room 0,4/0,8/1,5 s à ±15 % |
| T-B / T-C | Rejeu déterministe, refus, différences, proximité des seuils ; réexport WAV quand la version d'analyse change |
| T-D / D01-D04 | Retard Kick/Snare, inversion, combinaison ; précision cible ±1 échantillon pur, ±0,2 ms réel |
| D05-D07 | Diaphonie (-12/-6 dB), bruit (-50/-40/-30 dBFS), OH L/R décalés de 0,4 ms : juste ou refus explicite |
| D08-D10 | Niveau -20 dB sans changement de forme ; kit à trois pistes ; Kick Out sans Kick In |
| T-J / J01-J07 | Shelf +6 dB, bosse 3,5k, fuite grave, crash isolé, T60 Room, polarité Room/OH, passage stéréo→mono |
| T-E / E01-E02 | Valeurs finies et réglages dans leurs plages |
| E03-E05 | Intensité nulle : traitements ajoutés éteints ; monotonie des gains EQ ; GR Natural ≤ Rock ≤ Metal |
| E06-E10 | Cible ≤ plafond, sortie initiale prudente/bypass préservé, déterminisme, refus explicite et réglages inchangés après refus |
| T-F | Cas dorés validés à l'oreille, cible 3 par rôle/style, tolérances par réglage approuvées |
| T-G | Distributions par rôle/style avant/après ; alerte sur médiane déplacée sans annonce |
| T-H / H01-H04 | Temps médian/max de push pour 1/8 instances ; allocations ; mémoire ; absence de dépassement du budget audio à bloc 64 |
| T-M / M01 | EQ connue +4 dB à 100 Hz / -3 dB à 3k, retrouvée ±1 dB ; variante existante différente à identifier |
| M02-M03 | Compresseur -24 dB, 4:1, 10/100 ms : seuil ±3 dB, ratio ±1, temps facteur 2 ; gate -45 dB/range 12, estimation ±3 dB |
| M04-M07 | Saturation détectée, retard 0,5 ms retrouvé, reverb 30 % et parallèle 50 % reconnus comme limites de la méthode |
| T-S / S01-S03 | Somme de 3 micros identiques +4,77 dB ; 2 identiques +3,01 dB ; 3 bruits indépendants 0 ±1 dB |
| S04-S06 | Sub inversé et réparation ; peigne à 1 kHz pour 0,5 ms ; Sub trop fort de 6 dB, trim négatif borné |
| S07-S08 | Un micro : pas de valeur inventée ; somme forte : marge/alerte correcte |

**Ne pas confondre preuves et objectifs :**

- Le biais Kick actuel ne respecte pas la tolérance cible ±1 échantillon.
  Un test « échec attendu » conserve le constat, il ne certifie pas l'exigence.
- E03/E04 ont des couvertures partielles dans les tests processeur ; l'ensemble
  E03-E05 n'est pas une propriété universelle déjà vérifiée.
- E07 porte sur la proposition initiale (`output <= -3 dB` dans son test),
  pas une obligation générale de sortie finale après calibration.
- Le compteur d'allocations de `push()` ne certifie pas tous les callbacks audio.
- Les tests de groupe présents ne remplacent pas automatiquement S01-S08,
  notamment les valeurs exactes de somme, le peigne par bande et le Kick Sub.
- Les cas dorés musicaux sont différents des hachages de non-régression.
  Aucun cas doré ne peut être approuvé automatiquement par l'assistant.

### 6.1 Rapport et critères de livraison

Rapport Markdown + JSON : versions/données, refus par rôle/style, propriétés
réellement testées, réglages modifiés (moyenne/max/prise), cas dorés quand
disponibles, proximité des seuils (±5 %) et liste des prises à écouter.
Une rubrique non implémentée ne doit pas apparaître comme réussie.

Repères : niveaux synthétiques ±0,1 dB ; bandes ±1 dB ; corps ±1 bin sauf cas
plus strict ; rejeu 1e-6 relatif ; variation des refus >5 points = alerte.
Un changement de cas doré non approuvé, une propriété ou une mesure hors tolérance
est bloquant. Les seuils d'écoute actuels (1 dB, 5 % fréquence, 10 % autres) sont H.

Exécuter les trois suites CTest avec **Build and Test Release** ; consigner les
SKIP et la couverture réelle. Pour l'écoute : mêmes source/style/intensité,
niveau rapproché, solo **et mix**, ghost notes, passages denses, fills et pauses.
Proposition de protocole : A/B aveugle, au moins 5 auditeurs, un changement
cohérent à la fois. Cela ne décrit pas une campagne déjà réalisée.

Hôte : scan, insert/ARA, plusieurs instances, mono/stéréo, ASIO Guard,
automations, bypass/Revert, sauvegarde/réouverture, transport/édition et export
avec queues. Validation multi-hôtes et pluginval en CI restent des propositions,
pas une infrastructure disponible.

<a id="decisions"></a>
## 7. Décisions et hypothèses

Ce registre conserve les arbitrages des anciens documents 05 et 08. Il distingue
une pratique appliquée d'une proposition ; les preuves et suites restent dans
[le suivi](PROJET.md#a-faire).

| Référence | Décision / proposition à conserver |
|---|---|
| 01-Q1 | 55 dB calibré, +12 dB conservé/H ; ne pas rouvrir sans nouveaux résultats |
| 01-Q2 | **Proposition bêta** : mesures seules par défaut ; extrait ≤30 s uniquement sur demande et consentement séparé, suppression sur demande |
| 01-Q3 | Global + statistiques agrégées ; proposition hits.csv ≤200 frappes en développement |
| 01-Q4 | Fréquences en Hz et plafond 0,45 × rate ; absolus S01 non comparables sans étalonnage |
| 01-Q5 | Trames longues 400/100 ms H ; choisir la plus courte stable à <1 dB contre 800 ms |
| 01-Q6 | Room autonome pour ses mesures propres ; relations OH indisponibles si OH absents |
| 02-Q1 | Défauts synthétiques puis 3-5 kits réels ; un seul kit ne remplit pas la cible |
| 02-Q2 | Propriétés, cas dorés, sens de correction ; cible de correction 30-100 % d'une perturbation connue à tester |
| 02-Q3 | Budget CPU **proposé/H** : <1 % d'un cœur par instance, 8 analyses <10 % ; seuil final 2× médiane mesurée |
| 02-Q4 | Proposition pluginval/CI et checklist Cubase/Reaper/Studio One avant bêta ; Cubase reste l'hôte actuel |
| 02-Q5 | Cible corpus 10 kits par jeu, viser 15 ; au-delà de 30 au total, privilégier les cas difficiles |
| 03-Q1 | Cohérence 0,8 et 8 paires/3 mixeurs H ; relier le seuil à une erreur EQ <1 dB sur les tests M |
| 03-Q2 | Stems avant bus ; proposition d'étiquetage/exclusion dynamique des stems après bus |
| 03-Q3 | Hybrides séparés des cibles standard ; sample fourni possible comme entrée du bus |
| 03-Q4 | Temps informatifs jusqu'à récupération à facteur 2 dans 80 % des cas M02 |
| 03-Q5 | WAV hors Git, droits de source vérifiés, pas de redistribution |
| 04-Q1 | k=0,5, zone morte 1 dB/5 % H ; calibration par fonction et écoute |
| 04-Q2 | Proposition : dispersion des mixeurs visible dans un Bilan détaillé |
| 04-Q3 | Pas de variantes Doux/Précis engagées ; l'intensité suffit au périmètre courant |
| 04-Q4 | Budget global pondéré : proposition, non livré |
| 04-Q5 | Détection/option « Kick avec samples », traitements adoucis : proposition, non livrée |
| 08-Q1 | Balance proposée ; chaque micro applique sur validation, annulation disponible |
| 08-Q2 | Gate partagé différé ; ne pas supposer la cohérence des gates déjà assurée |
| 08-Q3 | Sous-bus sans micros instrumentés : traitement de la somme, pas mesures inter-micros |
| 08-Q4 | Budget partagé 40/40/20 : proposition/H, non livré |
| 08-Q5 | Latence des micros non ajoutée par Kit actuel ; vérifier l'ordre réel dans l'hôte |
| 08-Q6 | Menu d'instruments avec section Buses livré ; le sélecteur séparé Piste/Sous-bus/Bus et HiHat Bus ne sont pas retenus comme acquis |

Toute nouvelle hypothèse numérique doit être nommée dans une constante,
reliée à sa mesure de validation, et non disséminée comme nombre magique.

<a id="futur"></a>
## 8. Évolutions produit non engagées

Intentions utiles du plan historique conservées sans les transformer en backlog
immédiat :

- **Analyse du morceau entier en insert** : démarrer/pause/fin explicite,
  couverture signalée, annulation ; ne pas déduire la fin d'un silence.
- **Courbes par frappe** : gain prudent candidat ±3 dB, préservation des accents,
  ghost notes et crescendos ; exclusions, confiance, bornes et lissage.
  Temps absolu en insert, mapping de régions ARA ; pas d'extrapolation hors
  couverture, invalidation si source modifiée, pas d'écriture APVTS par bloc
  ni promesse d'automations dans les lanes Cubase.
- **Provenance persistante** : distinguer le preset appliqué, son Bilan et ses
  conditions des options du prochain Auto ; taille d'état bornée et migrations.
- **Palette manuelle** : shelves/bells, pentes sélectionnables, écoute du
  détecteur, sidechain externe, ordre EQ/compresseur optionnel, lookahead
  candidat 0-5 ms avec latence déclarée et mode monitoring sans lookahead.
- **UX** : application sélective de modules, timeline/confiance/couverture,
  vue musicale simple/avancée, accessibilité clavier et scaling ; ne pas
  confondre ces objectifs avec les pages actuellement livrées.
- **Finition** : vraie plate Snare, clips mieux caractérisés, styles supplémentaires
  et coordination stéréo OH ; validation musicale avant généralisation.
- **Recherche neuronale locale** : seulement si gain démontré sur jeu indépendant,
  licence/poids/données vérifiés, CPU/mémoire acceptables et repli sans modèle.
  Pas de dépendance, téléchargement ou envoi de pistes ajouté implicitement.

Le diagnostic historique « Kick devenu mauvais » a été résolu par le retour au
style Rock ; ce n'est pas une régression DSP ouverte à corriger automatiquement.
Les mentions Standalone, ancien prototype et Naturel par défaut du plan sont
supplantées par le cadre actuel.

<a id="sources"></a>
## 9. Sources et règles de maintenance

| Source conservée intégralement en archive | Sections de cette synthèse |
|---|---|
| [01 - Mesures](archives/CodeUpdate/01_cahier_des_charges_mesures.md) | 2, 6 |
| [02 - Tests](archives/CodeUpdate/02_programme_de_test.md) | 6, 7 |
| [03 - Brut / mixé](archives/CodeUpdate/03_comparaison_brut_mixe.md) | 3, 6 |
| [04 - Presets](archives/CodeUpdate/04_calcul_des_presets.md) | 4, 7 |
| [05 - Décisions](archives/CodeUpdate/05_decisions_questions_ouvertes.md) | 1, 7 |
| [06 - Extensions](archives/CodeUpdate/06_extensions_futures.md) | 5, 8 |
| [07 - Prompts historiques](archives/CodeUpdate/07_prompt_copilot_vscode.md) | Principes de méthode seulement, aucun prompt de réimplémentation actif |
| [08 - Sous-bus](archives/CodeUpdate/08_sous_bus.md) | 5, 6, 7 |
| [Plan produit historique](archives/plan.md) | 1, 6, 8 |

Mettre les résultats/jalons dans **PROJET**, modifier **SPECIFICATIONS** seulement
si une exigence, une décision ou sa portée change. Les tables « actuel » des
archives ne doivent plus être mises à jour. Une absence dans cette synthèse
n'efface pas un détail archivé : le consulter pour le chantier concerné et
résoudre les ambiguïtés avant le code, sans ressusciter les anciens plans.
