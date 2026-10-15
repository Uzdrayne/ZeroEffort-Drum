# Plugin De Batterie Acoustique - Plan

## Feuille De Route Active - ZeroEffort Drum

Mise a jour : 2026-10-15. Produit actuel : ZeroEffort Drum 0.9.0,
C++17/JUCE 8.0.6, Windows x64, VST3 avec ARA, sans Standalone.
Guide courant : [DrumInstruments08.md](docs/DrumInstruments08.md).
Les sections numerotees plus bas conservent le cahier des charges historique :
elles ne constituent pas une liste de fonctionnalites actuellement livrees.

### Etat Actuel

- Dix roles explicites, styles Naturel/Rock/Metal, analyse d'un evenement
  ARA entier ou capture insert de 8 s ; pas de reconnaissance d'instrument.
- Filtres, expander, EQ, compression large bande/multibande, Focus/Presence
  specifique au Kick ; Glue stereo liee et ambiance manuelle pour le bus.
- Intensite douce jusqu'a 50 %, puis accentuee : ancien maximum a 70 %,
  force interne 1.75 a 100 %. Les reglages deja appliques ne changent pas.
- Invalidations ARA par region/source, plus par compteur global du document.
  L'utilisateur signale que les scenarios qu'il essaie fonctionnent et que
  le rendu batterie s'est ameliore ; ce n'est pas une certification exhaustive.
- Slider lineaire contraste ; ambiance dose 0-100 %, taille elargie.
- Livraison 0.9 : pages Impact/Precision, Crush, couleur et soft clip 4x,
  Attack/Sustain, EQ dynamique deux bandes, corps mesure optionnel, room
  caractere/densite, A/B RMS et mono/correlation. Modules OFF au chargement initial.
- Suivi Auto/live : Analyser applique maintenant le resultat calibre et
  verifie, avec Revenir. Finitions selectionnees par role/style/mesures et
  dosees par Intensite ; a zero OFF. Les anciens etats ne sont pas reecrits.
- Courbe dynamique ambre animee par les reductions mesurees, seize indicateurs
  d'activite/unites/OFF/BYP dans la zone haute. Meters ARA par modification
  selectionnee ; spectre ARA encore partage. Output est le gain final.
- Limites : pas de vraie plate, de limiteur de cretes ni de coordination
  inter-micros. CPU, aliasing et qualite musicale restent a valider.
- Dernier build : trois suites CTest passent, capture graphique native
  controlee. Les tests DSP ne remplacent pas les ecoutes dans Cubase.

### Priorite Zero - Retour Kick Apres La Derniere Mise A Jour

Retour utilisateur : "depuis la derniere maj ca ne va pas", avec capture et
Bilan Kick Naturel 81 %. Ne pas ajouter des effets pour masquer une regression
non identifiee, ni conclure que l'audio est correct a partir du seul Bilan.

Clarification utilisateur : il etait auparavant en Rock/Metal, maintenant
en Naturel. Le changement de style explique l'EQ neutre et les reglages plus
doux du rapport fourni. Rechoisir le style precedent puis Analyser -> Appliquer
avant de conclure a une regression sonore.
Confirmation utilisateur : "en style rock c'est bon". Le probleme signale est
resolu par le retour en Rock ; aucun correctif DSP requis pour ce retour.
En 0.8 les options Auto n'etaient pas persistees, contrairement au rendu.
En 0.9 style/intensite sont sauvegardes en insert et par modification ARA.

- [x] Persister style/intensite Auto ; tests insert/archive passent.
- [ ] Persister une provenance distincte du preset applique et son Bilan,
  pas seulement les options du prochain Auto. Confirmer le cycle dans Cubase.

Constats verifies dans le code et le rapport fourni :

- Naturel est selectionne, pas Rock. Ce profil ne force pas de coloration
  EQ : gains a 0 et EQ OFF sont attendus si aucune correction fiable n'est
  detectee. Monter l'intensite ne transforme pas Naturel en Rock.
- A 81 %, force interne = 0.5 + 2.5 * (0.81 - 0.5) = 1.275.
  Cible Naturel = 1.5 * 1.275 = 1.9125 dB ; plafond = 3 * 1.275 = 3.825 dB.
  Ces valeurs correspondent au Bilan ; pas de preuve de calcul incoherent.
- COMP est actif : P75 rendu 2.1 dB, maximum 3.8 dB ; EXP maximum 7.6 dB,
  actif au-dessus de 1 dB sur 30.4 % des blocs audibles.
- Le rendu verifie est a -0.7 dB RMS relatif, makeup 0 et sortie 0.
  La compensation actuelle ne pousse pas le makeup pour un mix a 70 % ;
  l'A/B n'est donc pas parfaitement egalise. Ce n'est pas encore une mesure
  independante du signal effectivement restitue dans Cubase.
- Les 741 candidats et 285 declenchements Focus ne sont pas un comptage
  certifie des frappes : pas de conclusion sur des coups perdus sans ecoute.

Actions avant de modifier le DSP :

- [x] Preciser le symptome : changement Rock/Metal vers Naturel confirme.
- [ ] Comparer le meme evenement, le meme style et la meme intensite,
  avant/apres avec niveau rapproche et en contexte avec les overheads.
- [x] Revenir au style Rock : resultat satisfaisant confirme par l'utilisateur.
  Changer de style seul ne change pas les reglages appliques.
