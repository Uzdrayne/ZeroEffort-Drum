# Drum / Kick 0.3.1

Guide historique. Pour la livraison actuelle avec styles et calibration :
[Auto Musical 0.4.0](KickAuto04.md). Les mesures ci-dessous concernent 0.3.1,
avant calibration du rendu ; le comportement actuel de sortie differe.

## Livraison

Premiere chaine manuelle, implementee dans Source/Drum/KickEngine.h et
raccordee au processeur reel dans Source/ARA/PrototypeProcessor.cpp.
Les fichiers du dossier tests ne sont que les validations executables.
Cubase Gain reste inchange. Le nom du bundle reste Drum ARA Prototype.vst3
pour conserver les identifiants et le chemin de chargement valides dans Cubase.
L'interface affiche DRUM / KICK et v0.3.1.

Build : build/DrumARAPrototype_artefacts/Release/VST3/Drum ARA Prototype.vst3.
Apercu du composant JUCE reel : [Interface](../build/KickInterface.png).

Fermer Cubase avant de remplacer le bundle entier dans Common Files/ARA.
Mettre aussi a jour la copie Common Files/VST3 si elle est conservee.
Aucune installation automatique ni lancement audio n'est effectue.
Utiliser une copie du projet de test valide avec 0.1.6. Une archive enregistree
en 0.2.0 n'est pas lisible par 0.1.6 ; conserver le projet original avant migration.

## Commandes Disponibles

- Gain d'entree -24 a +24 dB, sortie -60 a 0 dB. L'identifiant historique
  prototypeGainDb est conserve pour la sortie et les anciens etats insert.
- Filtres passe-haut et passe-bas, 12 dB/octave, activation commune.
- Expander descendant a detection de crete liee stereo : seuil, ratio, range,
  attack et release. Pas de fermeture absolue : attenuation bornee par range.
- EQ : quatre bells Body, Box, Attack et Air, frequences et gains independants,
  Q commun. Courbe de reponse de l'EQ, pas un spectre ni la reponse des filtres.
- Compresseur a detection de crete liee stereo : seuil, ratio, attack, release,
  makeup manuel et mix parallele. Knee dur ; pas d'auto-makeup ni limiteur.
- Bypass global interne et activation de chaque module, transitions de 20 ms.
- Indicateurs de crete IN/OUT et attenuation EXP/COMP du dernier bloc.
  OUT devient rouge si la crete depasse 0 dBFS, sans limiter le signal.
- Diagnostic conserve dans une vue separee, dont mesures de sortie hote ARA.

Ordre : entree -> filtres -> expander -> EQ -> compresseur/mix -> sortie.
Tous les modules demarrent desactives, gains a zero. Aucun traitement
automatique n'est applique en ouvrant un evenement. Double-clic sur un
potentiometre : valeur par defaut. Les valeurs peuvent aussi etre saisies.

## Reglages ARA Et Insert

En insert VST3, les parametres sont propres a l'instance et exposes a l'hote.
En ARA, les reglages sont propres a l'AudioModification de l'evenement.
L'editeur lit la selection effective de Cubase, affiche les reglages de la
premiere modification selectionnee et applique chaque commande modifiee a
toutes les modifications selectionnees. Sans selection, commandes desactivees.
Des regions partageant la meme AudioModification partagent donc les reglages ;
ce n'est pas une copie independante par occurrence. Deux modifications
distinctes disposent de reglages independants et sont traitees avant sommation.

Les reglages ARA sont archives par identifiant persistant, avec limites de
taille et validation complete avant application. Le marqueur des projets
0.1.x reste lisible : aucun reglage musical ARA n'y etait stocke.
Le moteur ARA remet a zero les etats de filtres/enveloppes lors des seeks et
boucles. Pas de preroll de dynamique : l'attaque initiale peut differer d'une
lecture continue, surtout pour un expander avec attaque lente.

## Essai Cubase

1. Verifier DRUM / KICK et v0.3.1. Lire avec tous les modules desactives :
   le niveau doit rester celui de la version precedente.
2. Activer Equalizer et monter Body gain de quelques dB : courbe et timbre
   doivent changer ensemble. Revenir a zero, puis couper Box moderement.
