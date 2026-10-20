# DRUM / KICK 0.4.3

> Archive au 20/10/2026, non maintenue. Etat et suivi courants : [PROJET.md](../PROJET.md).
> Le texte ci-dessous conserve ses dates, decisions et eventuelles contradictions historiques.

Historique de la serie 0.4. La livraison actuelle, le nouveau format d'archive
et les nouvelles limites sont decrits dans [Presence Kick 0.5.0](KickPresence05.md).

## Livraison

Trois intentions musicales, intensite, calibration du compresseur par le vrai
DSP et ajustement de niveau sur le rendu mesure. Ce sont des presets globaux
heuristiques, pas une IA entrainee ni un remplacement du jugement de mixage.
Cubase Gain et les identifiants du bundle Drum ARA Prototype sont conserves.
L'archive des 29 controles ne change pas : anciens reglages conserves.
Les anciens projets ne recoivent aucune nouvelle coloration automatiquement.

Correctif 0.4.1 : compression calibree sur les blocs actifs plutot que le seul
coup maximal, attaques Rock/Metal plus rapides, mix 100 % et makeup mesure.
COMP a une echelle 0-12 dB et un relachement visuel de 300 ms distinct du DSP.
La courbe verte cumule EQ et filtres ; la courbe ambre isole les filtres.
HP/LP affichent leurs coupures effectives et leur pente fixe de 12 dB/octave.
Les reperes de coupure suivent les reglages ; OFF grise les libelles et retire
la contribution des filtres a la courbe, meme si l'EQ reste active.

Correctif 0.4.2 : le refus des regions de plus de 30 secondes est supprime.
La capture du vrai projet montrait une region mono 48 kHz de 231.448 s,
refusee avant toute lecture, donc cache vide et sortie silencieuse malgre
un bus actif. Cette source occupe environ 42.4 Mio et tient dans le cache.
La capacite est maintenant decidee par la RAM et les longueurs adressables,
pas un verrou de duree arbitraire. Les sources trop grandes restent refusees
explicitement, sans lecture disque dans processBlock.

Correctif 0.4.3 : Q des quatre bandes EQ Auto monte a 0.85/1.0/1.2 selon
Naturel/Rock/Metal, a la demande d'ecoute utilisateur. Les HP/LP gardent
leur resonance Butterworth fixe et leur pente 12 dB/octave. Le Q manuel et
les anciens presets appliques restent inchanges jusqu'a une nouvelle analyse.
EXP : seuil estime depuis sa propre enveloppe plutot que les cretes brutes,
releases plus courtes, ratio/range par profil, echelle 0-12 dB et relachement
visuel 150 ms, distinct de la dynamique audio et des mesures de verification.

Fermer Cubase et remplacer le bundle .vst3 ENTIER de
`build/DrumARAPrototype_artefacts/Release/VST3` dans
`C:\Program Files\Common Files\ARA`. Mettre aussi a jour la copie VST3 si
elle est conservee. Pas de copie systeme automatique ni lancement audio.
Utiliser une copie du projet de test et commencer a volume faible.

## Essai

1. Verifier v0.4.3, choisir un seul evenement Kick d'au moins 2 s en ARA.
2. Choisir Rock, intensite 60 %, puis Analyser, meme a l'arret.
3. Attendre Analyse, Calibration puis Verification. Ces trois passes lisent
   l'evenement sans changer le traitement actuel et sans lire le son en sortie.
4. Ouvrir Bilan pour voir le Q, EXP ON/OFF et seuil, niveaux d'enveloppe,
   attenuation EXP mesuree, P75 COMP, maximum, makeup et ecart RMS.
5. Appliquer : les reglages manuels sont remplaces par le preset complet.
   Comparer a l'original avec Bypass. Revenir restaure l'etat avant application
   tant que les reglages et la selection n'ont pas change.
6. Essayer Naturel puis Metal sur le meme extrait. Changer le profil ou
   l'intensite ne change pas le son : cela invalide seulement la proposition.
   Refaire Analyser avant Appliquer. Le point de retour reste disponible si
   aucun reglage du preset applique n'a ete modifie ; une nouvelle analyse
   remplace ce point par l'etat courant. Pas d'historique d'undo garanti.
7. Sauvegarder/reouvrir et verifier les vrais reglages appliques. Le profil
   choisi, l'intensite, la proposition et le retour sont temporaires, non
   archives. Les parametres DSP appliques sont toujours archives normalement.

En insert, capturer 8 s de lecture puis attendre les deux passes de calcul.
La capture utilise toujours un signal mono choisi par amplitude maximale
entre canaux : la calibration stereo insert est donc une approximation.
ARA verifie le vrai signal mono/stereo de la source exposee, avant les inserts
et faders Cubase. Deux occurrences partageant une AudioModification partagent
leurs reglages ; les selections multiples ne sont pas analysees en lot.

## Intentions

Valeurs a 100 % avant calibration, uniquement si l'analyse accepte le kick :