- Si un autre probleme audible apparait, conserver brut/traite du meme
  passage et Bilan pour le reproduire avant correction ; ne pas forcer une EQ
  dans Naturel pour masquer une confusion de style.

### Objectif Du Prochain Palier

Plus de poids, d'impact, de profondeur et de coherence DANS le morceau,
a niveau comparable, sans cymbales fatigantes ni disparition des nuances.
Il s'agit de mixage et de finition du bus batterie, pas du mastering complet
du morceau. Pas de cible LUFS arbitraire ni de promesse de son universel.
Direction artistique a choisir avec l'utilisateur : rock massif et vivant,
metal serre/incisif ou gros son naturel ; demander une ou deux references.

### To-Do Par Priorite

1. [x] A/B RMS manuel livre ; pas une certification perceptuelle ni LUFS.
  Objectif : comparaison rapide original/traite,
   mesure explicite, marge de crete preservee. La calibration RMS actuelle
   aide mais n'est pas une comparaison perceptuelle integree.
2. [x] Voie parallele Crush manuelle independante de la Glue : compression forte,
   filtrage et coloration propres, dose de retour ; conserver le direct.
  Avant 0.9, le mix parallele n'avait pas de seconde voie independante.
  Verifier aussi les relations de phase dans le mix multi-micros reel.
3. [x] Couleur manuelle 4x livree ; aliasing/CPU restent a mesurer.
  Objectif saturation Snare/Toms/Bus : grain, corps et presence sur petites
   enceintes ; drive/mix, compensation de gain et surechantillonnage.
   Mesurer aliasing, cretes et latence ; ne pas copier simplement le drive
   grave Kick sur tous les instruments. OFF par defaut.
4. [x] Attack/Sustain manuel lie stereo livre, sans classification instrument.
  Objectif par instrument : renforcer le claquant ou ajuster
   la queue sans fermeture brutale du gate ; proteger ghost notes, doubles
   frappes, roulements et longues decroissances des toms.
5. [x] EQ dynamique manuelle deux bandes livree : retenir les cymbales agressives seulement quand
   elles debordent et traiter les resonances intermittentes sans retirer
   continuellement le corps. Frequences stables, reductions bornees.
6. [ ] Partiel : corps mesure optionnel Snare/Toms livre, mais plage <=200 Hz
  et pas de nouvelle proposition automatique de resonances intermittentes.
  Objectif profils Snare/Toms davantage mesures : corps/resonances proposes
   depuis la prise et avec confiance, plutot que des frequences surtout
   predefinies ; distinguer choix de style et correction fiable.
7. [ ] Partiel : room caractere et retour compresse livres, vraie plate absente.
  Objectif ambiance travaillee : room courte de cohesion, plate Snare et retour
   compresse creatif. Pour cibler la Snare seule, utiliser sa piste ou un
   envoi dedie : le bus somme ne permet pas de l'isoler. Preserver le direct,
   controler les queues a l'export et ne pas augmenter seulement la dose.
8. [ ] Partiel : coloration soft clip differentielle 4x livree, sans plafond
  garanti ni validation spectrale. Objectif soft clipper : arrondir quelques cretes
   isolees, surechantillonnage et compensation testes. Derniere etape,
   OFF par defaut ; pas une garantie true-peak ou un remede au mauvais mix.

### Verification Multi-Micros

- [ ] Ecouter Kick/Snare avec les overheads en stereo puis en mono,
  verifier polarite et equilibre avant d'ajouter compression ou renfort.
- [ ] Traiter OvH L/R comme une paire : reglages coherents et dynamique
  liee. Deux instances mono analysees independamment ne garantissent pas
  la stabilite de l'image ; definir un workflow stereo ou une coordination
  explicite, pas une liaison entre pistes supposee deja disponible.
- [ ] Ne pas appliquer un alignement temporel automatique aveugle : les
  differences acoustiques participent a l'espace du kit. Un bus somme
  ne mesure ni les niveaux individuels ni les relations entre tous les micros.

### Lots De Livraison Proposes

1. Reference : lever le doute Kick, choisir couplet/refrain charge/fill,
   conserver brut, rendu actuel et mix complet, comparer a niveau proche.
2. Impact : A/B compense + Crush + saturation. Premier lot recommande,
   complementaire a la Glue plutot qu'une hausse globale de compression.
3. Precision : Attack/Sustain, profils mesures et EQ dynamique.
4. Finition : ambiance amelioree, verification mono/stereo et clipping
   facultatif ; le controle multi-micros manuel commence des le premier lot.

Implementation autorisee ensuite explicitement par l'utilisateur. Premiere
livraison 0.9 manuelle : un module a la fois, bypass independant, tests puis
ecoute utilisateur avant de generaliser Auto. Les cases partielles ci-dessus
restent ouvertes ; ce n'est pas l'achevement de tous les objectifs musicaux.

### Criteres De Validation

- [ ] Comparaisons musicales en solo ET dans le mix, a niveau rapproche ;
  tester frappes faibles/fortes, passages denses, fills et pauses.
- [ ] Signal fini et niveaux bornes a 44.1/48/96 kHz, mono/stereo ; reset,
  bypass, changements de parametres et cout CPU verifies.
- [ ] Chemin direct/parallele aligne ; toute latence declaree a Cubase,
  sans allocation, verrou, acces fichier ni log dans le callback audio.
- [ ] Etats insert/ARA, sauvegarde/reouverture, seeks/boucles et exports
  avec queues controles. Pas d'installation systeme ou lecture automatique.
