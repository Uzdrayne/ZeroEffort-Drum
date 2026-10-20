# Extensions futures : éléments de batterie à ajouter

<!-- consolidated-archive -->
> **Archive historique.** Référence consolidée : [SPECIFICATIONS](../../SPECIFICATIONS.md).
> Avancement vérifié : [PROJET](../../PROJET.md). Les états et chemins ci-dessous
> sont historiques ; ne plus entretenir ce document comme un suivi actif.
<!-- /consolidated-archive -->

**Document 6** | Version 0.1 | Complète les documents 01 à 05

**Interprétation de votre demande** : j'ai compris « hi-hat avec top et bottom » (deux micros) et, plus largement, les micros manquants (kick sub, caisse claire bottom, etc.). Si « tom et bottom » désignait autre chose, dites-le et j'ajuste la section 3.

---

## 1. Objectif

Pouvoir **ajouter un rôle ou un micro** sans réécrire le code : il suffit de le décrire dans un registre (mesures actives, fonctions actives, bandes d'EQ, relations de phase) et d'ajouter ses cibles et ses tests.

## 2. Architecture cible : le registre de rôles

Aujourd'hui, un rôle est une fonction (`KickProfile::propose`) avec ses propres règles. Cible : un **descripteur de rôle** (des données) lu par un moteur commun, qui calcule les réglages avec les règles du document 04.

```
RoleDescriptor
  id, famille (kick, snare, tom, hat, cymbal, overhead, room, bus, autre)
  micros[]            : variantes de micro (In, Out, Sub / Top, Bottom / ...)
  mesures_actives[]   : identifiants du document 01 (ex. L01, D02, H06, T01...)
  detecteur           : bande, période réfractaire, large bande ou non (H08, H09)
  fonctions[]         : filtres, expander, focus, EQ, compresseur, multibande, impact...
  eq_bandes[]         : noms affichés et plages de fréquence (Poids/Carton/Batte/Air, ...)
  plages_frequence    : corps, résonance, attaque
  relations_phase     : référence temporelle, polarité attendue, micros liés
  cibles_ref          : identifiant de la table de cibles (document 04, section 3)
```

Mise en œuvre progressive : les profils existants (Kick, Snare, Tom) continuent à fonctionner pendant qu'on introduit le registre ; un **test de non-régression** (document 02, cas dorés) garantit que les résultats ne bougent pas pendant la migration.

**État (17/10/2026, étape 1)** : `Source/Drum/DrumRole.h` contient la table `roleDescriptors` (un `RoleDescriptor` par `DrumInstrument` : famille, drapeaux proche / tom / cymbales / room / bus, corps mesuré, finitions Crush / Couleur / Impact, résonances dynamiques, Clip, Bleed Clean, Focus, Ambiance, et la fonction de proposition du profil). `DrumAnalyser` ne branche plus sur le rôle : ajouter un rôle = ajouter une ligne. Filet : `tests/RoleReferenceTests.cpp` (270 propositions gelées). Restent hors table : les réglages propres à chaque profil (`Profiles/*.h`), les bandes de mesure par rôle de `DrumAnalysis`, le détecteur Kick de `DrumReplay` et l'interface.

**Sous-bus par instrument** (Kick Bus pour In / Out / Sub, Snare Bus pour Top / Bottom, HiHat Bus…) : voir le document `08_sous_bus.md`. Il s'appuie sur le registre de rôles décrit ci-dessous.

## 3. Éléments à ajouter

Priorités : **P1** = prochain jalon, **P2** = ensuite, **P3** = plus tard.

| Élément | Prio | Particularités | Mesures clés | Fonctions actives | Relation de phase |
|---|---|---|---|---|---|
| **Snare Bottom** | P1 | Capte les timbres (ressort) ; souvent en polarité inversée par rapport au Top | O01 à O03 adaptés, T03 (bague) ; plage 3 à 10 kHz | Filtres, expander (fort bleed du kit), EQ ; pas de compresseur lourd | Référence : Snare Top ; polarité relative et retard Top/Bottom (K01, K02) |
| **Kick Sub** | P1 | Micro de grave (ou signal généré) ; 20 à 100 Hz | `corps_hz` bas (T01), glissement de pitch (T05), `fuite_grave` | Filtres (HP très bas, LP bas), expander, EQ (Sub / Poids), compresseur doux | Référence : Kick In ou Kick Out ; polarité et retard sont **critiques** dans le grave |
| **Hi-hat Top** | P2 | Fort bleed de caisse claire ; transitoires brillants | `cymbales_vs_corps`, `durete_db`, bleed caisse claire 150-400 Hz | Filtres (HP élevé), EQ (Présence / Brillance), expander léger ; **pas de drive** | Aligner sur les overheads (K01) |
| **Hi-hat Bottom** | P3 | Rare ; complète le Top | idem Top ; corrélation avec le Top | Filtres, EQ ; mélange avec le Top | Polarité vs Top |
| **Toms : Floor, Rack** | P2 | Le rôle « Tom » actuel devient des sous-types avec des plages de corps différentes | `corps_hz` par sous-type (Floor environ 60 à 120 Hz, Rack environ 100 à 250 Hz, à confirmer) ; boîte dépendante du tom | comme Tom aujourd'hui ; boîte par sous-type | Aligner sur les overheads |
| **Kick Out** | P2 | Micro extérieur : plus de timbre, moins d'attaque | mesures du kick, avec bande de corps déplacée | comme Kick | Référence : Kick In (déjà vu dans votre document Kit / Phase) |
| **Kick Sample / Trigger** | P3 | Signal d'un déclencheur ou sample de renfort | régularité des frappes (risque hybride, 04-Q5) | filtres, EQ ; pas de gate agressif | Alignement avec Kick In |
| **Ride, Crash (spot)** | P3 | Micros de cymbale proches | O01, O03, décroissance | Filtres, EQ ; pas de compresseur | Overheads |
| **Overhead mono, Room mono, Room lointaine** | P2 | Variantes d'overheads et de room | 5.10 du document 01, sans les mesures stéréo | comme OH / room | Overheads comme référence de temps |
| **Percussions, autres** | P3 | Hétérogène | mesures génériques seulement | EQ, filtres | À décider |
| **Bus parallèle** | P3 | Compresseur écrasé mélangé | `couleur_non_lineaire`, crest | compresseur, drive | Latence |

### Détails à noter pour la suite

- **Snare Bottom** : l'inversion de polarité devient un réglage de base, pas un cas particulier. Le module Kit / Phase doit pouvoir proposer « inverser » avec une confiance suffisante (document 04, 5.9).
- **Kick Sub** : retard et polarité sur le grave sont très sensibles. Le test D04 (retard + inversion) doit exister pour ce micro dès le premier jalon.
- **Hi-hat** : le détecteur grave du kick ne s'applique pas ; utiliser un détecteur large bande dans les aigus (H09 ou une variante).
- **Toms** : la fréquence de la boîte (aujourd'hui 450 Hz fixe) doit venir d'une mesure par tom.
- **Kit / Phase** : le groupe de rôles doit accepter **plusieurs micros par instrument** (Top / Bottom, In / Out / Sub) en gardant une seule référence de temps (les overheads).

## 4. Procédure d'ajout d'un élément

1. **Document 01** : lister les mesures actives du rôle (section 6) et en ajouter si besoin.
2. **Descripteur de rôle** : le renseigner (section 2).
3. **Corpus** : rassembler au moins 5 prises du rôle pour démarrer, puis 8 paires brut / mixé avant de publier des cibles (document 03).
4. **Cibles** : calculer les cibles par style (document 04, section 3), marquées « provisoire » en dessous du seuil.
5. **Tests** : défauts spécifiques (par exemple polarité Top / Bottom), cas dorés, propriétés E (document 02).
6. **Interface** : noms des bandes d'EQ, textes du Bilan, onglets visibles (par exemple Focus / Présence ou Nettoyage).
7. **Documentation** : fiche du rôle dans le document 04.
8. **Bêta** : recueillir des retours sur ce rôle précisément avant de le déclarer stable.

## 5. Ordre recommandé

1. **Registre de rôles** (sans changer le comportement), avec les cas dorés comme filet de sécurité.
2. **Snare Bottom** et **Kick Sub** : ils exercent Kit / Phase sur plusieurs micros par instrument.
3. **Hi-hat Top** : premier rôle avec un détecteur différent.
4. **Toms Floor / Rack** : sous-types, boîte par tom.
5. **Variantes overheads et room**.
6. Le reste, selon les demandes des bêta-testeurs.

## 6. Questions ouvertes

- **Q1** : la sélection du rôle se fait-elle uniquement dans l'interface, ou aussi par détection depuis le nom de piste (utile pour le bouton One Shot) ?
- **Q2** : un hi-hat avec seulement le micro Top doit-il fonctionner sans Bottom ? Oui par défaut : le Bottom est un plus.
- **Q3** : le nombre d'onglets visibles doit-il varier selon le rôle, ou rester uniforme (comme aujourd'hui, où Focus / Présence devient Nettoyage) ?