3. Activer Filters et monter High-pass : verifier le retrait du grave.
4. Activer Expander : regler le seuil entre repisse et frappes, commencer
   avec range 12 a 18 dB. Verifier EXP sans couper les frappes faibles.
5. Activer Compressor : baisser le seuil, verifier COMP, comparer avec mix
   a zero puis a 100 %. Eviter les boosts cumules, ecouter a faible volume.
6. Comparer au signal original avec Bypass, puis sauvegarder/reouvrir.
   Verifier les reglages, les activations et le rendu apres reouverture/export.
7. En ARA, essayer deux evenements sur des modifications distinctes :
   changer l'EQ du premier et verifier que le second ne change pas.

## Auto Preset

1. En ARA, selectionner UN evenement de kick non etire, d'au moins 2 s,
   contenant au moins trois frappes nettes. Cliquer Analyser, meme a l'arret.
   L'analyse parcourt l'audio expose de cet evenement par blocs de 32768
   echantillons, sans charger tout le fichier en memoire (maximum 1 h).
2. En insert, cliquer Analyser et lire 8 secondes d'audio. La capture est
   effectuee avant le DSP du plugin. L'analyse FFT et les decisions restent
   sur le thread d'interface ; le callback ne fait qu'une copie bornee
   dans un buffer prealloue. Aucun fichier n'est enregistre.
3. Attendre le bilan (transitoires detectes, corps spectral, sortie).
   Appliquer reste desactive pour silence, audio non fini, signal continu,
   faible contenu grave ou nombre de frappes insuffisant. Une saturation
   importante peut aussi provoquer un refus. Ce n'est pas un classifieur
   d'instruments fiable : la selection doit etre une piste de kick.
4. Cliquer Appliquer pour remplacer les reglages de la chaine. Avant cela,
   l'analyse n'a aucun effet sur le son. Ecouter puis cliquer Revenir pour
   retrouver les reglages precedant cette application, a la precision des
   parametres JUCE. Les valeurs ARA sont restaurees directement.
5. Tester sauvegarde/reouverture apres application : les reglages appliques
   sont archives comme les reglages manuels. La proposition et le point de
   retour Auto sont temporaires, non sauvegardes. Une nouvelle analyse
   remplace le point de retour precedent. Fermer l'editeur annule une analyse.

Reglage global heuristique, pas une IA entrainee ni une correction par frappe.
Depuis 0.3.1 : detection candidate par montee conjointe du grave (25-250 Hz)
et de la crete, avec espacement minimal de 120 ms. FFT JUCE Hann 4096 points
(8192 au-dessus de 48 kHz), pas de 10 ms, spectres separes selon le temps
depuis l'attaque : debut 0-70 ms, decroissance 70-230 ms, autres intervalles.
Les fenetres FFT debordent ces phases ; ce sont des estimations heuristiques,
pas une separation de sources. Le nombre affiche est celui des attaques
candidates, pas un comptage de frappes garanti, surtout sur double grosse caisse.
La frequence Body suit le maximum spectral des debuts entre 35 et 140 Hz,
sans boost ; HP adapte entre 20 et 35 Hz, LP 18 kHz. Box cible une resonance
entre 180 et 1000 Hz persistante dans la decroissance, plus forte que ses
voisines et le fond, avec une coupure de 0.5 a 3 dB. Air est attenue jusqu'a
3 dB uniquement pour un aigu significatif persistant entre les coups, pas
un simple clic d'attaque. Sans indice suffisant, EQ desactive, gains a zero,
bilan explicite "EQ neutre : pas de correction fiable". Le bilan decrit les
frequences et gains reellement proposes, pas une limite de correction.
L'application remplace toujours le preset manuel entier, ce n'est pas une
optimisation de ses reglages actuels. Expander 1.5:1,
range 6 dB seulement si contraste suffisant ; compresseur 2:1, attaque 25 ms,
mix 70 %, sans makeup, seuil limite pour une reduction statique de crete
d'environ 3 dB au plus. Entree remise a 0 dB, sortie a -3 dB ou moins si
elle etait deja plus basse ; bypass conserve. Les autres valeurs manuelles
sont remplacees par les valeurs de base du preset. Pas de limiteur ni de
garantie anti-ecretage ; commencer a faible volume.