- [x] Build and Test Release 0.9.0 Auto/live : trois suites passent (environ 35 s),
  captures natives et test du mouvement de la courbe dynamique inclus,
  RegionCacheTests, ZeroEffortDrumTests,
  ARADiscoveryTests ; tests cibles pour chaque changement sonore.

### References Consultees

- Hugh Robjohns, Sound On Sound, Parallel Compression (2013) :
  https://www.soundonsound.com/techniques/parallel-compression
  et https://www.soundonsound.com/techniques/parallel-compression?page=2
  Densite et traitement parallele sans confondre gain et preservation
  des transitoires ; le resultat depend du reglage, pas seulement du mix.
- Paul White, Sound On Sound, Mixing Multitracked Drums (2001) :
  https://www.soundonsound.com/techniques/mixing-multitracked-drums
  Relations micros/overheads, enveloppes et contexte musical. Les conseils
  de latence numerique anciens doivent etre adaptes aux DAW modernes.
- Les choix Crush/saturation/EQ dynamique et l'ordre des lots sont notre
  proposition de produit, pas une prescription universelle des articles.

## Cahier Des Charges Historique

## 1. Statut Et Objectif

- Cahier des charges de reference, ouvert a discussion. Le gain existant est
  conserve ; integration ARA 0.1.6 validee manuellement, premiere chaine Kick
  et interface 0.2.0 validees par l'utilisateur (commandes, meters, etat).
  Auto preset prudent 0.3.0 analyse/applique dans Cubase (captures utilisateur).
  Detection temporelle 0.3.1 testee synthetiquement et sur WAV utilisateur ;
  resultat EQ neutre sur cette prise, jugement musical dans Cubase encore requis.
  Evolution 0.4.0 : intentions Naturel/Rock/Metal et intensite, calibration de
  reduction maximale via rendu DSP puis ajustement sortie RMS/cretes. Tests
  et mesures sur le WAV utilisateur reussis, ecoute dans l'hote requise.
- Premiere livraison Auto : un evenement ARA non etire (2 s a 1 h) analyse
  incrementalement, ou capture insert fixe de 8 s ; proposition globale,
  application explicite et retour temporaire protege. Pas encore l'apprentissage
  complet decrit ci-dessous : fin manuelle insert, correction par frappe,
  comparaison de volume perceptuel et lecture ARA longue restent a implementer.
- Base existante : C++17, JUCE 8.0.6, Windows x64, VST3 et Standalone.
- Hote cible confirme : Cubase Pro 12.0.52, build 393, sous Windows.
- Direction retenue : moteur commun, VST3 classique en insert et integration
  ARA 2 pour les evenements audio, sous reserve d'un prototype valide dans cet hote.
- Compilation Release et tests existants reussis. Chargement, gain, automation
  et restauration des reglages confirmes par l'utilisateur dans Cubase.
- Objectif : un plugin unique pour nettoyer et modeler les pistes de batterie
  acoustique, ainsi que finir le bus complet, sans detruire les nuances.
- L'assistant prend en charge implementation, tests et documentation. Les
  choix encore ouverts ont des valeurs par defaut pour permettre d'avancer.
- Priorite au mixage de batterie : lisibilite, naturel et coherence du kit.
  Le Drum Bus assure une finition du kit, pas un mastering complet du morceau.
- Ne pas exiger de l'utilisateur des decisions DSP ou SDK. Documenter les choix
  techniques et n'escalader que les autorisations ou blocages reels.
- Un resultat musical universel ne peut pas etre garanti sans prises reelles
  variees et ecoutes comparatives. Les tests synthetiques ne les remplacent pas.

## 2. Experience Principale

1. Selectionner la source : Kick, Snare, Tom, Hi-hat, Overheads, Room, Drum Bus.
2. Selectionner une intention : Naturel, Rock, Metal, avec Naturel par defaut.
3. Choisir traitement manuel ou Apprendre. Ce nom est distinct du Write de Cubase.
4. En ARA, analyser les regions exposees sans imposer une lecture temps reel.
  En insert classique, lire le morceau entier une premiere fois, puis terminer
  explicitement l'apprentissage. Une analyse de selection reste possible.
5. Examiner les propositions globales et les corrections temporelles, leur
  confiance, la couverture analysee et les passages ambigus.
6. Appliquer tout ou certains modules et regler l'intensite des corrections.
7. Reecouter ou exporter : corrections internes synchronisees, sans nouvelle
  analyse automatique. Comparer avec l'original a niveau comparable.

L'analyse ne modifie pas le traitement pendant sa capture. Les propositions
comprennent des reglages de base et, si activees, des corrections temporelles.
Les detecteurs gate/compresseur/EQ dynamique restent reactifs au signal ; cela
n'implique pas de recalculer continuellement toutes leurs valeurs de reglage.
Annuler l'application doit restaurer les valeurs precedentes.
Changer de source ou de style ne doit pas ecraser silencieusement un mix existant.

## 3. Acces Au Son Et Anticipation

- Un effet VST3 standard recoit les blocs fournis par l'hote ; il ne peut pas
  demander arbitrairement le contenu futur ou le fichier de la piste Cubase.
- Cubase Pro 12 prend en charge ARA 2 ; la compatibilite de notre extension avec
  12.0.52 doit etre verifiee, elle n'est pas encore acquise.