| Profil | EQ volontaire | Dynamique |
| --- | --- | --- |
| Naturel | Corrections suffisamment etayees, Q 0.85 | COMP P75 cible 1.5 dB, plafond 3 dB, 2:1, 25/140 ms, mix 70 %, EXP 1.5:1 / range 6 dB / release 160 ms |
| Rock | Body +1.5 dB, Box -2 dB en plus d'une correction eventuelle, Attack +2 dB a 3 kHz, Q 1.0 | COMP P75 cible 4 dB, plafond 8 dB, 3:1, 8/100 ms, mix 100 %, EXP 2:1 / range 8 dB / release 100 ms |
| Metal | Body +2 dB, Box -3.5 dB, Attack +3.5 dB a 4.5 kHz, Q 1.2 | COMP P75 cible 6 dB, plafond 10 dB, 4:1, 4/80 ms, mix 100 %, EXP 2.5:1 / range 12 dB / release 80 ms |

Rock/Metal placent Body a 75 % de la frequence estimee, limite entre 45 et
90 Hz. Sans resonance localisee, Box est centre a 320 Hz en Rock, 400 Hz
en Metal ; coupure totale limitee a -5 dB. Ces couleurs sont des choix de
mixage explicites, pas des defauts que le programme pretend avoir detectes.
Naturel peut toujours avoir une EQ plate et desactivee.

L'intensite multiplie les gains EQ, le range et l'exces de ratio de l'expander
et la reduction cible du compresseur. A zero, EQ et dynamiques sont desactives,
mais filtres et ajustement de sortie restent possibles. Pas un mix dry/wet
global ni un morphing en lecture : refaire l'analyse pour appliquer un dosage.

## Calibration

EXP : trois enveloppes supplementaires aux attaques 1 ms/releases des
profils estiment les niveaux P20, P35 et P90 avant filtrage, sur le signal
d'analyse. Le seuil vise P35 + 6 dB mais reste au moins 12 dB sous P90 et
dans -80..-12 dB. EXP est desactive si P90-P20 est inferieur a 12 dB ou si
l'intensite vaut zero ; le Bilan explique ce cas au lieu de forcer une action.
Cette marge ne garantit pas la preservation de toutes les ghost notes.
L'ancienne formule Rock utilisait -46 dB sur trerst.wav, au niveau meme du
P20 du detecteur, avec release 200 ms : elle laissait trop de passages ouverts.
Le nouveau seuil est -39 dB avec release 100 ms sur cette prise.
La verification via le vrai moteur (filtres actifs, canaux natifs ARA) mesure
EXP max et la proportion ponderee en duree des blocs de 256 samples avec
plus de 1 dB de reduction et une crete d'entree superieure a -80 dBFS.
Le libelle "blocs audibles" designe ce plancher technique, pas un jugement
d'audibilite. Ce n'est ni une proportion de frappes ni un taux de suppression
des cymbales ; le maximum de chaque bloc peut inclure une attenuation breve.
La repisse simultanee au kick peut ouvrir le detecteur large bande et reste
audible : pas de separation de sources ni de filtre de sidechain a ce stade.

Analyse : trois enveloppes JUCE BallisticsFilter aux attaques/releases des
profils estiment un seuil de depart, plutot que les seules cretes brutes.
La passe Calibration rend le preset via KickEngine, par blocs de 256 samples,
et mesure la reduction maximale et une distribution par niveau d'entree.
Les blocs actifs ont une crete a moins de 18 dB du maximum de l'extrait ;
le P75 est le 75e percentile de leurs maxima de reduction, pas une mesure
par frappe ni une moyenne de toute la piste incluant le silence.
Le seuil vise ce P75 selon le ratio, sous un plafond maximal prioritaire.
La correction reste bornee : une cible peut ne pas etre atteinte si le plafond
est atteint. Verification rend une nouvelle fois et affiche le resultat reel.
Ces valeurs ne garantissent pas le meme comportement sur un autre passage.
Le bypass est ignore pendant cette simulation et conserve dans le preset.

La sortie cherche a rapprocher le RMS global du signal original et limite
les cretes d'echantillons mesurees a -1 dBFS. Elle reste entre -60 et 0 dB.
Si cette sortie ne suffit pas, Rock/Metal a mix 100 % utilisent le makeup
existant pour recuperer le niveau, dans la marge de crete mesuree et les bornes
du controle. Pas de makeup automatique en mix parallele : Naturel peut garder
un ecart RMS negatif. Bilan indique l'ecart restant et le makeup propose.
Ce n'est ni une mesure LUFS ni une egalisation du
volume percu, ni un limiteur temps reel/true-peak. Une nouvelle source, une
superposition de regions ou une modification manuelle peut depasser le niveau
mesure. La reduction affichee est celle de la branche compressee avant mix.

