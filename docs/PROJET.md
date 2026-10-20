# ZeroEffort Drum - suivi du projet

**Document de référence vivant · Mise à jour : 20 octobre 2026**

Ce fichier regroupe l'état du projet, les travaux restants, les problèmes connus,
les décisions et les repères techniques. Il remplace les anciens documents de
suivi : **pour reprendre le projet, commencer ici et mettre à jour ce fichier**.
Les détails des anciennes livraisons et les résultats chiffrés restent dans les
[archives](#archives), pas dans plusieurs feuilles de route concurrentes.

**Deux références à entretenir :** ce fichier pour l'avancement ;
[SPECIFICATIONS.md](SPECIFICATIONS.md) pour les exigences détaillées, les
critères de test et les décisions. Les anciens CodeUpdate et le plan produit
sont archivés, pas à relire à chaque reprise.

> **À retenir :** version **0.11.9**, analyse **6**, profils **4**. Le traitement
> par instrument, le radar Auto, Kit / Phase, les presets et les sous-bus avec
> balance sur validation sont implémentés. **La base musicale précédente a été
> validée par l'utilisateur sur Track 2 dans Cubase** ; son équilibre Auto est
> conservé. L'identité des presets est restaurée en 0.11.9 et **validée par
> l'utilisateur dans Cubase**. Suite du [plan ciblé](#plan-cible) : hi-hat, puis stabilisation.

## Sommaire

1. [Tableau de bord](#tableau-de-bord)
2. [Fait : fonctionnalités disponibles](#fait)
   - [Synthèse de l'audit des spécifications](#synthese-specs)
3. [Validation : acquis et preuves restantes](#validation)
4. [Problèmes et limites connus](#problemes)
5. [À faire : feuille de route](#a-faire)
   - [Plan ciblé retenu et ordre d'exécution](#plan-cible)
6. [Idées pour la suite](#idees)
7. [Versions et historique](#versions)
8. [Utilisation, compilation et installation](#utilisation)
9. [Corpus et outils de mesure](#corpus)
10. [Architecture et fichiers clés](#architecture)
11. [Décisions et entretien du suivi](#decisions)
12. [Sources et archives](#archives)

<a id="tableau-de-bord"></a>
## 1. Tableau de bord

| Repère | État au 20/10/2026 |
|---|---|
| Produit | Effet batterie **VST3 + ARA pour Cubase**, Windows x64, mono/stéréo ; pas de Standalone |
| Socle | C++17, JUCE 8.0.6, SDK ARA 2.2.0, CMake |
| Version | **0.11.9**, vérifiée dans [CMakeLists.txt](../CMakeLists.txt) |
| Versions internes | `analysisVersion = 6`, `profileVersion = 4`, vérifiées dans le code |
| État Git de référence | Branche `main`, dernier commit avant consolidation : `31391ec` (note de polarité des overheads) |
| Dernières livraisons | F17 : presets avec instrument/micro, Revert complet et invalidation des rapports Kit obsolètes |
| Tests automatiques | **Exécutés le 20/10 sur 0.11.9 : CTest 3/3**, Build and Test Release, VS2019 ; nouveaux tests bibliothèque/processeur/éditeur/Kit au vert |
| Couverture réelle | Deux familles d'essais kit réel **ignorées** (WAV non disponibles via `ZED_WAV_DIRECTORY`) ; essai utilisateur F17 confirmé, sans remplacer ces tests automatisés |
| Validation dans Cubase | Base musicale approuvée sur Track 2 ; identité des presets 0.11.9 testée et validée le 20/10 ; détection hi-hat réservée et généralisation non certifiée |
| Corpus | Hors dépôt, dernier inventaire documenté : 4 tracks ; données et résultats non réexaminés pendant cette consolidation |
| Travail en cours | F17 terminé, tests et validation utilisateur acquis ; prochain chantier : hi-hat (F18), puis stabilisation (F19). Aucun changement des règles Auto ou de l'équilibre Kick |

**Légende des statuts**

- **Fait** : implémentation présente et validation technique documentée ; ne signifie pas « écouté et approuvé ».
- **À valider** : fonction disponible, mais preuve d'écoute, d'hôte ou de généralisation encore manquante.
- **À faire** : travail identifié, pas livré.
- **Désactivé** : code présent, sans effet par défaut.
- **Idée** : piste non engagée ; aucune promesse de livraison.

<a id="fait"></a>
## 2. Fait : fonctionnalités disponibles

### 2.1 Traitement et interface

| Ensemble | Disponible |
|---|---|
| Chaîne manuelle | Gains entrée/sortie, bypass lissé, HP/LP, expander, EQ quatre bandes avec Q indépendants, compresseur large bande ou trois bandes |
| Kick | Focus, détection grave, hold/hystérésis, Bleed Clean, renfort de corps synthétique, punch et drive |
| Finitions | Crush parallèle, couleur et soft clip par voie différentielle suréchantillonnée 4x, Attack/Sustain, deux bandes d'EQ dynamique, corps mesuré Snare/Toms |
| Bus et ambiance | Glue liée en stéréo, sidechain HP, knee et réduction bornée ; room JUCE avec pré-délai, filtrage, caractère et densité |
| Affichage | Graphique EQ/filtres, spectre de sortie, réponse dynamique mesurée, indicateurs de traitement, compensation RMS A/B et écoute mono |
| Interface | Textes du plug-in en anglais ; documents et rapports développeur en français ; pages adaptées au rôle et à l'ordre du signal |
| Persistance | Réglages sauvegardés en insert et par modification ARA ; lecture des anciens formats de données testée |

**13 rôles**, sans reconnaissance automatique de l'instrument :

| Valeur enregistrée | Instrument |
|---|---|
| 0 / 1 | Kick / Snare |
| 2 / 3 / 4 / 5 | Tom 1 / Tom 2 / Tom 3 / Floor Tom |
| 6 / 7 / 8 / 9 | Hi-hat / Overheads / Room / Drum Master Bus |
| 10 / 11 / 12 | Kick Bus / Snare Bus / Toms Bus |

Kick Out et Snare Bottom sont des **positions de micro**, pas des rôles Auto
indépendants. Le menu distingue pistes et bus. Les sous-bus ont leurs propres
familles de presets et ne sont pas alignés comme des micros.

### 2.2 Auto, radar et presets

- Styles **Natural / Rock / Metal** ; nouvelle instance en **Rock, 75 %**.
  Les projets enregistrés conservent leurs choix. L'intensité n'est pas un
  dry/wet : sa dose augmente plus vite au-dessus de 50 % et atteint **1,75 à 100 %**.
- Analyse d'un événement ARA ou capture de **8 s en insert**, puis calibration
  et vérification par deux rendus du moteur.
- Radar à 12 axes : **source mesurée / cible estimée / résultat mesuré**.
  Dans l'éditeur, la proposition attend **Apply / Ignore** ; **Revert** restaure
  l'état précédent tant que le point de retour reste valide. Sans éditeur,
  le comportement d'application automatique historique reste distinct.
- La source du radar progresse pendant la capture insert. Les phases de rendu
  utilisent désormais des tranches de **22 ms par tick** : accélération livrée,
  mais pas encore de rendu sur un thread d'arrière-plan.
- Presets `.zedpreset` dans `%APPDATA%\ZeroEffort Drum\Presets\<famille>\` :
  enregistrer, charger, remplacer, supprimer vers la corbeille ; les quatre toms
  partagent leurs presets. Chargement annulable par Revert.
- Depuis **0.11.9**, un nouveau preset (format 2) conserve le **son, le rôle et
  la position de micro** : Kick In rétablit Kick In ; Tom 2 rétablit Tom 2, même
  depuis Floor Tom. Revert restaure aussi l'identité précédente.
- Groupe Kit, mode/force, retard/polarité, bypass, Mono et options Auto restent
  ceux de la session. Style et intensité du preset sont seulement indicatifs.
  Un changement d'identité invalide les résultats Kit concernés, pas les
  corrections déjà appliquées ; refaire l'analyse avant une nouvelle correction.
- Les anciens fichiers (format 1) restent lisibles et **gardent l'identité
  courante**. Choisir le rôle/micro voulu puis réenregistrer pour le mémoriser.
  Analyse, bilan et retour Auto ne constituent pas un historique persistant de session.

### 2.3 Analyse, mesures et outillage

| Jalon | Résultat |
|---|---|
| J1 / T2 | Snapshots JSON, restauration, rejeu, comparaison et export depuis WAV |
| J2 | RMS et crest actifs, plancher P10, densité de frappes, décroissance, tiers d'octave, bandes nommées, écart attaque/corps |
| T1 / T4 | Tests de propriétés, silence/invalides/écrêtage, niveaux, spectres, frappes, indépendance des blocs et fréquences d'échantillonnage |
| Calibration 01-Q1 | Seuil relatif actif **55 dB sous P95** ; terme **plancher +12 dB conservé** ; A09 ramené à 0,71 dB d'écart sur le cas synthétique |
| Registre de rôles | Distribution et finitions centralisées dans `RoleDescriptor` ; étape 2 de migration encore à faire |
| Brut / mixé | `--pair`, `--bus`, `--vs-plugin`, `--sub-sum`, orchestration du corpus et tableaux de résultats |
| Kit / Phase | Tests de défauts T3, mesure de diaphonie K04 et commande `--kit` avec détail des paires |

Repères de non-régression historiques : **540 rejeux** de snapshots identiques à
1e-6, **7 200 propositions** de tests de propriétés, **270 propositions figées**
pour le registre de rôles. Ces nombres décrivent leurs campagnes respectives,
pas le nombre de tests actuels ni une validation musicale.

### 2.4 Kit / Phase et sous-bus

- Registre multi-instances partagé par processus, **8 groupes de kit**, capture
  horodatée, analyse hors thread audio, corrections propres à chaque instance.
- Retard entier et polarité en tête de chaîne, transitions lissées ; les micros
  proches sont retardés vers les overheads, **sans latence déclarée ajoutée**.
  La Room n'est pas avancée.
- Modes mesure seule, suggestion et Auto ; aperçu, sélection des corrections,
  application, remise à zéro, réglage manuel au pas d'un échantillon et force.
- Rapports de cohérence avant/après, stéréo et Room. Depuis 0.11.8, une note
  explique le cas où la majorité des pistes sont inversées contre les overheads ;
  **elle ne change aucune correction** et n'inverse pas les overheads à votre place.
- Sous-bus : rapport des micros du même groupe et instrument sur l'audio récent :
  niveaux actifs, corrélation grave, gain de somme, polarité, retard résiduel,
  marge avant écrêtage. Les retards/polarités des instances sont pris en compte.
  La somme est reconstruite depuis les captures corrigées en phase : ce n'est
  pas une observation complète des faders et traitements du bus de l'hôte.
  Corrélation et gain de somme ne couvrent pas encore toutes les bandes prévues.
- **Measure balance / Apply trims / Undo trims** : proposition de balance
  uniquement hors plages plausibles, bornée à **±6 dB**, sans proposition sous
  0,5 dB. Chaque micro applique son propre gain d'entrée **après clic** ; Undo
  restaure le gain mémorisé. Refus si polarité, retard ou somme grave douteux.
- Pas de trim pour les toms faute de référence ; pas de chiffres inter-micros
  inventés avec un seul micro. Le sous-bus peut néanmoins traiter sa somme.

**Attention aux anciens numéros SB :** le journal appelle « SB3 » le profil de
groupe et « SB4 » les trims. Dans la [correspondance des jalons](SPECIFICATIONS.md#kit),
le profil correspond à SB4, les trims à SB5, le gate partagé à SB6 et les cibles
statistiques à SB7. Ce suivi utilise les **noms des fonctions** pour éviter l'ambiguïté.

### 2.5 Activations et options à ne pas confondre

| Fonction | État par défaut | Portée / réserve |
|---|---|---|
| J3 : exclusion des trames d'attaque du bleed | **Active** | Validée par l'utilisateur le 17/10 ; pas d'écoute dédiée documentée |
| J3 : crest actif pour activer le compresseur Kick | **Active** | Ne remplace pas tous les autres usages du crest global |
| J4a : doubles frappes | **Active** | Réarmement possible à 60 ms si montée ×4 ; distinct du détecteur Focus |
| Recalage des sauts de cycle OH | **Actif** | Ancien kit 4 : cohérence 59,5 → 96,6 ; à généraliser sur d'autres kits |
| `cymbalReference` | **Active pour essai** | EQ Hi-hat/OH en Rock/Metal ; Natural inchangé ; un seul ingénieur de référence |
| `subBusReference` | **Active pour essai** | EQ Kick Bus/Snare Bus en Rock/Metal ; pas de référence Toms Bus |
| Refus Kit hors fenêtre / diaphonie | **Désactivés**, non reliés à l'UI | Présents dans `KitPolicy`, accessibles aux essais `--guards` |
| Export JSON depuis le plug-in | **Désactivé en production** | Nécessite option CMake **et** variable d'environnement |

Valeurs vérifiées dans [DrumAnalysis.h](../Source/Drum/DrumAnalysis.h),
[DrumProposal.h](../Source/Drum/DrumProposal.h) et
[KitTypes.h](../Source/Kit/KitTypes.h). Activer un essai n'est pas valider sa qualité
sur tous les instruments et tous les morceaux.

<a id="synthese-specs"></a>
### 2.6 Synthèse de l'audit des anciens CodeUpdate

**Conclusion :** le plug-in, les profils heuristiques, la calibration par rendu
et l'outillage hors ligne sont présents. La chaîne complète **corpus diversifié
→ cibles statistiques → règles de presets généralisables** reste à terminer.
Cet audit a confronté les documents au code et aux tests écrits ; il ne constitue
ni une nouvelle exécution des tests, ni une nouvelle écoute.

| Ancienne source | Réellement présent | Reste ou limite importante |
|---|---|---|
| 01 · Mesures | Niveaux/crest actifs, spectres, décroissance, attaque/corps, Kit ; J3 et doubles frappes actifs | True-peak/LUFS, statistiques avancées par frappe, stéréo complète, résonances généralisées et mesures continues OH/Room manquent |
| 02 · Tests | A01-A11, propriétés principales, défauts Kit, rejeu, tests processeur et sous-bus | A12/A13, cas dorés musicaux, régression statistique complète et campagne CPU/mémoire restent ; certains tests réels peuvent être ignorés |
| 03 · Brut / mixé | Alignement, gain, cohérence, forme EQ, indicateur non linéaire, deltas simples ; régression bus de 1 à 4 entrées | Pas d'estimation complète seuil/ratio/temps/gate, stéréo, `paires.csv` ni agrégation des cibles par rôle/style/mixeur |
| 04 · Presets | Profils par rôle/style, réglages initiaux, puis seuils affinés par rendu du DSP | Pas de moteur général mesure/cible/dispersion, d'explication structurée complète ni de budget global de traitement |
| 05 · Décisions | Plusieurs choix appliqués : fenêtre active, WAV hors Git, export développeur contrôlé | Beaucoup de lignes restent des propositions/H, notamment collecte bêta, budgets, effectifs et méthode de cibles |
| 06 · Extensions | Registre de rôles et finitions centralisés ; positions Kick Out/Snare Bottom | Migration incomplète des bandes/détecteurs/UI ; nouveaux profils spécialisés non livrés |
| 07 · Prompts | Méthode de travail utile | Prompts désormais historiques ; ils demandent parfois de refaire des jalons déjà terminés |
| 08 · Sous-bus | Rôles Kick/Snare/Toms Bus, mesures simplifiées, EQ de groupe, balance avec Apply/Undo | Contributions/recouvrements par bande, ensemble exact S01-S08, gate lié et cibles statistiques restent incomplets |

**Corrections importantes par rapport aux anciens « à faire » :**

- Le corps Snare est déjà estimé dans 100-200 Hz sous condition ; cela ne livre
  pas tout le modèle tonal/résonant initialement prévu.
- Les seuils multibandes à **-35 dB** sont des valeurs initiales, **recalibrées**
  depuis les enveloppes du moteur après traitement amont. Les anciens risques
  R1/R2 ne prouvent donc pas un défaut final systématique.
- A08 teste maintenant le correctif de doubles frappes (48 attendues, tolérance
  10 %) et le sous-comptage de l'ancien mode ; A09 exige un écart **<1 dB**.
- L'intensité zéro et certaines monotonies ont des tests processeur/finitions ;
  les propriétés E03-E05 ne sont ni entièrement absentes ni entièrement couvertes.
- Les plages de trims Kick/Snare viennent de deux ingénieurs et restent
  provisoires ; elles ne remplacent pas des distributions validées sur corpus.

Exigences et identifiants conservés dans [SPECIFICATIONS.md](SPECIFICATIONS.md).
Il n'y a pas de pourcentage d'achèvement : une fonction codée, un test synthétique
et une validation musicale sont trois preuves différentes.

<a id="validation"></a>
## 3. Validation : acquis et preuves restantes

<a id="validation-musicale"></a>
### Validation utilisateur du 20 octobre 2026

- [x] **V08 · État musical actuel accepté sur Track 2 dans Cubase.**

L'utilisateur a largement essayé la version actuelle sur **Track 2**, morceau
qu'il connaît bien ; il est en relation directe avec l'ingénieur du son d'origine.
Les réglages Auto sont jugés cohérents et le résultat musical satisfaisant.
Aucun autre problème n'est signalé dans cet usage, hormis un **souci de détection
du hi-hat à caractériser** (P12).

**Décision validée : conserver l'équilibre sonore actuel.** Le Kick pourrait,
selon le goût de l'utilisateur, avoir un peu plus de gain et un renfort plus bas
(idée : environ 10 Hz sous la fondamentale estimée). Ce n'est pas une anomalie
retenue : aucun changement de gain, d'accord ou de dose Auto n'est engagé.
Une calibration plus générale attendra des kits de différents ingénieurs.

Cette acceptation musicale suffit pour poursuivre le projet : **pas de campagne
d'écoute exhaustive à refaire pour l'enregistrer**. Elle ne certifie pas chaque
opération de sauvegarde/export, chaque politique Kit ni tous les cas hôte.
Track 2 ayant déjà servi aux références, ce retour n'est pas une validation
statistique indépendante ni une suite de cas dorés automatisés.

Version du dépôt au moment de ce retour : **0.11.8** ; le retour porte sur la « version actuelle »
testée par l'utilisateur, sans nouvelle vérification du binaire chargé.
Aucun build, test automatique ou nouvel essai audio n'a été exécuté pour cette
validation documentaire ; les problèmes techniques connus restent ouverts.

### Historique des retours et portée

| Retour | Ce qu'il permet de conclure |
|---|---|
| 14/10, prototype 0.1.6 | Restitution audible, lecture depuis le milieu, boucles, sauvegarde/réouverture et export sur le projet de test de l'utilisateur |
| Chaîne manuelle / premiers Auto | Essais d'interface, réglages, application et restitution confirmés ; pas une certification de tous les profils ultérieurs |
| 17/10 | Export JSON Auto réel en ARA et progression du radar pendant la capture insert confirmés dans Cubase |
| 20/10, sous-bus | Premier essai de balance Snare Bus : refus constaté ; message amélioré en 0.11.7, pas preuve d'une campagne complète Apply/Undo |
| 20/10, polarités | Résultat du Track 2 reproduit hors ligne et cohérent avec le retour Cubase ; note explicative ajoutée en 0.11.8 |
| 20/10, validation utilisateur | Auto musicalement satisfaisant sur Track 2 dans Cubase ; état sonore accepté, hi-hat réservé ; voir V08 |
| 20/10, presets 0.11.9 | L'utilisateur confirme « testé et validé » après le correctif F17 et le parcours proposé de sauvegarde/chargement d'identité et Revert ; chantier accepté dans Cubase |

### Couverture technique et preuves restant à préciser

Cette liste conserve les vérifications non attestées individuellement ; elle ne
remet pas en cause V08 et ne bloque pas le plan ciblé. Les prochains essais seront
concentrés sur les fonctions modifiées.

- [ ] **V01 · Installation** : vérifier la version réellement chargée et l'absence
  d'un ancien build d'export ; copies ARA et VST3 cohérentes.
- [ ] **V02 · Auto et presets** : Apply, Ignore, Revert, annulation, changement de
  sélection ; sauvegarde/chargement/suppression des presets ; sauvegarde/réouverture.
- [ ] **V03 · Écoute comparée** : J3, J4a, recalage OH, références cymbales et
  sous-bus ; garder les réglages/source/style/intensité pour reproduire chaque essai.
- [ ] **V04 · Kit et balance** : Apply/Undo trims, corrections manuelles, groupes
  isolés, ASIO Guard, deux projets ouverts, insert et ARA multi-pistes.
- [ ] **V05 · Transport et export** : seeks, boucles, éditions ARA, queues de room,
  export et réouverture sur une copie du vrai projet.
- [ ] **V06 · Robustesse** : session dense, CPU, réactivité UI, automation,
  aliasing/couleur et lisibilité des textes longs.
- [ ] **V07 · Preuves musicales** : constituer des cas dorés validés par
  l'utilisateur et un jeu de validation indépendant du jeu de calibration.

Ces cases restent ouvertes pour leur périmètre complet tant que le résultat,
les conditions et la version testée ne sont pas consignés. V03 bénéficie de
l'acceptation musicale V08, sans preuve de comparaison isolée de chaque politique.
Ne pas extrapoler ce retour à une certification de tous les cas.

<a id="problemes"></a>
## 4. Problèmes et limites connus

| ID | Constat | Conséquence / prochaine vérification |
|---|---|---|
| P01 | WAV des anciens kits `raw2/raw3/raw4` signalés introuvables après réorganisation | Retrouver les prises synchronisées de référence ou les remplacer explicitement ; tests réels actuellement signalés SKIP |
| P02 | Biais du retard Kick d'environ **+0,05 ms** (2-3 échantillons) | Tolérance demandée ±1 échantillon non atteinte ; reprendre la corrélation derrière une option contrôlée |
| P03 | Hors fenêtre et forte diaphonie : fausse correction Kit encore possible avec les politiques par défaut | Garde-fous écrits mais désactivés ; calibrer puis décider leur activation et leur UI |
| P04 | Cible **Attack** du radar trop optimiste | Elle ajoute l'Impact nominal et ignore notamment compression/Crush ; ne pas la présenter comme une prédiction exacte |
| P05 | Détecteur de frappes commun encore orienté grave, seuil absolu sensible au bruit | Bruit rose à -54 dBFS : frappes fantômes constatées ; besoin de détecteurs par rôle |
| P06 | S01 absolue dépend de la FFT (+6 dB à 96 kHz dans le test historique) | Comparer les mesures relatives entre fréquences ; T60 extrapolé et H06 surtout pertinents Kick/Toms |
| P07 | Calibration/vérification Auto encore pilotées par le thread UI | Tranches de 22 ms livrées ; longues régions encore coûteuses, rendu d'arrière-plan non fait |
| P08 | Corpus insuffisant pour des cibles générales | 8 paires acceptées du même morceau/mixeur ne satisfont pas « 8 paires et 3 mixeurs » |
| P09 | Snapshots : proposition initiale, pas résultat final calibré ; politiques cymbales/sous-bus non enregistrées | Un rejeu n'est pas une preuve d'identité de toute la chaîne finale ; noter versions et politiques utilisées |
| P10 | Générateurs de référence signalés périmés après traduction anglaise | Ne pas régénérer aveuglément les références avec les anciens scripts ; contrôler avant utilisation |
| P11 | Automatisation Instrument : plage passée de 10 à 13 choix ; ancien changement de plage Output en 0.6 | États en valeurs conservés, mais vérifier les anciennes courbes normalisées dans l'hôte |
| P12 | Souci de détection hi-hat signalé par l'utilisateur sur Track 2 le 20/10 | Symptôme et passage à reproduire avant diagnostic ; lien possible avec P05/F07, pas une cause établie ; chantier ciblé F18 |

**Détail P04.** Sur deux Kicks en Rock 80 %, source Attack 9,8 / 11,1 dB,
cible 13,4 / 14,7, obtenu 4,8 / 2,6. L'axe mesure l'énergie **0-70 ms contre
70-230 ms**, pas le pic du clic. Décision documentée : attendre davantage de
corpus avant de changer le son. Une cible « non modélisée » quand nécessaire
est une option d'affichage à décider séparément d'une révision du compresseur.

**Limites de conception, pas fonctionnalités promises :**

- Pas de séparation de sources, reconnaissance automatique d'instrument,
  normalisation des vélocités ni garantie de préservation de toutes les ghost notes.
  Le renfort Kick est synthétique, pas une transposition de la prise.
- Aucun limiteur/true-peak/LUFS. Output manuel jusqu'à +12 dB ; soft clip et marge
  mesurée pendant Auto ne protègent pas toute autre section ni toutes les sommes.
  A/B rapproche le RMS, pas nécessairement la sonie perçue.
- Crossovers multibandes à phase minimale ; leur somme peut être plate en
  amplitude sans être transparente échantillon par échantillon.
- Cache ARA borné à **128 Mio / 128 régions par renderer**, sans streaming,
  resampling ni time-stretch. L'ancienne limite de **30 s a été supprimée en 0.4.2**.
  Une source éditée peut nécessiter une nouvelle préparation ; un cache invalide
  n'est pas rendu. Les queues restent bornées aux événements ARA.
- Groupe Kit manuel : risque de mélange entre projets actifs ; plug-in latent
  avant ZeroEffort ou déplacement de piste après mesure : résultat potentiellement
  obsolète. Rapports Kit non sauvegardés, corrections appliquées sauvegardées.
- Room : pas de vraie plate ; pré-délai modifié en lecture et resets peuvent
  interrompre la queue. Room Crush reste un choix de style, pas un défaut corrigé.

<a id="a-faire"></a>
## 5. À faire : feuille de route

<a id="plan-cible"></a>
### Plan ciblé retenu le 20 octobre 2026

**Exécuter dans cet ordre, sans retoucher l'équilibre musical accepté.**
Les étapes 1 et 2 sont terminées, **validation utilisateur dans Cubase comprise**.
Hi-hat et stabilisation restent planifiés.

| Étape | ID | Statut | Travail et critère de clôture |
|---|---|---|---|
| 1 · Valider la base | V08 | **Fait** | Retour utilisateur consigné sur Track 2 ; équilibre actuel conservé, Kick classé comme préférence artistique et hi-hat identifié comme réserve |
| 2 · Identité des presets | F17 | **Terminé et validé · 0.11.9** | Instrument/micro restaurés, affichage et Revert testés, formats 1/2 compatibles en lecture, Kit de session conservé et résultats obsolètes invalidés ; utilisateur satisfait après essai Cubase le 20/10 |
| 3 · Détection hi-hat | F18 | **À faire, prochain chantier** | Reproduire P12 sur Track 2, identifier le mécanisme, écrire un test puis corriger le cas ; vérifier l'absence de régression sur les autres instruments sans engager tout F07 |
| 4 · Stabilisation | F19 | **À faire après F18** | Tests ciblés puis trois suites CTest avec SKIP signalés ; valider le passage hi-hat et la non-régression des presets sur le build final ; validation F17 déjà acquise |

**Résultat F17 :** format de presets **2**, lecture du format **1** conservée ;
aucun changement d'ID de paramètre, des formats d'état hôte ou des versions
d'analyse/profil. Contrat détaillé dans [SPECIFICATIONS.md](SPECIFICATIONS.md#presets).
Les fichiers malformés sont refusés avec un message, sans application partielle.

**Preuves F17 :** 52 combinaisons rôle/position, lecture des anciens presets,
identité invalide, conservation des réglages de session, chargement/Revert,
libellés Kick In/Kick Out et Tom 2/Floor Tom dans l'éditeur ; invalidation Kit
après analyse terminée ou changement de position OH pendant une analyse.
Test de régression d'abord en échec sur le rapport Kit obsolète, puis corrigé.
Validation finale : `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/Build.ps1 -Action Verify`,
**3/3** ; les deux essais kit réel restent SKIP faute de WAV. VST3 0.11.9 compilé
localement, non installé automatiquement, aucune lecture audio lancée par l'assistant.
**Validation utilisateur reçue le 20/10 à 12:01 : « testé et validé, c'est bon ».**
Le correctif est accepté dans Cubase après proposition du parcours Kick In →
Kick Out → chargement → vérification de l'identité → Revert. F17 est clôturé ;
ce retour ne certifie pas tous les autres scénarios de la checklist technique.

**Périmètre F18 :** ne pas présumer que le problème est un mauvais comptage de
frappes, un refus Auto ou un réglage incorrect. Il faudra identifier le passage
et le symptôme exacts avant de choisir la correction. Si une évolution sonore
est nécessaire, la signaler et limiter l'essai au cas concerné.

**Après F19 :** élargir le corpus puis réévaluer les cibles avec plusieurs
ingénieurs. Le gain/renfort Kick, les nouveaux rôles, le gate partagé et le moteur
statistique général ne sont pas inclus dans cette mise à jour ciblée.

### Réserve de chantiers à plus long terme

**Priorités indicatives ; le plan ciblé ci-dessus fixe l'ordre immédiat.**
P1 = fiabiliser l'existant ; P2 = terminer les fondations ; P3 = extension.
Aucune date ni nouvelle activation n'est décidée ici.

| ID | Priorité | Chantier | Dépendance | Critère de clôture |
|---|---|---|---|---|
| F01 | P1 | Restaurer la couverture kit réel (P01) | WAV synchronisés + choix de référence | Tests concernés réellement exécutés, sans SKIP, référence identifiée |
| F02 | P1 | Compléter les preuves techniques Cubase V01-V06 au fil des changements ; acceptation musicale V08 acquise | Cas concernés + hôte et accord pour l'écoute | Résultats versionnés, anomalies reproductibles consignées ; ne pas refaire l'écoute générale déjà acceptée |
| F03 | P1 | Calibrer les garde-fous Kit et décider leur activation/UI (P03) | Plusieurs kits + écoute | Correction juste **ou refus explicite**, sans régression des kits ordinaires |
| F04 | P1 | Clarifier la cible Attack (P04) | Décision affichage vs changement sonore | Cible honnête ou modélisée et vérifiée ; modification sonore validée séparément |
| F05 | P2 | Corriger le biais Kick (P02) | Tests synthétiques + kits réels | Précision demandée ou refus fiable ; changement contrôlé |
| F06 | P2 | Registre de rôles, étape 2 | Références existantes | Migrer réglages des profils, bandes d'analyse, détecteur de rejeu et UI sans changer les sorties |
| F07 | P2 | J4b : détecteurs par rôle et analyse continue/stéréo OH/Room | Corpus adapté | Mesures O04/R01 et tests A12/A13 disponibles ; bruit/doubles/roulements couverts |
| F08 | P2 | Terminer la méthode brut / mixé | Nouvelles paires documentées | Compression équivalente, stéréo, `paires.csv`, agrégation rôle × style × mixeur ; M02/M03/M06/M07 |
| F09 | P2 | Calibrer les hypothèses H | Jeux de réglage et validation distincts | Seuils justifiés hors jeu d'apprentissage, décisions et effets versionnés |
| F10 | P2 | Cas dorés et traçabilité du rejeu (P09/P10) | Validation utilisateur | Références reproductibles, politiques explicites, générateurs compatibles |
| F11 | P2 | Réactivité du rendu Auto (P07) | Conception thread/cancellation | Interface réactive sur longues régions, résultats identiques, annulations sûres |
| F12 | P3 | Gate partagé entre micros d'un instrument (spec SB6) | Détecteur commun + essais d'hôte | Ouvertures cohérentes, pas de coupe des coups faibles, comportement validé |
| F13 | P3 | Cibles statistiques de sous-bus (spec SB7), références Toms | Corpus suffisant | Cibles avec dispersion et validation indépendante ; pas de valeurs inventées |
| F14 | P2 | Compléter les mesures de groupe et leur couverture | Mesures existantes + signaux connus | SB02/SB03 par bande, SB04/SB05 et tests S01-S08 vérifiés selon leur définition, pas seulement des tests approchants |
| F15 | P2 | Construire le moteur de presets fondé sur les cibles | F08/F09, mesures fiables et décisions validées | Cibles avec effectifs/dispersion, règles traçables par réglage, comparaison et validation indépendante avant activation |
| F16 | P2 | Mesurer le coût CPU/mémoire et le temps réel | Protocole et machine de référence | H01-H04 sur le trajet concerné, 1/8 instances, budget bloc 64 ; aucun résultat déduit du seul test d'allocation de push |

Hypothèses H encore à vérifier : seuil de compresseur Kick **10 dB**, plancher
**+12 dB**, doubles frappes **60 ms / ×4**, trame longue **400 ms**, cohérence
**0,8**, gain correctif **k = 0,5**, zone morte **1 dB**, refus diaphonie **-15 dB**,
rapport hors fenêtre **1,2**, tolérance OH **1,5 ms**, plages radar et seuils de
balance. Le seuil actif à 55 dB a été calibré sur un petit corpus : ce n'est pas
une preuve universelle.

**Prochaine session : F18, détection hi-hat sur Track 2.** F01 reste à traiter pour
rétablir la couverture automatisée réelle ; ne pas confondre cette dette avec
un refus de la validation musicale. Ne pas commencer plusieurs chantiers sonores
simultanément.

<a id="idees"></a>
## 6. Idées pour la suite

Ces pistes ne sont **ni livrées ni engagées** :

- Kick : un peu plus de gain ou renfort environ 10 Hz sous la fondamentale
  estimée, selon préférence artistique. **Décision du 20/10 : laisser le son
  actuel inchangé**, sans imposer cette idée aux presets Auto.
- Radar : comparaison A/B à l'écoute dans le panneau, spectres avant/après,
  explications au survol des réglages.
- Kit : corrélation détaillée par paire dans l'UI, rapports persistants, mode
  de latence explicite pour avancer des micros distants, masquage fréquentiel
  et conflits Kick/Toms, diagnostic global de l'équilibre.
- Instruments : traitement Auto distinct de Snare Bottom, Kick Sub et Hi-hat
  Bottom ; clarifier le rôle du Ride et des toms supplémentaires. Hi-hat Bus
  a été laissé de côté, pas implicitement promis.
- Styles : gate plus profond Rock/Metal, alternative Room sans Crush ; à décider
  musicalement, pas à généraliser depuis un seul mix agressif.
- Répartition du traitement micro / sous-bus / bus : budget partagé proposé
  **40/40/20** dans la spec, **hypothèse non implémentée**.
- Ergonomie : renommer le Makeup général multibande pour lever l'ambiguïté.
  Son fonctionnement est testé : il s'ajoute aux makeups de bandes quand le
  compresseur est actif, il n'est pas un gain global permanent.
- Distribution : plateforme supplémentaire, installateur, licences et métadonnées
  de publication à traiter comme un chantier distinct ; macOS non construit/testé.
- Analyse insert du morceau entier, courbes de gain par frappe préservant accents
  et ghost notes, couverture/exclusions explicites.
- Historique persistant du preset réellement appliqué et de son Bilan, distinct
  des options du prochain Auto.
- Lookahead avec latence déclarée, sidechain externe, palette manuelle étendue,
  vraie plate Snare, vues simples/avancées et recherche neuronale locale facultative.

Les intentions et garde-fous utiles de l'ancien plan sont conservés dans
[Évolutions produit non engagées](SPECIFICATIONS.md#futur). Ce ne sont pas des
fonctionnalités annoncées pour la prochaine version.

<a id="versions"></a>
## 7. Versions et historique

### 7.1 Trois numéros différents

| Numéro | Signification | Valeur |
|---|---|---|
| Version du plug-in | Livraison visible, build et compatibilité | **0.11.9** |
| `analysisVersion` | Mesures, constantes ou état d'analyse sérialisé | **6** |
| `profileVersion` | Règles qui construisent les propositions | **4** |

`analysisVersion` ne versionne **pas le cache ARA**. Le format d'un snapshot et
les formats de sauvegarde du plug-in ont leur propre logique. Ne pas baisser un
numéro pour annuler un comportement : préparer une nouvelle révision, documenter
le retour et comparer ses résultats selon les règles du chantier.

### 7.2 Livraisons récentes

| Version / date documentée | Contenu principal | Repère |
|---|---|---|
| 0.11.9 · 20/10 | F17 : presets son + identité, compatibilité format 1, Revert et rapports Kit invalidés après changement d'identité ; règles Auto inchangées | Build Release + CTest 3/3 ; validé par l'utilisateur dans Cubase ; 2 familles réelles SKIP |
| 0.11.8 · 20/10 | Note sur les polarités relatives aux OH ; `--kit` accepte le stéréo en lisant le canal gauche | `31391ec` |
| 0.11.7 · 20/10 | Refus de trims plus précis, niveaux lisibles ; test de portée du Makeup | `3d21911` |
| 0.11.6 · 20/10 | Balance de sous-bus proposée, Apply/Undo sur clic | `63b8f2e` |
| 0.11.5 · 20/10 | Rapport de groupe en lecture seule | `7851f1e` |
| 0.11.4 · 20/10 | EQ de référence Kick Bus/Snare Bus, `--sub-sum`, profils **4** | `34988ce` |
| 0.11.3 · 20/10 | Rôles et menu Kick Bus/Snare Bus/Toms Bus | `7a6f88f` |
| Sans incrément dédié · 20/10 | Accélération du radar par tranches de rendu | `0ad6ef7` |
| 0.11.2 · 19/10 | Référence cymbales activée pour essai, profils **3** | `3e222a4` |
| 0.11.1 · 18/10 | Presets utilisateur ; ajout ultérieur des indications style/intensité | `27d0597`, `981d1f9` |
| 0.11.0 · 17-18/10 | J4a activé, analyse **6** ; interface anglaise, radar ; défaut Rock 75 % ajouté sans nouvel incrément | Journal archivé |
| Chantier analyse v1 · 16-17/10 | J1/T1/T2/J2/J3/T3/T4, calibration, rejeu, registre de rôles ; versions analyse 1→5, profils 1→2 | Journal archivé |

### 7.3 Origines du produit

| Série | Apport | À ne pas prendre pour l'état actuel |
|---|---|---|
| 0.10 | Kit / Phase multi-instances, interface restructurée ; tag de base `v0.10.0` | Pas encore les sous-bus et options 0.11 |
| 0.9 | Finitions Auto, EQ dynamique, Impact, couleur/clip, A/B et Mono, visuels live | L'application automatique décrite à l'époque a changé dans l'éditeur |
| 0.8 | Instruments, Glue et room ; renommage en ZeroEffort Drum, retrait de Cubase Gain | Nouvelle identité hôte : pas de remplacement automatique des anciens prototypes |
| 0.7 | Compresseur trois bandes, calibration et indicateurs | Ancien nom de bundle et anciens tests |
| 0.6 | Q par bande, Output positif, onglets et spectre | Automatisation normalisée Output à vérifier |
| 0.5 | Focus, nettoyage aigu et présence Kick | Mesures/doses de cette version uniquement |
| 0.4 | Styles, intensité, calibration ; retrait de la limite ARA 30 s en 0.4.2 | Anciens réglages et limitations de sortie |
| 0.2-0.3 | Chaîne manuelle, Auto correctif et analyse temporelle | Anciennes cibles/fichiers |
| 0.1 | Intégration ARA, diagnostic et corrections des bus Cubase ; restitution validée en 0.1.6 | Prototype historique, ne pas recréer sa cible ni son option de build |

<a id="utilisation"></a>
## 8. Utilisation, compilation et installation

### 8.1 Parcours dans Cubase

1. Travailler sur une **copie de projet** et vérifier la version chargée.
2. Choisir le rôle et la position ; en ARA sélectionner les événements concernés.
   Les occurrences partageant une `AudioModification` partagent leurs réglages.
3. Choisir style/intensité, lancer **Analyze**. En insert, fournir 8 s de lecture ;
   en ARA, analyser l'événement sélectionné. Un changement de style demande
   une nouvelle analyse, il ne transforme pas instantanément le son.
4. Lire le radar et **Report**, puis **Apply** ou **Ignore**. Comparer au bypass
   à niveau proche ; Revert n'est pas un historique d'undo Cubase permanent.
5. Pour Kit / Phase : premier insert sur les micros, positions correctes, même
   groupe de kit ; mesure sur un passage joué, aperçu puis validation.
6. Pour un sous-bus : instance sur la vraie piste de groupe, rôle de bus correct,
   même groupe Kit que ses micros ; pendant la lecture, consulter Report puis
   Measure balance. Appliquer les trims seulement après vérification.

Une extension ARA sur un événement ne reçoit pas la somme du groupe de mixage :
utiliser un **insert sur le groupe** pour traiter un bus.

### 8.2 Compiler et tester

Depuis la racine, ou tâche VS Code **Build and Test Release** :

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Build.ps1 -Action Verify
```

[Build.ps1](../scripts/Build.ps1) utilise `windows-vs2019` et `build` si VS 2019
est installé, sinon `windows-vs2022` et `build-vs2022`. CMake portable est préparé
par [Bootstrap.ps1](../scripts/Bootstrap.ps1). Le premier configure récupère les
dépendances ; les suivants réutilisent le build.

Suites : **RegionCacheTests**, **ZeroEffortDrumTests**, **ARADiscoveryTests**.
F5 lance les tests Debug, pas une application audio. Pour les tests réels,
configurer `ZED_WAV_DIRECTORY` vers les prises synchronisées réellement présentes
(`Kick_12`, `Snare_12`, `OvH L_12`, `OvH R_12`) ; ne pas recopier aveuglément
l'ancien chemin `raw3`. Vérifier le résultat des tests, notamment les **SKIP**.

### 8.3 Installer manuellement

Bundle Release, selon l'outil utilisé :

```text
build\ZeroEffortDrum_artefacts\Release\VST3\ZeroEffort Drum.vst3
build-vs2022\ZeroEffortDrum_artefacts\Release\VST3\ZeroEffort Drum.vst3
```

Fermer Cubase, puis copier **le dossier `.vst3` entier**, pas seulement sa DLL,
dans le dossier VST3 scanné pour les inserts et, selon la procédure ARA de ce
projet Cubase, `C:\Program Files\Common Files\ARA`. Garder les deux copies à la
même version. Ne pas renommer un ancien bundle prototype pour le remplacer.
**Aucune copie système ni lecture audio automatique.**

L'export développeur exige `-DZED_ANALYSIS_EXPORT=ON` **et**
`ZED_ANALYSIS_EXPORT_DIR`. Utiliser un build séparé pour cet essai ; remettre
le build de production avec export désactivé après usage.

<a id="corpus"></a>
## 9. Corpus et outils de mesure

### 9.1 Dernier état documenté

Corpus hors dépôt : `C:\Users\robin\Desktop\Database_drum`, manifeste
`corpus.json`. Les anciens nombres de **32 / 37 / 38 snapshots** correspondent
à des campagnes différentes, pas à un inventaire actuel certifié.

| Données | Exploitation documentée au 19/10 |
|---|---|
| Track 1 · A.H., metal moderne | Samples ajoutés ; paires peu cohérentes, non retenues comme courbes ; utile pour doubles frappes/statistiques |
| Track 2 · A.H., post punk | **8 paires acceptées**, un morceau et un mixeur ; certaines combinaisons sont dépendantes ; toms trop courts |
| Track 3 · autre ingénieur | Bus Kick/Snare expliqués par régression multi-micros à **0,90 / 0,82** |
| Track 4 · autre ingénieur | Bus à **0,63 / 0,61**, non expliqués ; contenu manquant à clarifier, rôles des toms supplémentaires et Ride à confirmer |

Constats : Kick In globalement cohérent avec les deux sources ; choix de grave
divergents, donc pas de règle générale. Référence OH/Hi-hat plus proche du Track 2
après activation, mais **réglée et mesurée sur les mêmes données**. Snare Bottom
distincte et gate profond restent des pistes. Room : l'écart vient largement du
Crush, conservé comme choix de style.

Sous-bus : creux prudent à **280 Hz**, présence à **3 kHz** pour Kick/Snare en
Rock/Metal. Deux ingénieurs suggèrent la direction, mais une seule paire Snare
permet une évaluation chiffrée fiable : écart 2,8 → 1,8 dB. Les sommes du Track 3
ont une cohérence de paire de **0,55-0,59** : ne pas la confondre avec la cohérence
**multiple** 0,90/0,82 des bus.

### 9.2 Préparer un apport exploitable

- Bruts sans inserts et stems correspondants après inserts de piste, avant bus,
  master et effets envoyés ; mêmes noms, fréquence et début d'export de préférence.
- Au moins **20 s de jeu effectif par piste** ; une longue durée de fichier vide
  ne suffit pas. L'outil sait rechercher un décalage de départ.
- Pour un bus d'instrument, fournir **tous** les micros et samples contributifs.
  Pour isoler le bus batterie, paire `BUS_IN` / `BUS_OUT`.
- Documenter morceau, style, mixeur, samples, polarité/décalages manuels et
  compression parallèle. Les étiquettes décrivent le mix ; le script les lit
  actuellement **par entrée kit**. Plusieurs mixes peuvent partager les mêmes bruts.
- Rôles 0-12 selon le tableau ; position 0 principale, 1 secondaire, 2 gauche,
  3 droite. Ne pas inventer un rôle de snapshot pour Ride/Kick Sub/sample.
- Garder un jeu de validation indépendant. Règle de cibles générales : **8 paires
  et 3 mixeurs**, pas huit lignes corrélées issues d'une seule chanson.

### 9.3 Lancer et lire

Modèle : [corpus.example.json](corpus.example.json). Commande depuis la racine :

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\RunCorpus.ps1 -Corpus 'C:\Users\robin\Desktop\Database_drum'
```

Le script cherche par défaut l'exécutable Release dans `build`. Avec VS 2022,
ajouter `-Exe '.\build-vs2022\ZeroEffortDrumTests_artefacts\Release\ZeroEffortDrumTests.exe'`.
Autres options : `-Out`, `-SkipSnapshots`, `-SkipBuses`, `-SkipPairs`.
Les outils analysent/rendent des fichiers sans ouvrir de périphérique audio.

| Commande de `ZeroEffortDrumTests.exe` | Usage |
|---|---|
| `--export-wav` | Snapshot depuis un WAV, rôle/style/intensité et position |
| `--replay` / `--compare` | Rejeu des snapshots / comparaison de deux rapports |
| `--analyse-wav` | Analyse, calibration et vérification de la proposition |
| `--pair` | Comparaison brut/stem : alignement, courbe équivalente, cohérence, indices |
| `--bus` | Régression du bus sur plusieurs micros, pas une paire à un seul micro |
| `--vs-plugin` | Proposition du plug-in contre résultat de l'ingénieur |
| `--sub-sum` | Écrit une somme alignée `_sum.wav` et le bus correspondant `_mix.wav` |
| `--kit` | Retards, polarités et K04 ; `--detail`, `--guards`, `--no-reconcile` |

Dans `out` : commencer par `summary.md`, puis `reports`, `pairs`, `buses`,
`versus` et `snapshots`. [CorpusTables.py](../scripts/CorpusTables.py) affiche les
tables `pairs`, `versus`, `shapes` ou `buses`. Ne pas remplacer des données absentes
par zéro ; un refus ou une valeur indisponible fait partie du résultat.

<a id="architecture"></a>
## 10. Architecture et fichiers clés

| Responsabilité | Entrée |
|---|---|
| Processeur, session Auto, liaison insert/ARA | [ZeroEffortDrumProcessor.cpp](../Source/ARA/ZeroEffortDrumProcessor.cpp) |
| Moteur commun et paramètres | [DrumEngine.h](../Source/Drum/DrumEngine.h) |
| Mesures et constantes | [DrumAnalysis.h](../Source/Drum/DrumAnalysis.h) |
| Propositions, versions et politiques | [DrumAutoPreset.h](../Source/Drum/DrumAutoPreset.h), [DrumProposal.h](../Source/Drum/DrumProposal.h), [DrumRole.h](../Source/Drum/DrumRole.h), [Profiles](../Source/Drum/Profiles) |
| Radar et rendu de vérification | [DrumAutoRadar.h](../Source/Drum/DrumAutoRadar.h), [DrumPresetPreview.h](../Source/Drum/DrumPresetPreview.h), [AutoRadarPanel.h](../Source/UI/AutoRadarPanel.h) |
| Presets utilisateur | [DrumPresetLibrary.h](../Source/Drum/DrumPresetLibrary.h) |
| Réseau Kit et analyse | [KitRegistry.h](../Source/Kit/KitRegistry.h), [KitAnalysis.h](../Source/Kit/KitAnalysis.h), [KitTypes.h](../Source/Kit/KitTypes.h) |
| Alignement et balance | [KitAligner.h](../Source/Kit/KitAligner.h), [KitGroupMeasures.h](../Source/Kit/KitGroupMeasures.h) |
| Cache ARA | [RegionCache.h](../Source/ARA/RegionCache.h), [CacheValidity.h](../Source/ARA/CacheValidity.h) |
| Éditeur et apparence | [Source/UI](../Source/UI) |
| Snapshots et comparaisons | [DrumAnalysisSnapshot.h](../Source/Drum/DrumAnalysisSnapshot.h), [DrumReplay.h](../Source/Drum/DrumReplay.h), [DrumPairAnalysis.h](../Source/Drum/DrumPairAnalysis.h) |
| Tests et outils | [tests](../tests), [scripts](../scripts), [CMakePresets.json](../CMakePresets.json) |

**Règles structurantes :** le thread audio écrit uniquement dans son propre
`Member` et applique ses traitements/son `KitAligner` ; aucun verrou, allocation,
accès fichier ni journalisation. Le registre possède les slots, réutilisés sans
laisser de pointeur périmé. Le thread Kit possède ses sessions ; les résultats
sont publiés hors temps réel, chaque instance modifie ses propres paramètres.
Ne pas confondre ce thread Kit avec les passes Auto encore pilotées par l'UI.

La corrélation recherche la première arrivée, puis la polarité au retard trouvé,
pas à retard nul. Les overheads servent de référence : plusieurs micros
« inversés » ne prouvent pas plusieurs câbles défectueux. Le groupe Kit isole
manuellement les batteries/projets dans un même processus hôte.

<a id="decisions"></a>
## 11. Décisions et entretien du suivi

### Décisions conservées

- Pas de changement audible non annoncé ; options de comportement contrôlées
  jusqu'à écoute **ou validation explicite** de l'utilisateur. Ne jamais approuver
  un cas doré à sa place.
- Préserver IDs de paramètres et codes de plug-in ; ne pas recréer Cubase Gain,
  une cible Standalone ou le commutateur prototype supprimé.
- Corpus hors dépôt, pas de lecture audio sans accord ni copie automatique dans
  un dossier protégé. Aucun besoin de récupérer des données privées en ligne.
- Utiliser les trois suites et les outils existants, pas de cible `drumtest`
  supplémentaire. Séparer refactorisation et modification sonore.
- Les exigences sont réunies dans [SPECIFICATIONS.md](SPECIFICATIONS.md).
  Ce fichier conserve l'avancement ; la spécification conserve règles et
  critères. L'ordre de priorité historique y est expliqué sans transformer les
  anciennes propositions en décisions validées. Les prompts 07 sont archivés,
  pas un ordre de réimplémenter les jalons terminés.

### Comment garder un seul suivi propre

1. Mettre à jour la date et les versions **seulement quand elles changent**.
2. Pour chaque chantier, garder un ID stable (Fxx/Pxx/Vxx), son statut,
   son résultat attendu et sa dépendance. Une idée n'entre pas dans « Fait ».
3. Après implémentation, actualiser « Fait », le problème associé et, si nécessaire,
   la portée de l'exigence dans SPECIFICATIONS ;
   laisser les validations d'hôte ouvertes tant qu'elles ne sont pas réalisées.
4. Ajouter une ligne courte à l'historique : date, version, modification,
   effet audible/visible, validation exécutée et éventuels SKIP.
5. Consigner les décisions d'écoute et les rapports utiles avec leurs conditions.
   Ne pas réécrire une ancienne mesure comme si elle concernait la dernière version.
6. **Ne plus entretenir les archives ni créer un nouveau fichier état/plan/reprise
   pour chaque session.** Ajouter le prochain point de reprise ici.

Gabarit de mise à jour :

```text
Date / version :
Chantier (Fxx, Pxx ou Vxx) :
Résultat et statut :
Effet sur le son / l'interface / la compatibilité :
Validation exécutée, données utilisées et tests ignorés :
Décision utilisateur ou blocage restant :
Prochaine action :
```

### Point de reprise

**V08 consignée ; F17 terminé et validé dans Cubase en 0.11.9, sans changement des règles Auto.**
Reprendre le [plan ciblé](#plan-cible) à **F18 : détection hi-hat**, puis F19
(stabilisation). Ne pas redemander la validation F17 déjà reçue ; prévoir seulement
sa non-régression avec les prochains changements. Ne pas réouvrir la calibration
générale ni le Kick sans nouvelle décision. Vérifier versions et état Git avant le code.

Les limites à conserver visibles sont **les tests réels ignorés**, **les
garde-fous Kit désactivés** et **la différence entre cible radar et résultat
mesuré** ; la satisfaction sur Track 2 ne les clôt pas automatiquement.

<a id="archives"></a>
## 12. Sources et archives

Consolidation des **14 documents Markdown de `docs`, des 8 CodeUpdate et du plan
produit**, hors guide ingénieur son : **23 originaux conservés en archives**.
Le guide et son PDF sont exclus du travail et restent à leur emplacement.
Leurs contenus n'ont pas été utilisés. Le modèle JSON reste à la racine de `docs`.

Les archives conservent le texte historique, avec un bandeau de classement et
des liens ajustés au déplacement. Elles peuvent contenir des affirmations
contradictoires ou dépassées : les mentions « actuel », les commandes prototype,
les anciennes dates d'en-tête et les anciennes listes « à faire » y sont historiques.
Les captures de builds anciens peuvent ne plus être disponibles.

| Source archivée | Contenu conservé / intégré ici |
|---|---|
| [ETAT_ACTUEL.md](archives/ETAT_ACTUEL.md) | État général, activations, travaux manquants et limites d'écoute |
| [SUIVI_IMPLEMENTATION.md](archives/SUIVI_IMPLEMENTATION.md) | Journal détaillé, mesures, tests, décisions et dernières livraisons |
| [PLAN_IMPLEMENTATION.md](archives/PLAN_IMPLEMENTATION.md) | Audit initial, risques R1-R6, jalons et écarts aux specs |
| [REPRISE_PROCHAINE_SESSION.md](archives/REPRISE_PROCHAINE_SESSION.md) | Passation, décisions, commandes et pièges historiques |
| [ANALYSE_CORPUS_19OCT.md](archives/ANALYSE_CORPUS_19OCT.md) | Courbes et résultats du corpus, limites de preuve, cymbales et sous-bus |
| [Corpus.md](archives/Corpus.md) | Préparation, manifeste, outils et sorties |
| [KitPhase10.md](archives/KitPhase10.md) | Architecture détaillée, threading, algorithme Kit, utilisation et limites |
| [DrumInstruments08.md](archives/DrumInstruments08.md) | Instruments, finitions, paramètres, Glue/room, ARA et identité du produit |
| [KickMultiband07.md](archives/KickMultiband07.md) | Multibande, crossovers, calibration et mesures 0.7 |
| [KickInterface06.md](archives/KickInterface06.md) | Q indépendants, sortie positive, navigation et spectre 0.6 |
| [KickPresence05.md](archives/KickPresence05.md) | Détecteur Focus, nettoyage, renfort et mesures/limites 0.5 |
| [KickAuto04.md](archives/KickAuto04.md) | Styles, calibration, suppression de la limite 30 s et mesures 0.4 |
| [KickTest.md](archives/KickTest.md) | Chaîne manuelle, premiers Auto, sélection/persistance et tests 0.2-0.3 |
| [ARAPrototypeTest.md](archives/ARAPrototypeTest.md) | Intégration ARA, diagnostic Cubase et validations du prototype 0.1 |
| [01 - Mesures](archives/CodeUpdate/01_cahier_des_charges_mesures.md) | Catalogue et conventions repris dans SPECIFICATIONS |
| [02 - Programme de test](archives/CodeUpdate/02_programme_de_test.md) | Familles, critères et méthodes de preuve |
| [03 - Brut / mixé](archives/CodeUpdate/03_comparaison_brut_mixe.md) | Alignement, EQ/dynamique équivalentes, agrégation et fiabilité |
| [04 - Calcul des presets](archives/CodeUpdate/04_calcul_des_presets.md) | Modèle cible, fonctions, calibration et explications |
| [05 - Décisions proposées](archives/CodeUpdate/05_decisions_questions_ouvertes.md) | Arbitrages et hypothèses, sans validation implicite |
| [06 - Extensions](archives/CodeUpdate/06_extensions_futures.md) | Registre et procédure d'ajout de rôles |
| [07 - Prompts historiques](archives/CodeUpdate/07_prompt_copilot_vscode.md) | Méthode ancienne conservée ; ne plus l'utiliser comme plan de reprise |
| [08 - Sous-bus](archives/CodeUpdate/08_sous_bus.md) | Mesures, responsabilités, trims, jalons et tests de groupe |
| [plan.md](archives/plan.md) | Intentions produit, apprentissage long, courbes par frappe et historique prototype |

**Arbitrages de consolidation :** les versions et politiques actuelles ont été
vérifiées dans le code ; la chronologie récente dans Git. Les en-têtes datés du
17/10 mais enrichis jusqu'au 20/10 ne fixent plus l'état courant. Les sous-bus et
références cymbales ne sont plus listés comme « entièrement à faire/désactivés ».
Les essais ponctuels dans Cubase sont distingués d'une campagne complète.
Les mesures historiques sont conservées, pas recalculées ni présentées comme
une nouvelle validation.