- ARA expose des sources et regions audio selon l'integration de l'hote, pas
  necessairement le signal apres les inserts, faders ou sommation du mixeur.
- Extension ARA pour les evenements ; insert VST3 classique pour le Drum Bus,
  le monitoring et l'analyse du signal effectivement recu pendant la lecture.
- Prototype ARA prioritaire : acces audio autorise, placement des regions,
  restitution, sauvegarde/restauration et notifications de modification.
- Verifier SDK ARA, licences, integration JUCE 8.0.6 et toolchain VS2019 avant
  de retenir les dependances. Le support ARA de l'hote ne suffit pas a lui seul.
- Si le prototype echoue, poursuivre le VST3 avec apprentissage en deux passes.
  Une application d'analyse de fichiers reste un recours, pas un livrable impose.
- ARA ne garantit ni acces a toutes les pistes ni creation d'automations Cubase.
  L'analyse multi-micros et l'alignement automatique ne sont pas promis en V1.
- Capture VST3 : demarrage, pause, fin explicite, annulation et relance. Ne pas
  deviner la fin de chanson a partir d'un silence ou d'un simple arret du transport.
- Import WAV ulterieur possible ; le fichier ne represente pas necessairement
  le signal recu par l'insert dans Cubase.
- Lookahead du gate : retard audio court et fixe par mode, candidat 0 a 5 ms,
  pour laisser le detecteur agir avant l'arrivee de l'attaque retardee.
- Declarer toute latence a Cubase. Eviter les changements de latence en lecture.
  Prevoir un mode sans lookahead pour le monitoring.
- La compensation de latence aligne les pistes ; elle ne corrige pas les
  differences de phase acoustiques entre microphones.
- Aucun acces aux autres pistes sans routage explicite. Pas d'alignement
  automatique du kit promis dans cette version.

## 4. Analyse Et Recommandations

### Gate Et Expander

- Mesurer l'enveloppe, detecter des attaques et analyser les intervalles entre
  frappes en distinguant repisse, bruit, queues de resonance et coups faibles.
- Ne pas assimiler automatiquement un passage faible a du silence.
- Employer un detecteur filtre par source, une estimation robuste du bruit et
  une estimation du niveau des frappes, plutot qu'une moyenne globale seule.
- Proposer seuil d'ouverture, hysteresis de fermeture, attenuation maximale,
  attack, hold et release. Preferer un expander doux au silence force.
- Refuser une proposition agressive si bruit et coups faibles se chevauchent.
  Afficher une confiance faible et conserver un reglage conservateur.
- Tester en particulier ghost notes, roulements, flam, grosses queues de toms
  et repisse de cymbales. Le lookahead protege l'attaque, pas leur classification.
- Les overheads, room et bus ne recoivent pas de gate par defaut.

### Spectre Et EQ

- Analyser plusieurs frappes et leur evolution dans le temps.
- Estimer la resonance grave dominante des kicks et toms avec confiance ;
  ne pas promettre une note fondamentale unique ou stable.
- Distinguer energie utile, resonance persistante et bruit autant que possible.
- Proposer des corrections limitees ; ne pas retirer le corps sous pretexte
  qu'une frequence est dominante. Pas de boosts importants automatiques.
- Proposer les filtres de nettoyage en respectant le registre de l'instrument.
- Une piste isolee ne permet pas de garantir la coherence avec les overheads.

### Dynamique

- Mesurer cretes, niveau RMS, facteur de crete, duree des attaques, decroissance
  et espacement des frappes. Exploiter le tempo de l'hote seulement si disponible.
- Proposer seuil, ratio, knee, attack et release selon source et style.
- Ne pas deduire le compresseur de la seule resonance du kick.
- Limiter la compression automatique ; verifier la reduction sur plusieurs
  frappes. Ne pas normaliser sur une seule crete exceptionnelle.
- Sur le bus, employer une detection stereo liee et un filtre de sidechain
  interne pour reduire le pompage provoque par le grave.

### Regularite Des Frappes Et Courbes Internes

- Detecter les frappes et leur contexte local/section avant de proposer une
  correction de gain ; ne pas egaliser toute la chanson sur un niveau unique.
- Preserver accents, ghost notes, crescendos et changements d'intensite.
- Correction avant compression, fenetres lissees preservant attaque et queue.
  Regrouper ou ignorer les evenements qui se chevauchent de facon ambigue.
- Intensite continue de 0 a 100 %, bornes initiales conservatrices de +/-3 dB
  pour la correction par frappe, ajustables seulement dans les reglages avances.
- Ne pas amplifier une frappe incertaine ni sa repisse. Desactive sur le bus
  et les overheads par defaut. Autoriser exclusions et desactivation par passage.
- Conserver une liste compacte d'evenements et courbes, pas des valeurs pour
  chaque echantillon. Memoriser domaine temporel, couverture et version d'analyse.
- Insert classique : courbes ancrees au temps absolu de projet, sans supposer
  qu'elles suivent un evenement audio deplace. ARA : utiliser les mappings de
  regions fournis par l'hote et invalider les portions affectees si necessaire.
- Gerer seeks, boucles, lecture au milieu, changements de tempo et sample rate.
  Si la synchronisation requise est absente, suspendre les courbes et le signaler.
- Hors couverture ou analyse perimee : traitement de base uniquement, pas
  d'extrapolation de correction. Capture discontinue signalee, pas comblee.