Les calculs et lectures hote restent sur le thread UI, incrementalement.
Pas d'allocations, FFT, verrous ou fichiers ajoutes au callback audio.
Les longues sources ou un disque lent peuvent rendre l'interface moins
reactive. La lecture ARA reste limitee a 128 Mio de source et 128 regions
par renderer. A 48 kHz cela represente environ 699 s mono ou 349 s stereo
au total, moins aux frequences superieures. Une meme source utilisee dans
plusieurs regions est encore stockee plusieurs fois. L'analyse peut lire
jusqu'a 1 h mais cela ne garantit pas que son rendu tienne dans le cache.
Pas encore de detection
adaptative de fin de capture, traitement par frappe, autres instruments,
saturation, comparaison A/B avec niveaux perceptuels egaux ou multibande.

## Verification

Quatre suites CTest passent : processeur initial, cache, moteur/processeur/UI
Kick, decouverte du vrai VST3/ARA et regression des bus Cubase. Tests styles,
intensite, neutralite avant application, retour, calibration du vrai DSP aux
44.1/48/96 kHz, headroom mesure et commandes UI. Regression avec coup isole
plus fort, compensation RMS et relachement/reset du vumetre COMP.
La reponse HP/LP affichee est comparee aux vrais filtres TPT du moteur aux
trois frequences d'echantillonnage, filtres actifs et desactives.
La 0.4.0 est confirmee appliquee dans Cubase mais jugee trop peu compressee
par l'utilisateur. Regression 0.4.2 sur la region de 231.448 s : validation,
capacite native mono/stereo, lecture stereo a trois minutes, borne de fin,
limites memoire cumulee et nombre de regions, protection des conversions.
Le son de la 0.4.2 est confirme par l'utilisateur dans son vrai projet.
Regression 0.4.3 aux trois rates : plus de 2 dB de nettoyage du fond entre
coups synthetiques, perte inferieure a 1 dB sur les 70 premieres ms des coups
faibles a -10.5 dB des coups forts, range borne, intensite nulle et refus EXP
avec contraste insuffisant. Relachement/reset de l'affichage EXP testes.
La validation d'ecoute de la 0.4.3 reste a faire, notamment les queues et
ghost notes des deux prises utilisateur. Les mesures ne prouvent pas un
meilleur son ni une absence de pompage sur une prise reelle.

Apres mise a jour, fermer/reouvrir une COPIE du projet pour reconstruire le
cache. Pour l'evenement de la capture, attendre Regions cached: 1,
Unavailable: 0 et Cache ready. Si le cache n'est pas reconstruit, retirer puis
reappliquer l'extension uniquement dans cette copie en notant les reglages.
Ne pas compter sur Bypass interne pour recuperer une region non chargee.

Prise utilisateur `wev test/trerst.wav`, 16.5 s, 48 kHz stereo. Source lue
seulement, non modifiee. Crete brute -9.14 dBFS, RMS brut -34.37 dBFS.
Table historique 0.4.2 avant revision EXP/Q :

| Profil 100 % | COMP P75 actif | COMP max rendu | Makeup | Crete rendue | RMS rendu |
| --- | --- | --- | --- | --- | --- |
| Naturel | 0 dB | 3 dB | 0 dB | -10.74 dBFS | -35.19 dBFS |
| Rock | 3.2 dB | 8 dB | +1.97 dB | -13.34 dBFS | -34.37 dBFS |
| Metal | 4.3 dB | 10 dB | +2.40 dB | -14.62 dBFS | -34.37 dBFS |

Mesures 0.4.3 a 100 % :

| Prise / profil | Seuil EXP | EXP max | Blocs >1 dB EXP | COMP P75 / max | Makeup | Crete / RMS rendus |
| --- | --- | --- | --- | --- | --- | --- |
| trerst / Rock | -39 dB | 8 dB | 78.3 % | 3.2 / 8 dB | +2.08 dB | -13.22 / -34.37 dBFS |
| trerst / Naturel | -39 dB | 6 dB | 70.0 % | 0 / 3 dB | 0 dB | -10.74 / -35.23 dBFS |
| Kick_12 / Rock | -37 dB | 8 dB | 61.9 % | 3.6 / 8 dB | +1.98 dB | -8.35 / -30.52 dBFS |
| Kick_12 / Metal | -38 dB | 12 dB | 61.4 % | 4.8 / 10 dB | +2.44 dB | -9.60 / -30.52 dBFS |

Kick_12.wav : 165.129 s, 48 kHz mono 24 bits, crete brute -5.13 dBFS,
RMS brut -30.52 dBFS. 741 attaques candidates, pas un comptage certifie
de kicks. Cette prise tient dans le cache ARA natif (environ 30.2 Mio).
Les deux WAV sont inchanges, sans lecture audio ni installation automatique.

Le rendu ne valide pas un meilleur son ; il prouve ici que la compression
agit et que la comparaison ne depend plus d'une attenuation fixe de -3 dB.
Commande reproductible (natural, rock ou metal en dernier argument) :

```powershell
& '.\build\DrumPrototypeTests_artefacts\Release\DrumPrototypeTests.exe' --analyse-wav 'C:\Users\robin\Documents\CubaseVST\wev test\trerst.wav' rock
```

[Apercu de l'editeur reel](../../build/KickInterface.png).