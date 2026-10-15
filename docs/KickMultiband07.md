# DRUM / KICK 0.7.0

Guide historique. Version actuelle : [Instruments et Drum Bus 0.8.0](DrumInstruments08.md).

## Interface Globale

Ordre : bandeau, graphique et vumetres, onglets, reglages, commandes Auto.
Le graphique/spectre OUT et IN/OUT/EXP/COMP restent visibles sur les cinq
onglets et en Diagnostic. Input, Output et Bypass restent accessibles.
La courbe verte represente EQ + HP/LP, pas la reduction dynamique multibande.
Les echelles EQ dB et spectre OUT dBFS restent distinctes.

## Compresseur Trois Bandes

Compresseur active le module. Multibande coche remplace le compresseur large
bande par trois compresseurs lies stereo, apres l'EQ. Decoche, les anciens
reglages large bande restent disponibles, sans ecraser ceux des trois bandes.

- Grave / Medium : coupure 60-350 Hz ; Medium / Aigu : 800-6000 Hz.
- Chaque bande : seuil -80 a 0 dB, ratio 1-12, attaque 0.1-100 ms,
  release 10-1500 ms, makeup -12 a +12 dB, GR max 0-24 dB, Solo.
- GR max borne la reduction demandee par cette bande, pas la crete de sortie.
- Solo permet d'ecouter une bande ou la somme de plusieurs bandes cochees.
  Il reste sauvegarde : le desactiver apres inspection. Auto remet les trois
  Solos a zero lorsqu'on applique la nouvelle proposition.
- Makeup global s'ajoute au makeup de chaque bande. Mix melange le signal
  filtre de chaque bande et sa version compressee ; a 0 %, la dynamique et
  les makeups sont inactifs, mais les coupures restent dans le trajet.
- Les trois barres GR affichent le maximum de reduction par bande du dernier
  bloc traite. COMP global affiche la plus forte reduction d'une bande,
  avec relachement visuel ; ce n'est pas une attenuation globale equivalente.
  La graduation 12 dB sature visuellement au-dela, le nombre reste reel.

Filtres JUCE Linkwitz-Riley 24 dB/octave ; compensation allpass du grave pour
recombiner les branches avec une amplitude plate lorsque les ratios valent 1.
Ce sont des filtres a phase minimale, pas lineaires en phase ni transparents
echantillon par echantillon. Verifier la relation avec overheads/room/autres
micros. Le changement de mode est lisse sur 20 ms, mais peut produire une
coloration transitoire de phase. Desactiver Compresseur retire les coupures.
Le bypass global conserve le trajet sec original une fois son fondu termine.

## Reglage Par Analyse

Choisir Naturel/Rock/Metal et l'intensite, puis Analyser, Bilan, Appliquer.
Analyser ne change pas les reglages audio. En ARA, l'analyse porte sur une
region selectionnee ; en insert, capture de 8 secondes pendant la lecture.
Le traitement est commun aux deux modes, avec les limites de capture et
cache decrites dans les guides precedents.

Lorsque l'analyse juge la compression pertinente, elle propose Multibande :
coupure basse adaptee au corps estime (100-180 Hz), coupure haute 2 kHz en
Naturel/Rock, 2.5 kHz en Metal, temps/ratios adaptes au profil.

| Profil a 100 % | Cibles P75 grave / medium / aigu | GR max grave / medium / aigu |
| --- | --- | --- |
| Naturel | 1.5 / 0.75 / 0.375 dB | 3 / 1.8 / 1.2 dB |
| Rock | 4 / 2 / 1 dB | 8 / 4.8 / 3.2 dB |
| Metal | 6 / 3 / 1.5 dB | 10 / 6 / 4 dB |

Cibles et plafonds sont multiplies par l'intensite. A zero, la compression
est desactivee. Naturel conserve son mix 70 %, Rock/Metal 100 %.
Le P75 concerne les maxima des blocs de 256 echantillons dont le pic de bande
est dans les 18 dB du plus fort pic de cette bande, pas 75 % des frappes.
Les cibles portent sur la reduction du detecteur, avant le mix parallele.