- Ne pas garantir la detection des edits Cubase par un insert standard. Prevoir
  une commande de reanalyse et afficher l'origine/la portee de l'apprentissage.
- Les courbes ne reecrivent pas l'APVTS a chaque bloc. Parametres automatises
  par l'hote pour les reglages de base ; corrections internes bornees/lissees,
  avec une regle de combinaison documentee et un controle d'intensite a zero.
- Pas de promesse d'ecriture dans les lanes Write de Cubase. Sauvegarder les
  courbes avec le projet ; offrir comparaison et annulation sans traitement destructif.

### Resultats

- Afficher duree analysee, nombre de frappes estime, mesures utiles et confiance.
- Distinguer capture terminee et donnees suffisantes pour une recommandation.
- Si l'analyse echoue ou manque de donnees, ne pas remplacer les reglages.
- Conserver les reglages appliques dans le projet ; la capture brute n'est pas
  necessaire a la restauration et n'est pas stockee par defaut.
- Sauvegarder aussi courbes, couverture, confiance et provenance. Borner la
  taille de l'etat et prevoir migration, donnees absentes et etat corrompu.

## 5. Chaine Audio Et Modules

Ordre initial pour micros proches : gain d'entree -> filtres -> expander ->
correction de gain par frappe -> EQ -> dynamique -> saturation -> gain de sortie.
Le detecteur du gate observe le signal avant attenuation et correction par frappe.
Le compresseur large bande est le mode de dynamique par defaut ; le multibande
est un mode alternatif, pas deux compressions obligatoires en serie.
EQ avant/apres compresseur : choix prevu, avec changement sans discontinuite.

### Socle Manuel

- Gain d'entree et de sortie independants, indicateurs de cretes et ecretage.
- Passe-haut et passe-bas avec frequences et pentes selectionnables.
- EQ a quatre bandes : grave et aigu configurables shelf/bell, deux bells
  parametriques, frequence/gain/Q et bypass par bande.
- Gate/expander : seuil, hysteresis, range, ratio d'expansion, attack, hold,
  release, filtre de detection et ecoute du detecteur.
- Compresseur : seuil, ratio, knee, attack, release, makeup manuel, mix parallele,
  filtre de detection et affichage de reduction de gain.
- Bypass par module et global sans clic ; comportement du bypass hote teste.
- Stereo liee pour la dynamique, sans mouvement involontaire de l'image.
- Mode mono et stereo ; pas de conversion ou de sommation implicite.

### Traitement Adaptatif Cible

- EQ dynamique : jusqu'a deux bandes pour resonances intermittentes, avec
  frequence/Q de base stables, reduction bornee, seuil, attack/release et meters.
- Compresseur multibande optionnel : trois bandes, crossovers fixes apres
  analyse, compression stereo liee et reglages/mesures par bande.
- Construire la separation avec des filtres eprouves ; tester recombinaison,
  reponse et phase. Choix initial minimum-phase pour limiter la latence ; ne pas
  promettre une recombinaison identique en phase au signal non filtre.
- Eviter les crossovers mobiles par frappe. Les seuils/enveloppes peuvent agir
  dynamiquement sans deplacer continuellement les bandes de frequence.
- Choisir EQ dynamique pour resonance et multibande pour desequilibre plus large.
  Activer seulement si l'analyse indique un besoin avec confiance suffisante.
- Fonctionnalites visees pour le produit complet, apres le socle manuel et les
  courbes temporelles ; livrer d'abord les etapes validees, pas tout a la fois.

### Finition Et Extensions

- Drum Bus : EQ subtil, compression de cohesion liee, mix parallele, saturation
  optionnelle, gain de sortie. Gate desactive par defaut.
- Saturation/soft clip : drive, compensation, mix et surechantillonnage a evaluer
  pour limiter l'aliasing. Desactive par defaut ; ne garantit pas de true peak.
- Transient shaper : extension optionnelle attack/sustain, avec garde-fous.
- Sidechain externe : extension possible, distincte des filtres internes de
  detection. Necessite routage et tests Cubase dedies.
- Aucun limiteur brickwall ou correction de phase automatique par defaut.

## 6. Profils Source Et Style

- Kick : grave utile, zone carton, attaque ; detection dans le registre adapte.
- Snare : corps, timbre, claquant, preservation des ghost notes.
- Tom : resonance et queue preservees, fermeture douce adaptee a la decroissance.
- Hi-hat : retrait du grave inutile, controle prudent des aigus ; gate facultatif.
- Overheads : equilibre du kit et cymbales, stereo preservee, nettoyage prudent.
- Room : ambience preservee, compression plus creative uniquement si demandee.
- Drum Bus : cohesion du kit, dynamique et equilibre global.
- Naturel : corrections modestes, dynamique et sustain conserves.
- Rock : attaque presente et controle modere.
- Metal : clarte dans les passages rapides, grave du kick controle, attaque
  presente, snare solide et ghost notes preservees, toms avec queues propres,
  overheads moins agressifs et bus sans ecrasement systematique.
- Pas de remplacement par samples, triggering ou changement de timbre radical
  par defaut. Transformation creative distincte, hors premiere version.
- Le style guide les propositions ; il n'impose pas une cible spectrale unique
  et ne pretend pas reproduire une pratique universelle de production.
- Presets source/style versionnes, distincts des resultats d'analyse du signal.

## 7. Interface Et Usage Dans Cubase

- Une interface JUCE dediee : selecteurs source/style, commandes Apprendre,
  Terminer, Appliquer et Annuler, progression, modules et meters lisibles.
