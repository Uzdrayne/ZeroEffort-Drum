# ZeroEffort Drum 0.9.0

## Finitions Auto Et Visuels Live

Huit pages, 121 controles sauvegardes. Les 88 IDs precedents sont conserves ;
les nouveaux sont ajoutes. Charger un ancien etat/archive laisse les nouvelles
finitions OFF. Style/intensite Auto sont maintenant sauvegardes en insert et
par modification ARA, mais cela ne conserve pas le Bilan ni un historique des
retouches. La reouverture de ces nouveaux choix dans Cubase reste a verifier.

**Workflow actuel :** choisir role/style/Intensite puis **Analyser**. L'analyse,
la calibration et la verification terminees, les reglages sont appliques
automatiquement, finitions comprises. Aucun changement pendant une analyse
incomplete ou refusee. **Revenir** restaure l'ensemble des reglages precedents,
tant qu'ils n'ont pas ete modifies manuellement. Bilan detaille les choix.
En insert il faut toujours fournir 8 s d'audio ; en ARA un seul evenement entier.
Changer style/Intensite ensuite demande de relancer Analyser. Cela peut remplacer
les retouches manuelles ; comparer sur une copie. Le bypass existant est conserve.

**Selection automatique :** Intensite dose aussi les finitions (ancienne courbe
douce jusqu'a 50 %, puis accentuee), pas seulement les traitements historiques.
A zero les nouvelles finitions sont OFF. Crush exige un crest >9 dB (>12 Bus),
Attack/Sustain vise les micros proches avec crest >8 dB. Couleur est reservee
aux styles Rock/Metal avec assez d'energie grave/medium ; pas de distorsion,
Crush, impact ou ambiance automatiques sur Hi-hat/Overheads. Soft clip reste
rare : Rock/Metal, crest >14 dB et crete source >-6 dBFS.

L'EQ dynamique utilise des dominantes spectrales 220-1600 Hz et 3.5-12 kHz,
avec contraste local et energie suffisante ; une forte proportion d'aigu peut
aussi justifier la bande haute des cymbales. C'est un indice spectral global,
pas un diagnostic certifie de resonance ni une separation des sources.
La room est dose courte pour Snare/Toms/Bus si la prise est transitoire et
pas trop brillante ; OFF pour Kick, Room, cymbales et Metal. Niveau A/B est
active pour comparer. Corps mesure est tente sur Snare/Toms ; l'ecoute mono
n'est jamais imposee automatiquement.

### Lecture Du Graphique

La courbe verte reste EQ statique + HP/LP ; l'ambre est la reponse combinee
avec les deux attenuations dynamiques reellement mesurees. Les frequences/Q
restent celles du DSP ; seule l'attenuation bouge. Mise a jour 20 Hz avec
relachement visuel, pas une animation decorrelee de l'audio. Le spectre est
celui de la sortie ; ce n'est pas une courbe de reduction de gain.

Deux rangs permanents sous le trace, dans la zone haute : Crush, Dyn grave/aigu,
Impact, Couleur, Clip, Room, A/B puis Glue, trois bandes MB, Nettoyage, Corps,
Room GR et Correlation. Les dB indiquent une action de gain ; les dBFS de
Couleur/Clip mesurent un residu harmonique a 4x, ceux de Room/Corps un retour.
OFF = module inactif, 0 dB avec ON = pas d'action mesuree, BYP = bypass global.
Les barres retombent a l'arret ; IN/OUT restent visibles en bypass.
Les infobulles donnent la signification et l'unite de chaque mesure.

En ARA les meters suivent la premiere modification selectionnee, comme les
reglages ; plusieurs regions partageant la meme modification partagent aussi
ces mesures. Pas de mesure individuelle de tous les micros du kit. Le spectre
ARA reste partage entre renderers, il n'est pas une analyse multi-pistes liee.

- **Impact / Crush :** retour compresse ajoute au direct, independant de la
  Glue ; dose, seuil, ratio, attaque, release et HP/LP du retour. Commencer
  vers 10-20 % et comparer dans le mix, car ce retour peut augmenter le niveau.
- **Impact / Couleur :** drive et dose, traitement harmonique surechantillonne
  4x. Le residu entre voie coloree et reference filtree est ajoute au direct
  non retarde. Ce n'est pas une saturation serie ni une garantie d'absence
  d'aliasing ; mesure spectrale et budget CPU en session dense restent a faire.
- **Impact / Soft clip :** reference et dose d'arrondi harmonique, meme voie
  differentielle 4x. La reference n'est PAS un plafond de sortie garanti,
  encore moins un limiteur true-peak. Surveiller les cretes et le bus suivant.
- **Precision / Attack-Sustain :** enveloppes rapide/lente liees en stereo,
  attaque et sustain +/-12 dB. Verifier ghost notes, doubles et roulements ;
  ce n'est pas un classificateur de frappes propre a chaque instrument.
- **Precision / EQ dynamique :** deux bells a frequence/Q fixes, attenuation
  au-dessus du seuil et reduction maximale bornee. Premiere bande 80-2000 Hz,
  seconde 2-16 kHz. Les libelles Resonance/Cymbales expriment une destination,
  pas une separation des instruments. Chaque reduction est affichee en haut.
- **Precision / Corps mesure :** option pour le prochain Auto Snare/Toms,
  pic spectral 100-200 Hz Snare, 45-200 Hz Toms ; exige au moins 12 % d'energie
  dans cette plage. Sinon, conserve le profil et signale l'incertitude.
  Ni fondamentale certifiee ni mesure de corps Snare au-dessus de 200 Hz.
- **Bus / Salle caractere et densite :** caractere augmente taille et brillance
  de la room JUCE ; densite compresse le retour uniquement. Ce n'est PAS une
  vraie plate. Pour une ambiance de Snare seule, utiliser sa piste ou un envoi
  dedie plutot que pretendre l'extraire du bus somme.

**Niveau A/B :** rapprochement RMS progressif, gain borne environ +/-6 dB et
remontee cible limitee selon les cretes observees. Comparer avec Bypass ;
pas LUFS, pas un limiteur, pas une garantie d'egalite perceptuelle. La finition
et A/B sont avant Output, qui reste le gain final. La calibration ajuste ce
gain final plutot que le makeup avant les traitements non lineaires. Une
sortie legerement positive est possible si le rendu verifie garde sa marge.

**Mono :** moyenne L/R a l'ecoute ; l'indicateur haut mesure la correlation AVANT cette
moyenne. -1 indique une opposition, +1 une forte similitude ; une valeur seule
ne diagnostique pas la phase de tout le kit. Utiliser Overheads en paire stereo,
ecouter avec Kick/Snare ; aucune coordination ni correction inter-instances.

La queue declaree au host est de 8 s, mais le renderer ARA reste borne aux
evenements : cette declaration ne prolonge pas leurs queues. Preferer une
ambiance en insert de groupe pour les fins de morceaux. Changer le pre-delai
peut encore cliquer et un reset coupe la queue.

Tests : modules actifs/OFF, mono/stereo a 44.1/48/96 kHz, reductions bornees,
reset, corps mesure, ambiance modifiee, options Auto et migration des archives,
non-chevauchement des controles, activation Auto/Revenir, courbe dynamique et
captures pixel du processeur reel sous CTest. Ecoute,
automation intensive, CPU/aliasing et cycle complet des nouvelles options dans
Cubase restent des validations distinctes, pas des acquis de ces tests.

## Base Instruments 0.8

## Choisir L'element

Menu en haut de chaque instance : Kick, Snare, Tom 1, Tom 2, Tom 3, Tom basse,
Hi-hat, Overheads, Room et Drum Master Bus. Le choix est sauvegarde en insert
et dans les modifications ARA. Choisir un role ne modifie aucun traitement
audio ; il invalide l'ancienne proposition Auto. Choisir, Analyser, Bilan,
Appliquer. Les reglages manuels restent communs et disponibles.

Kick conserve le moteur et l'analyse 0.7, dont le multibande calibre. Les autres
roles ont des propositions explicites, pas une reconnaissance automatique :

| Role | Filtrage propose | Direction Auto |
| --- | --- | --- |
| Snare | HP 70 Hz, LP 18 kHz | Body 180 Hz, coloration Rock/Metal volontaire, compression large bande, expansion prudente si contraste suffisant |
| Tom 1/2/3, Tom basse | HP 35 Hz, LP 18 kHz | Corps mesure par pic spectral 45-200 Hz, coloration Rock/Metal, release COMP 250 ms et EXP 350 ms pour garder la decroissance |
| Hi-hat | HP 250 Hz, LP 18 kHz | EQ neutre, pas d'expansion, compression legere seulement si crest >12 dB |
| Overheads | HP 100 Hz, LP 18 kHz | EQ neutre, pas de gate ni traitement Kick, compression prudente si crest >12 dB |
| Room | HP 40 Hz, LP 18 kHz | Pas de gate, compression large bande moderee, decroissances a surveiller |
| Drum Master Bus | HP 20 Hz, LP 18 kHz | EQ neutre, Glue seule, pas de gate ni ambiance automatique |

Les noms Tom numerotent les instances, pas des frequences imposees : leur
corps est mesure dans la meme plage. Snare/Toms Rock a 100 % : Body +2.6 dB,
Box -3.5 dB, Attack +3.5 dB ; Metal accentue Box/Attack par 1.3. Naturel ne force
pas de coloration. Ce sont des intentions sonores, pas des defauts detectes.
Body du strip est limite a 200 Hz ; ce premier profil Snare ne mesure pas
une fondamentale au-dela. Les HP/LP restent a 12 dB/octave.

Focus, nettoyage et renfort synthetique Kick sont OFF dans les nouveaux
presets, pour ne pas les appliquer aux cymbales, caisses claires ou bus.
Snare/Toms proposent au maximum 7 dB d'expansion a 100 % si le contraste
est >=18 dB ; aucune garantie de preservation de toutes les ghost notes.
Hi-hat/Overheads/Room/Master ne proposent aucune expansion.
L'intensite dose l'EQ et les reductions cibles/plafonds, pas les coupures.
La courbe reste identique jusqu'a 50 %, puis augmente plus rapidement :
force interne 0.5 a 50 %, 1 a 70 %, 1.75 a 100 %. Le maximum est un choix
accentue pour les dix roles, pas une garantie de resultat musical ni une
simple hausse de volume. Les parametres restent bornes a leurs plages.
Les anciens reglages appliques ne changent pas : relancer Analyser puis
Appliquer. Le rapport conserve le pourcentage affiche, pas la force interne.
Les sliders lineaires affichent une piste grise, un remplissage vert et une
poignee claire, y compris a 100 %.
A zero, les dynamiques proposees sont OFF. Les profils non-Kick acceptent
un signal sans grave ; ils exigent un signal fini/audible sur au moins 2 s,
mais ne certifient pas qu'il s'agit bien de l'instrument choisi.

## Organisation Du Code

L'extraction initiale des profils conservait le comportement sonore ; la
facade applique maintenant la nouvelle courbe d'intensite :
[DrumProposal.h](../Source/Drum/DrumProposal.h) definit `DrumStyle`, `drumStyleName`
et `DrumProposal`. [DrumAnalysis.h](../Source/Drum/DrumAnalysis.h) garde les mesures
et les buffers FFT/histogrammes ; les profils les consultent par reference
constante, sans copie ni analyse supplementaire. Les helpers temporels
`kickTemporalResonance` et `kickDominantBody`, ainsi que `kickGateReleases`,
restent specifiques au Kick dans cet objet de mesure.

[DrumAutoPreset.h](../Source/Drum/DrumAutoPreset.h) est la facade `DrumAnalyser` :
elle normalise style/intensite et distribue le role explicite vers `KickProfile`,
`SnareProfile`, `TomProfile` (les quatre Toms), `HiHatProfile`, `OverheadsProfile`,
`RoomProfile` ou `MasterBusProfile` dans `Source/Drum/Profiles/`.
Chaque profil possede ses coupures, frequences EQ, temps et limites ;
[ProfileAssembly.h](../Source/Drum/Profiles/ProfileAssembly.h) partage seulement
la validation et l'assemblage non-Kick. Les regles Glue et avertissements Bus
sont dans le profil Master. Pas d'heritage polymorphique.

Les types partages sont renommes `DrumEngine`, `DrumAutoCapture`,
`DrumPresetPreview`, `DrumFilterResponse`, `DrumMultiband`, `ArchivedDrum` et
`DrumModification`. `KickPresenceDSP` et ses parametres gardent leur nom.
Mesures, refus, role/bypass/output/reset restent inchanges, comme les 88 IDs
et les formats d'etat/archive. Les propositions au-dessus de 50 % changent.

Reference de test figee AVANT extraction, sans duplication du moteur de mesure :
[LegacyDrumProposalReference.h](../tests/LegacyDrumProposalReference.h).
4 110 controles couvrent les dix roles, trois styles, doses
0/0.5/1, trois sample-rates et dix signaux synthetiques, plus les options
invalides. Les intensites jusqu'a 50 % et les refus restent compares exactement
a la reference ; au-dessus, les tests verifient validite, candidats, nouvelles
cibles/plafonds et bornes. Les tests existants verifient aussi le
rendu, la calibration, les archives, l'insert et l'interface.
Les gros histogrammes de preview restent sur le tas et le callback audio
n'ajoute aucune allocation, lecture de fichier ni verrou.

## Bus Glue

Pour la batterie complete, placer le plugin en INSERT sur le groupe Drum,
choisir Drum Master Bus, puis lire un passage representatif et Analyser.
Une extension ARA sur un evenement individuel ne recoit pas le groupe entier.
L'insert analyse une capture de 8 s. Une instance stereo doit etre negociee
par l'hote pour un groupe stereo ; le plugin accepte mono et stereo.

Page Bus : compression liee stereo de type glue, inspiree des reglages de
bus usuels, PAS une emulation de circuit SSL/VCA ni un produit SSL.

- Seuil -60 a 0 dB, ratio 1-10, attaque 0.1-100 ms, release 10-1500 ms.
- Makeup -12 a +18 dB, mix parallele 0-100 %.
- HP de sidechain 0-250 Hz (0 = OFF), ne filtre que le detecteur.
- Knee doux 0-12 dB, plafond GR 0-12 dB applique dans le moteur.
- Detecteur peak JUCE, max des canaux, gain commun : pas de modification
  volontaire du panorama. Pas de release Auto, lookahead ou modelisation VCA.

Auto Master a 100 % : cible P75 2.625 dB, GR max 5.25 dB, attaque 30 ms,
sidechain HP 80 Hz, knee 6 dB. Naturel : ratio 2, release 200 ms ; Rock :
ratio 2, release 100 ms ; Metal : ratio 4, release 100 ms. Cible et GR max
suivent l'intensite. Auto active la Glue si crest >6 dB et intensite >0,
desactive le compresseur ordinaire pour ne pas empiler les deux.

La calibration mesure l'enveloppe reelle APRES filtres/EQ et HP de sidechain,
sur les blocs dont le pic source est dans les 18 dB du plus fort. Elle inverse
la courbe du knee pour approcher le P75, puis verifie le rendu. Le Bilan montre
seuil, ratio, temps, sidechain, knee, P75 et max reels, RMS et sortie. Le P75
concerne des maxima de blocs de 256 samples, pas un pourcentage de frappes.
Le vumetre COMP commun montre la plus forte GR mesuree dans un etage ; si
plusieurs compresseurs sont actives manuellement, il ne somme pas leurs GR.

Les petits reglages 2:1 / 30 ms / 100 ms servent a laisser l'attaque puis
faire respirer le corps. Pour davantage de mouvement, ajuster le seuil en
ecoutant ; viser d'abord 1-3 dB de GR, pas un gain de volume apparent.
Verifier le mix parallele et comparer a niveau proche. Le HP de sidechain
peut eviter que le grave pilote toute la compression, mais trop haut peut
laisser passer les cretes de kick. Aucun limiteur/true-peak/LUFS.

## Petite Ambiance

Ambiance manuelle, OFF par defaut et dans toutes les propositions Auto.
JUCE Reverb, plage de room size elargie, damping fixe, signal wet uniquement
ajoute au sec APRES Glue et AVANT Output. Ce n'est pas un echo repetitif.

- Dose 0-100 % (ancien plafond : 10 %), commencer a 5-15 %. Pourcentage de coefficient wet ajoute,
  pas pourcentage d'energie mesuree ni remplacement du signal sec.
- Taille 0-100 % : petite salle a salle plus ample, pas une duree en ms/RT60.
- Pre-delai 0-40 ms ; HP wet 50-1000 Hz, LP wet 2-16 kHz.
- Valeurs initiales : dose 2 %, taille 25 %, pre-delai 10 ms, HP 150 Hz,
  LP 7 kHz. Retenir le grave/aigu dans la salle limite le brouillage.

Pour verifier l'effet en insert, activer Ambiance, desactiver Bypass, monter
la dose a 30-50 % et la taille a 60-80 %, conserver HP 150 Hz et LP 7 kHz.
Ne pas monter tous les reglages : monter le HP retire davantage de grave.
Le direct reste present meme a dose 100 % ; surveiller le niveau de sortie.
Le test processeur verifie une queue non nulle a 10 % et une energie environ
100 fois plus forte a 100 %, en insert a 44.1, 48 et 96 kHz. L'ecoute dans
Cubase reste a confirmer. La version affichee de ce build reste 0.8.0.

Le direct n'est pas attenue par le mix d'ambiance. La stereo peut s'elargir
par les reflexions : comparer en mono et avec les Room existantes. Desactiver
Ambiance coupe sa queue apres le fondu ; un reset de transport la vide.
Changer le pre-delai pendant la lecture peut produire une petite discontinuite,
pas de crossfade a deux lignes de retard. Queue declaree a l'hote : 3 s.
En ARA, le moteur est par region et la lecture est bornee aux regions : les
queues ne sont pas garanties au-dela de leur fin. Preferer l'INSERT sur le
groupe pour cette ambiance et verifier la fin de l'export dans Cubase.

## Bypass ARA

### Correctif Multi-Pistes ARA

Le rendu utilisait un compteur d'invalidation commun au document : modifier
une source ou region sur une autre piste pouvait rendre les anciens lecteurs
silencieux jusqu'a leur prochaine preparation. La validite est maintenant
suivie par region et source ; les caches sans lien avec la modification
continuent de jouer. Un cache reellement modifie reste refuse jusqu'a sa
reconstruction, sans lecture disque ni allocation dans le callback audio.
Les tests couvrent sources distinctes/partagees, suppression et reconstruction
sans reactivation des anciens caches. Le cycle multi-pistes complet dans
Cubase reste a confirmer. Fermer Cubase, remplacer le bundle entier dans ARA
et la copie VST3 si utilisee, puis reouvrir une copie du projet pour reconstruire
les caches. La version affichee reste 0.8.0 pour ce build correctif.

Cause identifiee dans le code 0.7 : toutes les cases etaient desactivees
lorsque la selection ARA disparaissait. En 0.8, les autres reglages gardent
cette protection, mais Bypass retrouve la derniere selection editee via les
identifiants persistants des modifications encore actives avec des regions.
Il ne modifie PAS toutes les pistes/document si la selection est vide.
Une suppression/deactivation de cible empeche son rappel ; aucun pointeur
vers une ancienne region n'est conserve. L'infobulle indique la cible active
ou le rappel de derniere selection. Un groupe de cibles recoit la meme valeur.

Il faut avoir selectionne une region au moins une fois dans cette instance
d'editeur. La memoire de cible est temporaire, pas restauree avec le projet.
Si aucune cible n'est disponible, Bypass reste desactive et son infobulle
demande une selection. Le cas reel signale par l'utilisateur doit encore
etre confirme dans Cubase ; aucun test automatise du cycle complet de
selection/de-selection ARA de Cubase n'a ete execute.

## Bilan Du Groupe

Le Bilan Master affiche RMS, crete, crest et mesures de Glue. Il avertit si
les cretes source sont proches de 0 dBFS, si le bus est deja dense ou si le
grave est discret. Un grave discret invite a verifier niveau, timbre et phase
du Kick : ce n'est PAS une preuve que son fader est trop bas.
Input Auto reste 0 dB ; aucun fader Cubase ni niveau individuel n'est modifie.
La verification garde la marge sample-peak -1 dBFS, ajuste Output <=0 et,
en full-wet, le makeup du compresseur actif pour restaurer le RMS si possible.

Le bilan collectif entre instances, leurs passages communs et des propositions
de niveaux par piste ne sont PAS implementes dans cette version. Il faudra
une communication des mesures des instances identifiees ; la somme seule
ne suffit pas. Rien ne normalise les accents ou velocites du batteur.

## Validation Et Installation

88 controles : les 71 anciens IDs sont conserves, role + 16 parametres bus
ajoutes. Les etats/archives anciens gardent Kick, Glue OFF, Ambiance OFF.
Renommage de developpement : produit/bundle **ZeroEffort Drum**, cible CMake
`ZeroEffortDrum`, processeur `ZeroEffortDrumProcessor` et fichiers
`Source/ARA/ZeroEffortDrumProcessor.cpp/.h`. Test :
`tests/ZeroEffortDrumProcessorTests.cpp`, cible `ZeroEffortDrumTests`.
L'ancien test Cubase Gain est retire. Nouvelle identite, sans migration hote garantie :
code plugin `Zedr`, bundle `com.robin.zeroeffortdrum`, factory
`com.robin.zeroeffortdrum.factory.1`, archive `com.robin.zeroeffortdrum.archive.1`.
Les anciens formats de donnees restent testes, mais Cubase ne substitue pas
ce nouveau plugin aux instances Drum ARA Prototype sauvegardees.
Utiliser de nouvelles instances sur une copie du projet de developpement.

Release windows-vs2019 : 3/3 suites passent (cache, Drum, decouverte ARA).
La suite de l'ancien exemple Gain est retiree. Tests trois sample-rates
44.1/48/96 kHz, neuf nouveaux roles et trois styles avec calibration/rendu,
silence/nonfinite/absence de grave, conservation du Kick, plafonds Glue,
stereo opposee, ratio 1, reponse impulsionnelle et reset salle, bypass sec,
RMS/headroom Master, archives 71/88 et etats insert, menu discret sans
modification DSP, page Bus sans chevauchement et Bypass insert raccorde.
Chargement du VST3 0.8 et initialisation ARA passent. Capture native Bus et
graphique reel, sans ouverture de peripherique audio. Pas de mesure instrumentee
des allocations ni benchmark CPU, pas d'ecoute ou de validation Cubase 0.8.

Prise Kick_12 en Rock 100 % : P75 4.2/2.1/1.0 dB, max 8/4.8/2.8 dB,
crete -8.02968 dBFS, RMS -30.517 dBFS, identiques a 0.7. WAV inchange.
Le Bilan utilisateur 0.7 a Rock 90 % indique 3.7/1.8/1.0 dB pour cibles
3.6/1.8/0.9 dB et ecart RMS 0 ; il confirme le workflow Appliquer, pas
une validation musicale des nouveaux instruments. 741 candidats et 298
declenchements Focus ne sont pas des comptes de frappes certifies.

Fermer Cubase. Installer manuellement le dossier .vst3 ENTIER :
`build/ZeroEffortDrum_artefacts/Release/VST3/ZeroEffort Drum.vst3`
dans `C:\Program Files\Common Files\ARA`, et le dossier VST3 pour les inserts.
Retirer l'ancien bundle Drum ARA Prototype des dossiers scannes pour eviter
de choisir le mauvais plugin ; le conserver ailleurs si un ancien projet en a
besoin. Les anciens artefacts de build peuvent encore exister, mais ne sont
plus produits par les cibles actuelles. Ne pas simplement renommer l'ancien
bundle installe : utiliser le nouveau build et son binaire interne correspondant.
Pour l'INSERT sur groupe, mettre a jour la copie dans le dossier VST3 scanne
par Cubase. Verifier v0.8.0. Aucune copie systeme ni lecture audio automatique.

1. Sur une copie du projet, ajouter ZeroEffort Drum et choisir Snare/Tom/Overheads sur la bonne instance.
2. Analyser, lire le Bilan, Appliquer, verifier attaques et ghost notes.
3. Placer une instance sur le groupe Drum, choisir Drum Master Bus.
4. Analyser un passage representatif, appliquer et ecouter Glue seule.
5. Ajouter Ambiance manuellement a 1-3 %, comparer en stereo et mono.
6. En ARA, desselectionner l'evenement apres edition et tester Bypass :
   seule la derniere cible doit changer. Tester aussi changement de selection.
7. Sauvegarder/reouvrir, boucler et exporter en verifiant les queues de salle.

Captures : [Bus Glue / Ambiance](../build/DrumMasterBus.png),
[Menu et EQ Kick](../build/KickInterface.png).