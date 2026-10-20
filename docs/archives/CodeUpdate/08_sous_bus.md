# Sous-bus par instrument

<!-- consolidated-archive -->
> **Archive historique.** Référence consolidée : [SPECIFICATIONS](../../SPECIFICATIONS.md).
> Avancement vérifié : [PROJET](../../PROJET.md). Les états et chemins ci-dessous
> sont historiques ; les numéros SB diffèrent de ceux de l'ancien journal.
<!-- /consolidated-archive -->

**Document 8** | Version 0.1 | Complète les documents 01 à 07 | Dépend de : 01 (mesures), 04 (presets), 06 (registre de rôles)

**Interprétation de votre demande** : un instrument à plusieurs micros (Kick : In, Out, Sub ; Snare : Top, Bottom ; Hi-hat : Top, Bottom) a un **sous-bus** qui reçoit la somme de ses micros. Le bus batterie complet existe déjà ; il se place au-dessus.

---

## 1. Objet

Traiter un **instrument** comme un tout, après que chaque micro a été nettoyé et aligné :

- mesurer comment les micros s'**additionnent** (cohérence, annulations, balance) ;
- appliquer des réglages **de groupe** (EQ, compression, punch) sur la somme ;
- proposer une **balance** entre les micros ;
- éviter que le même traitement soit appliqué trois fois (une fois par micro, puis sur le sous-bus, puis sur le bus).

## 2. Hiérarchie

```
Micros                     Sous-bus (instrument)            Bus batterie
Kick In  ─┐
Kick Out ─┼──►  Kick Bus  ──┐
Kick Sub ─┘                  │
Snare Top ─┐                 │
Snare Bot ─┴──► Snare Bus ───┤
HH Top ───┐                  ├──►  Drum Master Bus (glue + ambiance)
HH Bot ───┴──►  HiHat Bus ───┤
Toms (Floor, Rack…) ► Toms Bus ┤
OH L / OH R ──► Overheads Bus ─┤
Room(s) ──────► Room Bus ─────┘
```

Un instrument à micro unique (par exemple une caisse claire avec seulement Top) fonctionne **sans sous-bus** : le micro est directement son propre « instrument ».

## 3. Trois types d'instance

| Type | Où on le place | Ce qu'il voit | Rôle |
|---|---|---|---|
| **Micro** (existant) | sur la piste du micro | son propre signal | nettoyage, phase, correction propre au micro |
| **Sous-bus** (nouveau) | sur la piste de groupe de l'instrument | la **somme** de ses micros | timbre et dynamique de l'instrument |
| **Bus batterie** (existant) | sur le bus général | la somme de tout | glue et ambiance globales |

**Limite de principe.** Une instance posée sur un groupe ne reçoit que la somme : elle ne voit pas les micros séparément. Pour mesurer ce qui se passe *entre* les micros (corrélation, balance), le sous-bus lit les **mesures publiées par les instances micro** dans le registre partagé (celui que Kit / Phase utilise déjà, avec le même groupe, par exemple « Kit 1 »). Cela suppose que **chaque micro ait son instance** ; sinon le sous-bus fonctionne comme un rôle ordinaire, sans mesures inter-micros (voir 08-Q3).

**Identification des micros d'un sous-bus** : même groupe de kit et même famille d'instrument (Kick, Snare, Hi-hat…), d'après les champs Groupe, Micro et Role que le module Kit / Phase utilise déjà.

## 4. Composition des sous-bus

| Sous-bus | Micros | Mesures inter-micros typiques |
|---|---|---|
| Kick Bus | Kick In, Kick Out, Kick Sub (+ sample / trigger) | polarité du Sub, balance In / Out / Sub, annulation grave |
| Snare Bus | Snare Top, Snare Bottom (+ Side, sample) | polarité Top / Bottom, annulation dans les médiums |
| HiHat Bus | HH Top, HH Bottom | polarité, corrélation, bleed de caisse claire |
| Toms Bus | Floor, Rack 1, Rack 2… | balance entre toms, diaphonie entre toms |
| Overheads Bus | OH L, OH R | corrélation G/D, largeur |
| Room Bus | Room proche, lointaine, mono | décalage et compatibilité avec les overheads |