- Vue musicale principale : Nettoyage, Regularite, Dynamique et Couleur avec
  controles d'intensite ; reglages techniques accessibles en vue avancee.
- Les controles musicaux ont des mappings bornes et documentes vers le DSP ;
  ne pas multiplier des macros et des automations contradictoires.
- Vue d'analyse avec timeline de frappes/corrections, confiance et couverture,
  possibilite d'exclure un passage. Indiquer clairement mode ARA ou insert.
- Vue principale compacte, reglages avances accessibles sans encombrement.
- Courbe EQ, niveaux entree/sortie, reduction gate et compresseur.
- Comparaison A/B des reglages et bypass avec compensation de niveau optionnelle.
  Compensation mesuree puis figee, pas de gain automatique qui pompe en continu.
- Valeurs numeriques editables, unites, remise a zero et acces clavier.
- Fenetre redimensionnable, prise en charge du scaling Windows.
- Etats visibles : analyse inactive/en cours/insuffisante/prete, clipping,
  latence et mode de qualite. Pas de lecture audio demarree automatiquement.
- Automatiser les parametres DSP utiles avec identifiants stables. L'analyse
  reste une commande ; source/style n'ecrasent pas les automations existantes.
- Application de recommandations coherente avec undo et gestes de parametres
  de l'hote, sans garantie de transaction atomique multi-parametres dans Cubase.
- Sauvegarde versionnee, valeurs par defaut pour parametres nouveaux et migration
  de l'etat existant. Preserver l'identifiant du gain actuel et les codes plugin.
- Renommage et distribution du produit : decisions explicites, pas de changement
  silencieux susceptible de casser les projets deja sauvegardes.

## 8. Detection Neuronale

- Option de recherche, pas prerequis de la premiere version autonome.
- Cas utile : distinguer frappe cible, ghost note et repisse lorsque le
  detecteur classique manque de separation. Pas de promesse de separation audio.
- Commencer par DSP statistique explicable et mesurer ses limites.
- N'adopter un modele que s'il ameliore un jeu d'evaluation independant.
- Exiger poids disponibles, licence compatible, donnees d'apprentissage
  documentees, inference locale, taille/CPU acceptables et repli sans modele.
- Evaluer ONNX Runtime ou autre moteur maintenu seulement apres choix du modele
  et verification de compatibilite avec le toolchain Windows du projet.
- Inference d'analyse hors thread audio. Toute inference continue serait un
  chantier distinct avec contraintes de temps, latence et synchronisation.
- Aucun envoi de pistes sur un service distant, telechargement de modele ou
  entrainement a partir de fichiers utilisateur sans consentement explicite.
- Sans modele adequat, livrer l'assistant classique et documenter ses limites.

## 9. Architecture Et Contraintes

- Utiliser les APIs JUCE audio, DSP, parametres et GUI ; reutiliser les briques
  eprouvees plutot que reinventer filtres, FFT ou surechantillonnage.
- Separer DSP par module, capture/analyse, recommandation et interface.
- Un moteur commun pour insert, renderer ARA et tests ; adaptateurs distincts
  pour transport/blocs de l'hote et sources/regions ARA. Pas de DSP duplique.
- En ARA, respecter les permissions et durees de vie d'acces audio du SDK,
  notifications d'edition, archivage et contraintes de threads de l'hote.
- Capture integrale : extraire en continu des descripteurs compacts avec buffers
  bornes ; ne pas accumuler toute la chanson en RAM sur le thread audio.
- Publier des plans immuables de correction vers le thread audio ; reclamation
  et destruction des anciens plans hors de ce thread, acces sans verrou.
- Application autonome non prioritaire ; pas de service reseau ou de processus
  externe requis pour utiliser la version VST3 sans ARA.
- Thread audio : aucune allocation, attente, prise de verrou, lecture de fichier
  ou journalisation. Buffers prealloues et transfert borne non bloquant.
- Analyse sur worker ; en surcharge abandonner des donnees de capture et le
  signaler, sans bloquer l'audio. Annulation et destruction du worker testees.
- Transmettre un instantane coherent des resultats a l'interface, puis appliquer
  les changements via les parametres JUCE sur le thread approprie.
- Lissage, coefficients stables, protections contre NaN/Inf et denormaux.
- Latence, tails et compensation du mix parallele correctes selon les modules.
- Un render offline accelere doit rester deterministe ; definir les regles de
  capture avant de permettre l'apprentissage pendant un export.
- Cible initiale : 44.1, 48, 88.2 et 96 kHz ; blocs variables, stereo/mono.
- Mesurer CPU et memoire avec plusieurs instances ; fixer le budget apres un
  prototype mesure sur la machine disponible, pas une promesse arbitraire.

## 10. Validation Et Criteres De Livraison

- Conserver les tests existants et ajouter des tests par module et integration.
- Filtres/EQ : reponse frequencielle, stabilite, bypass et changements de valeurs.
- Gate : attaques preservees, hysteresis sans chatter, range respecte, release
  continue, ghost notes, bruit, repisse et silence numerique.
- Compresseur : courbe statique, attack/release, stereo liee et mix parallele.
- Multibande : separation/recombinaison, absence de bosses inattendues, phase,
  crossovers extremes, bypass et stereo. EQ dynamique : action bornee et retour.