Premiere passe : mesure des enveloppes apres Focus/Presence/EQ et coupures,
sans confondre reduction demandee et reduction plafonnee. Les trois seuils
sont deduits de ces distributions. Une bande trop faible pour atteindre la
cible dans la plage autorisee est laissee sans compression et signalee.
Deuxieme passe : rendu avec les seuils calibres, mesure P75/max par bande,
RMS et crete ; ajustement Output et, en full-wet, makeup global si necessaire.
Le Bilan donne cibles et mesures, toutes les valeurs finales et les limites.
Les cibles sont des intentions, pas une garantie d'ecoute ni des automations
par frappe. La quantification et les transitions peuvent produire un petit
ecart P75 ; les plafonds de reduction sont imposes dans le moteur.

L'Auto conserve Output <= 0 dB et une marge de crete mesuree de -1 dBFS.
Ce n'est ni un limiteur ni du true-peak/LUFS. Output manuel reste jusqu'a
+12 dB ; verifier les anciennes automations normalisees depuis la 0.6.
La phase et le mapping des canaux ARA peuvent changer les cretes dans le mix.

## Verification

Release windows-vs2019 : quatre suites CTest passent. Couverture : amplitude
recombinee a 44.1/48/96 kHz, compression independante, Solo grave/aigu,
stereo en opposition, coupures extremes, reset deterministe, trois profils
Auto aux trois rates, cibles P75 accessibles, plafonds, RMS et marge de crete,
archives/etats insert et anciens 29/43/47 controles, 24 reglages multibandes
visibles sans chevauchement, raccordement seuil/mode et navigation sous le
graphique. Capture native : pixels FFT reel et trois GR non nulles verifies.
Le bundle VST3 0.7.0 est scanne/instancie et sa factory ARA initialisee.

Mesures Rock 100 %, WAV utilisateur en lecture seule :

| Prise | P75 grave / medium / aigu | Max grave / medium / aigu | RMS brut = traite | Crete traitee |
| --- | --- | --- | --- | --- |
| trerst.wav, stereo 16.5 s | 4.1 / 2.1 / 1.1 dB | 8 / 4.8 / 2.7 dB | -34.3675 dBFS | -13.4728 dBFS |
| Kick_12.wav, mono 165.129 s | 4.2 / 2.1 / 1.0 dB | 8 / 4.8 / 2.8 dB | -30.517 dBFS | -8.02968 dBFS |

Les seuils grave/medium/aigu sont -24.5/-36/-43.5 dB sur la prise stereo,
-20/-29.5/-38 dB sur la prise mono. Coupures 164.1 Hz et 2 kHz sur les deux.
Pas de fichiers audio ecrits ni de peripherique audio lance. Ces mesures ne
remplacent pas l'ecoute dans Cubase. Le nouveau multibande, ses metres ARA et
la sauvegarde/reouverture du projet reel restent a valider par l'utilisateur.
Les limites Focus/Presence, notamment le refractory 90 ms et l'absence de
separation des cymbales pendant les frappes, restent valables.

## Installation Et Essai

Fermer Cubase et remplacer manuellement le dossier .vst3 ENTIER :
`build/DrumARAPrototype_artefacts/Release/VST3/Drum ARA Prototype.vst3`
dans `C:\Program Files\Common Files\ARA`. Mettre a jour la copie VST3
si elle est conservee. Verifier v0.7.0 dans l'editeur. Rien n'a ete copie
automatiquement dans les dossiers systeme ; Cubase Gain reste inchange.

Travailler sur une copie du projet. Les 47 IDs existants et l'identite du
plugin/factory/archive restent conserves ; 24 IDs sont ajoutes (71 controles).
Les anciens etats gardent Multibande OFF et leurs reglages large bande.
Les versions 0.5/0.6 peuvent ignorer les nouveaux controles de l'archive,
mais ne reproduisent pas le son multibande ; conserver la sauvegarde initiale.

1. Confirmer graphique et vumetres au-dessus des onglets.
2. Choisir Rock 50-70 %, Analyser, consulter le Bilan, puis Appliquer.
3. Dans Compresseur, verifier Multibande et les trois GR en lecture.
4. Ecouter Solo Grave, Medium et Aigu, puis retirer tous les Solos.
5. Comparer avec le bypass et les autres micros, verifier les coups faibles.
6. Tester Revenir avant toute retouche manuelle, puis sauvegarde/reouverture,
   boucle et export sur la copie du projet.

Captures : [Multibande](../build/KickMultibande.png),
[EQ et graphique global](../build/KickInterface.png),
[Large bande](../build/KickCompresseur.png),
[Focus / Presence](../build/KickPresence.png).