## 5. Répartition des responsabilités

Pour éviter les doublons, chaque niveau a un rôle précis.

| Fonction | Micro | Sous-bus | Bus batterie |
|---|---|---|---|
| Filtres (passe-haut / passe-bas) | oui, propres au micro | non | léger (20 Hz) |
| Nettoyage, expander, gate | **oui** | non (voir 7.3) | non |
| Phase, retard, polarité | **oui** (Kit / Phase) | mesure seulement | non |
| EQ correctif propre au micro | oui (par exemple LP du Sub, HP du Out) | non | non |
| **EQ de groupe** (timbre de l'instrument) | non | **oui** | EQ large global |
| Compression | aucune ou très légère | **oui** (glue de l'instrument) | glue globale |
| Punch, saturation | non | **oui** | non |
| **Balance des micros** | reçoit un trim | **calcule** le trim | non |
| Ambiance / room | non | non | **oui** |

Le **budget de traitement** (décision 04-Q4 du document 05) s'applique à la chaîne entière micro, sous-bus, bus : un instrument dont le sous-bus compresse beaucoup voit sa compression au niveau du micro réduite (voir 08-Q4).

## 6. Mesures du sous-bus (famille `sb_`)

Toutes les mesures du document 01 s'appliquent aussi à la **somme** (le sous-bus est analysé comme une piste, avec un rôle « Kick Bus », « Snare Bus »…). Les mesures ci-dessous sont spécifiques aux groupes.

N = nombre de micros du groupe, après alignement de Kit / Phase.

| ID | Mesure | Calcul | Décision servie | Prio | État |
|---|---|---|---|---|---|
| SB01 | `balance_micros_db` | niveau actif de chaque micro relatif à la somme | trims de balance | P1 | À faire |
| SB02 | `coherence_interne` | corrélation moyenne pondérée entre les micros, par bande | indice de santé du groupe | P1 | À faire (réutilise K01 à K03) |
| SB03 | `gain_somme_db` (par tiers d'octave) | `10 × log10( énergie de la somme / somme des énergies des micros )` | repère annulations et additions | P1 | À faire |
| SB04 | `contribution_bande` | part de l'énergie de la somme apportée par chaque micro, par bande | « qui couvre quoi » (Sub : grave, In : attaque, Out : corps) | P2 | À faire |
| SB05 | `recouvrement_micros` | bandes où deux micros dominent à la fois | EQ complémentaire de chaque micro | P2 | À faire |
| SB06 | `polarite_vs_somme` | signe de la corrélation de chaque micro avec la somme des autres, en grave | confirmation de l'inversion de polarité | P1 | À faire |
| SB07 | `retard_residuel_ms` | retard résiduel entre micros après Kit / Phase | alerte si l'alignement est insuffisant | P1 | À faire |
| SB08 | `somme_clip_marge_db` | marge avant écrêtage de la somme | garde-fou de sortie | P1 | À faire |

**Lecture de `gain_somme_db`.** Pour N micros de même niveau :
- micros indépendants : environ 0 dB ;
- micros parfaitement cohérents : jusqu'à `10 × log10(N)` dB (2 micros : +3,01 dB ; 3 micros : +4,77 dB), qui est le **maximum possible** ;
- annulation (polarité inversée, peigne) : valeurs négatives.

**Exemple de peigne.** Deux micros identiques décalés de 0,5 ms donnent un premier creux à 1 kHz (la fréquence est `1 / (2 × retard)`). `gain_somme_db` doit le montrer.

## 7. Fonctions et calcul des presets

Les règles générales du document 04 s'appliquent (mesure, cible, écart, correction partielle, bornes, intensité, confiance, refus).

### 7.1 Balance des micros (trims)

| Élément | Règle |
|---|---|
| Entrée | `balance_micros_db` (SB01) et cible de balance (7.4) |
| Calcul | `trim = clamp( k × (cible − mesure), −6, +6 dB ) × intensité` par micro (bornes à calibrer) |
| Application | le sous-bus **ne modifie pas** le gain des pistes micro : il publie un **trim proposé** dans le registre ; l'instance micro correspondante l'affiche et l'applique si l'utilisateur valide (mode Suggérer, par défaut) |
| Refus | si un micro est très en retard ou en polarité douteuse, **aucun trim** n'est proposé avant correction de Kit / Phase |

### 7.2 EQ de groupe et compression de groupe

Mêmes mécanismes que l'EQ à 4 bandes (document 04, 5.4) et que le compresseur (5.5), **calculés sur la somme** et avec les cibles du rôle « Bus » correspondant (Kick Bus, Snare Bus…). Le seuil de compression se calcule sur le P95 de l'enveloppe **de la somme**, ce qui règle une partie du risque R1 (les micros ont déjà été traités en amont).

| Fonction | Valeur de départ (hypothèse H) |
|---|---|
| Réduction visée du compresseur de sous-bus | 2 à 4 dB (Rock), 1 à 2 dB (Naturel), 4 à 6 dB (Metal) |
| EQ de groupe | gains plafonnés à ± 4 dB |
| Punch / saturation | doses proches de celles du micro principal, divisées par 2 |

### 7.3 Gate et expander liés (version ultérieure)

Aujourd'hui chaque micro a son propre gate : les trois s'ouvrent à des instants légèrement différents et peuvent créer un peigne sur les attaques. **Décision proposée (08-Q2)** : en version 1, chaque micro garde son gate, avec des réglages cohérents (même hold, même release) et un avertissement quand les ouvertures diffèrent de plus d'une durée seuil. Un **détecteur partagé** (le micro principal pilote les autres) vient dans un jalon dédié (SB6).

### 7.4 Cibles issues des données

Document 03 adapté au niveau sous-bus :

- **Paire « sous-bus »** : *brut* = somme des micros alignés, sans traitement ; *mixé* = sortie du sous-bus du mixeur (après ses inserts de groupe).
- **Balance cible** : pour chaque micro d'un stem post-fader, le `gain_moyen_db` déjà stocké (document 03, 5.2) donne son niveau dans le mix ; la différence entre micros donne la **balance de mixeur**. Les cibles de balance sont la médiane de ces différences par rôle et par style, avec dispersion.
- Les seuils habituels (8 paires, 3 mixeurs) restent valables.

## 8. Tests (à ajouter au document 02)

Famille `T-S` :

| Test | Entrée | Attendu |
|---|---|---|
| S01 | 3 micros synthétiques identiques, alignés | `gain_somme_db` ≈ +4,77 dB dans la bande commune |
| S02 | même chose avec 2 micros | ≈ +3,01 dB |
| S03 | 3 micros indépendants (bruits différents) | `gain_somme_db` ≈ 0 dB (± 1 dB) |
| S04 | un micro (Kick Sub) en polarité inversée | annulation en grave ; Kit / Phase propose l'inversion ; après correction, gain de somme positif |
| S05 | In et Out décalés de 0,5 ms | creux de `gain_somme_db` près de 1 kHz |
| S06 | Kick Sub à +6 dB trop fort | `balance_micros_db` le détecte ; trim proposé négatif, dans les bornes |
| S07 | groupe avec **un seul** micro | le sous-bus se comporte comme un rôle ordinaire, sans erreur ni valeur inventée (`null`) |
| S08 | somme de trois micros forts | `somme_clip_marge_db` correct, avertissement avant écrêtage |

Propriétés (famille E) : trims toujours dans les bornes ; le sous-bus ne propose rien tant que la polarité est douteuse ; intensité 0 = tout éteint ; déterminisme.

## 9. Registre de rôles : extension

Ajouter au descripteur de rôle (document 06, section 2) :

```
GroupDescriptor
  id, nom ("Kick Bus", "Snare Bus", ...)
  micros_roles[]     : rôles attendus (Kick In, Kick Out, Kick Sub...)
  micro_principal    : rôle qui sert de référence de temps et de détecteur (Kick In, Snare Top...)
  fonctions[]        : EQ de groupe, compression, punch...
  cibles_ref         : cibles de sous-bus
  budget_traitement  : part du budget global (08-Q4)
```

Un micro appartient à **au plus un** sous-bus.

## 10. Plan d'implémentation par jalons

| Jalon | Contenu | Livrable |
|---|---|---|
| SB1 (**fait le 20/10/2026**, rôles et menu seulement, voir [le suivi](../../PROJET.md#fait)) | `GroupDescriptor`, rattachement des micros à un sous-bus par le registre, type d'instance « Sous-bus » dans l'interface | Un sous-bus qui s'identifie et liste ses micros |
| SB2 (**fait le 20/10/2026**, rapport texte dans le bouton Report) | Mesures `sb_` (SB01 à SB03, SB06 à SB08) en lecture seule, sans effet sur les presets | Rapport de groupe dans le Bilan |
| SB3 | Tests S01 à S08 | Tests au vert |
| SB4 | Presets de sous-bus : EQ de groupe et compression de groupe sur la somme | Sous-bus Kick et Snare utilisables |
| SB5 (**fait le 20/10/2026**, appelé SB4 dans le suivi : trims proposés, appliqués sur clic, annulables) | Trims de balance proposés (mode Suggérer) | Balance suggérée, validée par l'utilisateur |
| SB6 | Détecteur partagé pour les gates | Ouverture cohérente des micros d'un même instrument |
| SB7 | Cibles de sous-bus issues des paires brut / mixé, balance cible | Cibles publiées avec leur dispersion |

Prérequis : le registre de rôles (jalon du document 06) et la prise en charge de **plusieurs micros par instrument** dans Kit / Phase.

## 11. Décisions proposées pour les questions ouvertes

| # | Question | Décision proposée | Statut |
|---|---|---|---|
| 08-Q1 | Qui applique la balance des micros ? | Le sous-bus calcule et **propose** un trim ; l'instance micro l'applique après validation (mode Suggérer). Pas de modification silencieuse | P |
| 08-Q2 | Gate partagé dès la version 1 ? | Non. Version 1 : réglages cohérents et avertissement de décalage ; détecteur partagé au jalon SB6 | P |
| 08-Q3 | Sous-bus sans instances micro ? | Il fonctionne comme un rôle ordinaire sur la somme ; les mesures inter-micros restent `null`, les trims ne sont pas proposés | P |
| 08-Q4 | Budget de traitement entre les niveaux | Budget global partagé, réparti au départ à 40 % micro / 40 % sous-bus / 20 % bus (hypothèse H à calibrer) ; plus le sous-bus compresse, moins le micro le fait | P, H |
| 08-Q5 | Compensation de latence et ordre de traitement | Les instances micro n'ajoutent aucune latence (comportement de Kit / Phase) ; ordre vérifié manuellement dans Cubase, Reaper et Studio One avec la checklist du document 05 (02-Q4) | P |
| 08-Q6 | Nom et choix dans l'interface | Sélecteur « Piste / Sous-bus / Bus batterie » ; noms « Kick Bus », « Snare Bus », « HiHat Bus », « Toms Bus » | P |

## 12. Points à vérifier dans le code

Je n'ai pas vu le module Kit / Phase ni le mécanisme de partage entre instances : ces points sont des hypothèses.

- Le registre partagé permet-il de publier des **mesures** (pas seulement de l'audio horodaté) entre instances ?
- Une instance peut-elle **lire** les trims proposés par une autre (flux dans les deux sens) ?
- Le **mode Bus** actuel (« Drum Master Bus » vu sur vos captures) peut-il être généralisé en « Sous-bus » en gardant Glue et Ambiance pour le seul bus global ?