- Courbes : ghost notes non normalisees, gain limite, aucune discontinuite,
  chevauchements, exclusions, seeks/boucles et zones hors couverture.
- Export : corrections identiques pour une meme position independamment de
  la vitesse de rendu et du decoupage des blocs, dans les tolerances definies.
- ARA : source partagee entre regions, trims, deplacements, duplication,
  notifications d'edit, acces retire, archivage et reouverture du projet.
- Prototype hote obligatoire avant investissement ARA complet ; si validation
  Cubase indisponible, marquer ce jalon non valide et poursuivre le repli VST3.
- Analyse : silence, son continu, coups rares, roulements et confiance insuffisante.
- Estimation spectrale : signaux amortis connus, pitch descendant et bruit ;
  tolerance definie avant chaque test, refus des resultats ambigus.
- Recommandations : bornes conservatrices et absence de modifications avant
  validation ; application selective et annulation testees.
- Tests de blocs variables, changements de sample rate, automation rapide,
  sauvegarde/restauration, latence et bypass. Aucun NaN/Inf en sortie.
- Mesures et captures de reference reproductibles ; tests synthetiques generes
  automatiquement, sans banques d'echantillons aux droits inconnus.
- Prises acoustiques licites variees pour evaluation ecoute et faux declenchements.
  Sans corpus disponible, signaler cette partie comme non validee.
- Compiler VST3 et Standalone via windows-vs2019 et executer ProcessorTests.
- Verification Cubase : scan, instances multiples, mono/stereo, automation,
  sauvegarde, lecture bouclee et export. Les outils disponibles ne garantissent
  pas l'automatisation complete d'une session Cubase ou de son evaluation auditive.
- Aucun lancement audio ni copie vers un dossier systeme sans autorisation.
- Documentation des limites, installation, latence et licences avant distribution.

## 11. Ordre De Realisation Autonome

### Avancement Du Premier Jalon

- [x] Integration optionnelle du SDK ARA 2.2.0 avec JUCE 8.0.6 et VS2019.
- [x] Cible Drum ARA Prototype distincte : acces source et rendu de regions depuis
  un cache borne, diagnostic, marqueur d'archive ARA et mode insert de verification.
- [x] Compilation Release VST3 et Standalone existant preservee.
- [x] Trois suites automatiques : gain existant, cache temporel, factory/processeur
  du prototype et creation de son editeur diagnostic.
- [x] Detection comme extension et liaison ARA dans Cubase Pro 12.0.52 :
  capture utilisateur ARA editor apres copie dans Common Files/ARA.
- [x] Validation manuelle du prototype 0.1.6 dans Cubase : lecture depuis le
  milieu, boucles, sauvegarde/reouverture et export ecoute. Essais declares
  reussis par l'utilisateur le 14 octobre 2026 sur son projet de test.
  Pas de mesure du placement a l'echantillon pres ni de validation exhaustive.
- Premier essai utilisateur : prototype trouve dans les inserts, pas encore
  dans Extensions. Correctif 0.1.1 compile/teste : informationURL ARA non vide
  comme exige par le SDK ; effet sur la detection de l'hote a confirmer.
- Apres captures : menu contextuel evenement confirme sans Extensions ; cache
  Cubase contenant bien ARA Main Factory Class 0.1.1 et service ARA installe.
- [x] Quatrieme suite ARADiscoveryTests : scan/instanciation du vrai VST3,
  factory ARA chargee/initialisee et compatibilite ARA 2.0 verifiee. Egalement
  reussie sur le bundle installe. Ne valide pas encore document/binding/rendu.
- Defaut d'installation identifie : Cubase exige Common Files/ARA pour les
  plugins de developpement non valides pour sa decouverte ARA depuis VST3.
  Prototype seulement dans VST3 et dossier ARA absent sur cette machine.
  Procedure corrigee suivant la reponse Steinberg :
  https://forums.steinberg.net/t/ara-plug-in-not-loaded-in-cubase/756711/3
  Copie manuelle effectuee et detection confirmee par capture utilisateur.
  Les quatre suites restent reussies.
  Ne pas multiplier les manipulations utilisateur ni bloquer le moteur commun
  et l'apprentissage VST3 deux passes sur ce seul probleme d'integration.
- [ ] Cache de production avec reconstruction automatique, analyse et DSP drum.
- [x] Prototype 0.1.2 : conditions de refus testables, raison du dernier refus,
  formats source/rendu et duree dans l'editeur ; etats vide/partiel corriges.
  Capture utilisateur : refus source mono 48 kHz vers rendu stereo 48 kHz,
  region 17.5 secondes. Restitution encore a valider apres correction.
- [x] Prototype 0.1.3 : mono par defaut, detection des canaux source et adaptation
  automatique mono/stereo dans les quatre sens ; cache au format natif, limite
  RAM selon canaux stockes. Render et compteurs mono/stereo toujours visibles.
  Quatre suites reussies, dont negociation mono/stereo via le vrai VST3.
  Capture Cubase : region mono chargee et sortie stereo ; aucun son entendu.