Changer de selection, monter/editer une source ou modifier un parametre
invalide la proposition et le retour pour eviter d'ecraser d'autres choix.
Les occurrences partageant une AudioModification partagent toujours le
preset. Revenir n'est pas un historique d'undo Cubase. Pas de comparaison
automatique a niveau egal : la sortie prudente peut paraitre moins forte.
Formats : mono/stereo 8 a 192 kHz ; estimation par le canal de plus forte
amplitude a chaque echantillon, sans annulation par moyenne stereo.
L'analyse incrementale depend du timer de l'editeur ; les fichiers longs
prennent du temps. Lectures hote synchrones sur le thread UI, sans promesse
de reactivite sur stockage lent. La limite de lecture ARA de 30 s ci-dessous
reste distincte : analyser plus long ne rend pas ces regions lisibles.

## Validation Et Limites

Quatre suites CTest reussies : gain existant, cache/format/transport, moteur
Kick/processeur/archive/interface, decouverte du vrai VST3 et mapping hote.
Tests Kick : transparence, smoothing/bypass, reduction, liaison stereo,
reponse HP/LP et EQ +/-6 dB, range expander, mix sec, 44.1/48/96 kHz,
restauration insert, archive a deux entrees, troncatures et ancien format.
Capture de l'interface generee et inspectee sans ouvrir de peripherique audio.
Les commandes manuelles, la courbe EQ, les meters et la sauvegarde/reouverture
0.2.0 sont confirmes par l'utilisateur dans Cubase. Tests Auto : silence,
signal continu, entree non finie, corps a 65 Hz, 44.1/48/96 kHz, invariance
par blocs, limites des propositions, cycle insert capture/proposition sans
changement/application/retour, annulation et changement de frequence.
L'utilisateur a confirme par captures l'analyse et l'application ARA 0.3.0
dans Cubase ; la nouvelle detection 0.3.1 reste a ecouter dans l'hote.
Tests temporels 44.1/48/96 kHz : kick propre laisse neutre, resonance 400 Hz
localisee et reduite, invariance de gain, coups alternes forts/faibles,
aigu persistant corrige, clic d'attaque et repisse discrete preserves.

### Mesure Sur La Prise Utilisateur

Analyse en lecture seule de `wev test/trerst.wav`, 48 kHz stereo, 32 bits,
16.5 s, avec le moteur 0.3.1. Source non modifiee, aucun peripherique lance.
Crete brute -9.14 dBFS, RMS -34.37 dBFS (moyenne de tous les echantillons
des deux canaux). 85 attaques candidates, corps spectral 117.19 Hz,
aucune correction EQ fiable. Proposition : HP 35 Hz, expander seuil -46 dB,
ratio 1.5/range 6 dB ; compresseur seuil -15.14 dB, ratio 2, attaque 25 ms,
release 140 ms, mix 70 %, sortie -3 dB. Rendu en memoire : crete -12.60 dBFS,
RMS -37.98 dBFS ; reduction maximale EXP 6 dB, COMP 0.049 dB seulement.
Cette prise ne valide donc pas une amelioration musicale ni une compression
effective. Le nombre de frappes et les corrections doivent etre juges a
l'ecoute ; le resultat neutre ne prouve pas l'absence de besoin d'EQ.

Commande reproductible depuis la racine, sans modification du WAV :

```powershell
& '.\build\DrumPrototypeTests_artefacts\Release\DrumPrototypeTests.exe' --analyse-wav 'C:\Users\robin\Documents\CubaseVST\wev test\trerst.wav'
```

Limites ARA conservees : 30 secondes/region, 128 regions, 128 Mio de source
par renderer, sans resampling/time-stretch ni reconstruction automatique
apres montage. Buffers de travail DSP supplementaires prealloues a la preparation.
Des editions de source peuvent imposer une nouvelle preparation du cache.

Pas encore de courbes par frappe, autres sources/styles,
hold/hysteresis/lookahead du gate, ecoute/filtre de detection, knee reglable,
EQ shelf/Q par bande, saturation, EQ dynamique ou multibande. Pas d'undo ARA
personnalise garanti et pas de validation CPU sur de longues sessions.