- [x] Prototype 0.1.4 : conditions de silence testables et mesures atomiques
  (callbacks, transport, position, bornes cache, cretes cache/rendu avant gain).
  Capture Cubase : 2206 callbacks, 992 en lecture, position 541966 dans le cache
  [0, 840000[, crete cache -6.1 dBFS, rendu -11.3 dBFS ; toujours aucun son.
- [x] Prototype 0.1.5 : contournement du wrapper JUCE pour Cubase/Nuendo lies
  a ARA ne declarant pas leurs bus actifs via activateBus. Negociation des
  arrangements utilisee uniquement sans activation audio explicite de l'hote.
  Macro limitee au prototype, correctif reproductible au configure CMake.
  Test du remappeur reproduisant l'absence de copie puis restitution exacte
  mono/stereo apres activation ; quatre suites reussies, vrai VST3 0.1.5 charge.
- Capture utilisateur 0.1.5 : toujours aucun son, 1237 appels dont 1006 en
  lecture, position 607342 dans [0, 840000[, cache/rendu -6.1 dBFS.
- [x] Prototype 0.1.6 : secours des bus verifie avant remappage, y compris
  liaison ARA tardive et activation explicite de l'entree seule ; une sortie
  explicitement desactivee n'est pas reactivee. Mesure des buffers hote apres
  copie/gain et recalcul des flags de silence par canal. Editeur 640 x 520.
  Tests mono/stereo float/double, ordre de liaison et priorite hote reussis.
- [x] Retour du son avec 0.1.6 dans Cubase Pro 12.0.52 confirme par l'utilisateur
  le 14 octobre 2026. Capture : cache/rendu/sortie hote -6.1 dBFS, 2 canaux,
  bus actif, fallback yes. Transport arrete apres lecture ; cretes retenues.
  Ne permet pas d'isoler le changement decisif entre activation et flags.
- [x] Essais hote complementaires : lecture depuis le milieu, boucles,
  sauvegarde/reouverture et export confirmes reussis par l'utilisateur.
  Base 0.1.6 conservee ; montage intensif et longues sessions hors de ce jalon.

Le prototype est volontairement limite a 30 secondes par region, 128 regions et
128 Mio par renderer, sans resampling/time-stretch. Il n'analyse pas une chanson.
Le cache est invalide apres notifications d'edition, sans reconstruction automatique.
Procedure utilisateur : [Test ARA](docs/ARAPrototypeTest.md).
La validation automatique ne remplace pas ce jalon dans Cubase.

### Etapes Suivantes

Livraison 0.2.0 : moteur reel Source/Drum/KickEngine.h commun ARA/insert,
HP/LP 12 dB/octave, expander borne, quatre bells avec Q commun, compression
liee stereo, mix, gains, bypass lisse et meters. Interface native 1020 x 760,
courbe EQ et diagnostic separe. Reglages ARA par AudioModification selon
selection effective, archive versionnee et lecture des anciens marqueurs.
Tests moteur/reponse/stereo/etat/archive et vrai bundle reussis ; nouveaux
reglages selection/edition/reouverture ARA a valider dans Cubase. Voir
[Essai Kick](docs/KickTest.md) pour les limites de cette premiere chaine,
notamment gate sans hold/hysteresis et compresseur sans knee reglable.
La liste du socle manuel ci-dessus reste la cible, pas une liste entierement livree.

1. Preserver la base validee et stabiliser parametres/etat et non-regression.
2. Preserver le prototype ARA 0.1.6 valide manuellement dans Cubase Pro 12.0.52
  comme base d'integration. Verifier les licences avant distribution et garder
  le repli VST3 sans ARA pour les usages non couverts par le prototype.
3. Construire le moteur manuel et ses tests, d'abord Kick, puis autres sources
  et Drum Bus. Garder le VST3 sans ARA compilable pendant le developpement.
4. Construire interface principale/avancee, meters et comparaison de niveau.
5. Ajouter apprentissage du morceau entier, statistiques gate/spectre/dynamique
  et propositions avec confiance ; adapter les deux chemins d'acces au son.
6. Ajouter corrections par frappe, courbes synchronisees, archivage, exclusions,
  invalidation et tests de transport/export.
7. Ajouter EQ dynamique et multibande optionnel, profils de style et A/B.
8. Mesurer CPU/memoire/latence, valider integration, documenter et compiler.
9. Evaluer ensuite neural, transient shaper et analyse multi-micros.

Le travail peut etre pris en charge dans une meme campagne de developpement,
mais chaque etape reste compilee et testee avant la suivante. Pas de gros
assemblage non valide en fin de parcours.

## 12. Decisions Encore Ouvertes

- Direction technique decidee : VST3 + prototype ARA, pas application externe
  imposee, pas d'ecriture d'automation Cubase promise, apprentissage global et
  courbes internes. Les choix SDK restent soumis a verification effective.
- Direction musicale decidee : preservation de la performance, Naturel par
  defaut, Metal defini et controle plutot que remplacement automatique du son.
- ARA : choix d'un bundle commun ou d'une cible experimentale separee apres
  examen de compatibilite. Preserver codes et etats du VST3 existant ; toute
  nouvelle identite necessaire doit etre explicite et documentee.
- Nom final et identite graphique : conserver l'identite technique existante
  tant qu'une migration explicite n'est pas decidee.
- Corpus reel : utiliser des prises autorisees disponibles ; sinon commencer
  avec fixtures synthetiques et annoncer la limite de validation musicale.
- Profils supplementaires : jazz, funk, pop et variantes kick in/out ou snare
  top/bottom apres validation des profils principaux.
- Lookahead et surechantillonnage : valeurs finales apres mesures.
- Reseau neuronal : seulement si modele disponible et gain demontre.
- Le plan reste modifiable pendant la discussion ; sa creation ne lance pas
  l'implementation du plugin.