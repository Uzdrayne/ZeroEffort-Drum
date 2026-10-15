# DRUM / KICK 0.6.0

Guide historique. Version actuelle : [Multibande 0.7.0](KickMultiband07.md).
Les mesures ci-dessous concernent uniquement le traitement 0.6.0.

## EQ Plus Cible

Quatre Q independants sont ajoutes a la fin des parametres : bodyQ, boxQ,
attackQ et airQ. Les 43 IDs precedents sont conserves, pour 47 controles.
Chaque Q peut monter a 6. La valeur zero, affichee Global, herite du Q global
historique. Les Q positifs utilises par le moteur sont bornes au minimum a 0.3.
Les transitions de Q effectif sont lissees sur 20 ms, y compris Global vers
une valeur independante. La courbe utilise exactement les memes Q que le DSP.

Nouvelles intentions Auto a 100 % :

| Profil | Body gain / Q | Box gain / Q | Attack gain / Q |
| --- | --- | --- | --- |
| Rock | +3.5 dB / 2.0 | -4.0 dB / 1.4 | +3.0 dB / 1.5 |
| Metal | +4.5 dB / 2.5 | -6.0 dB / 1.8 | +4.5 dB / 2.0 |

Les gains sont multiplies par l'intensite ; les Q restent propres au profil.
Si une resonance est detectee, sa correction s'ajoute au creux Box, avec un
plafond de -8 dB a 100 %. Naturel conserve son EQ corrective anterieure.
La frequence Body reste adaptee au corps estime, entre 45 et 90 Hz en Rock/Metal.
Sur les deux prises utilisateur, elle reste a 87.89 Hz ; le renfort est a 80 Hz.

A Rock 75 %, sans resonance corrective supplementaire, cela donne Body
+2.625 dB / Q 2, Box -3 dB / Q 1.4 et Attack +2.25 dB / Q 1.5.
Un preset deja applique n'est pas modifie : refaire Analyser puis Appliquer
pour obtenir ces nouvelles doses, ou ajuster manuellement dans l'onglet EQ.

## Sortie Positive

Output manuel va de -60 a +12 dB. Il n'y a ni ecretage interne impose ni
limiteur : un boost peut produire des echantillons au-dessus de 0 dBFS.
Le metre OUT devient rouge quand le bloc atteint ce niveau. Commencer a
volume faible et surveiller aussi le bus Cubase.

La calibration Auto conserve une sortie entre -60 et 0 dB et sa marge de
crete mesuree de -1 dBFS. Autoriser un gain manuel ne supprime pas cette
precaution. L'ID prototypeGainDb est conserve, mais sa plage normalisee VST3
change : verifier les anciennes courbes d'automation Output dans Cubase.
Les etats insert/ARA stockes en dB sont testes, y compris +6 dB.

## Navigation Et Graphique

Cinq onglets exclusifs : Filtres, Expander, EQ, Compresseur, Focus / Presence.
Les meters, gains Input/Output, bypass et commandes Auto restent accessibles.
L'EQ presente une colonne par bande avec frequence, gain et Q ; le Q global
reste disponible pour les bandes affichees Global. Le changement d'onglet
ne modifie aucun parametre audio. Diagnostic garde sa vue technique.

Le graphique est commun aux onglets :

- Axe logarithmique 20 Hz-20 kHz.
- Axe gauche EQ dB : -18 a +18 dB, graduation tous les 6 dB.
- Courbe verte : reponse cumulee des bells actives et des HP/LP actifs.
- HP ambre et LP rose : reponses separees, traits et zones translucides ;
  coupures effectives et pente 12 dB/octave indiquees. OFF retire leurs courbes.
- Axe droit OUT dBFS : 0 a -100 dBFS, independant de l'echelle EQ.
- Survol du graphique : frequence, gain cumule en dB et niveau spectral de sortie.

La case Spectre OUT active une FFT JUCE 4096 points avec fenetre Hann,
normalisee pour l'amplitude d'un sinus. Actualisation graphique 20 Hz et
decroissance visuelle a l'arret. A 48 kHz, l'espacement des bins est 11.72 Hz ;
le grave apparait donc moins lisse que la reponse analytique EQ. Ce n'est ni
une mesure LUFS/true-peak ni une analyse de fondamentale.

Le spectre vient de la sortie finale du processeur, apres tous les traitements
et le bypass. Le canal de plus forte amplitude dans chaque bloc est retenu,
sans sommer L/R ni annuler les canaux en opposition de phase. Ce n'est pas
un affichage stereo separe ; un changement de canal peut modifier le spectre.
En ARA, le processeur de lecture transmet ses blocs au document partage avec
l'editeur. Avec plusieurs lecteurs, un seul bloc complet est publie a la fois,
pas une somme spectrale de tous les lecteurs.

Capture et transfert audio utilisent des tableaux fixes et des atomiques
sans verrou ; la publication peut abandonner une image plutot que bloquer.
La FFT est executee uniquement par le timer de l'editeur. Desactiver Spectre
ou fermer l'editeur arrete la capture lorsqu'aucun autre editeur n'en demande.

## Verification

Compilation Release windows-vs2019 et quatre suites CTest. Tests du gain +6 dB,
largeur Body Q dans le vrai moteur, Q independants et gain positif sauvegardes,
archives 29/43/47 controles, calibration trois profils a trois sample-rates,
onglets exclusifs et bornes sans chevauchement. Spectre : sinus 750 Hz a
-12.04 dBFS, selectivite, canaux opposes, livraison unique, decroissance,
activation/desactivation et liberation a la fermeture de l'editeur.
Les captures passent aussi une verification de pixels du spectre produit par
le vrai processeur, sans ouverture de peripherique audio.

Mesures des deux WAV en Rock 100 %, sources non modifiees :

| Prise | RMS brut = traite | Crete traitee | COMP P75 / max |
| --- | ---: | ---: | ---: |
| trerst.wav, stereo 16.5 s | -34.3675 dBFS | -13.2537 dBFS | 3.6 / 8.0 dB |
| Kick_12.wav, mono 165.129 s | -30.517 dBFS | -8.0674 dBFS | 4.0 / 7.94 dB |

Ces mesures ne remplacent pas l'ecoute. Les limites du detecteur et du renfort
de la serie 0.5 restent valables, notamment l'anti-redeclenchement de 90 ms.
La nouvelle interface et le spectre ARA doivent encore etre verifies dans
le projet Cubase reel ; aucune validation d'ecoute n'est revendiquee.

## Installation

Fermer Cubase, puis remplacer manuellement le dossier .vst3 ENTIER :
`build/DrumARAPrototype_artefacts/Release/VST3/Drum ARA Prototype.vst3`
dans `C:\Program Files\Common Files\ARA`. Mettre aussi a jour la copie VST3
si elle est conservee. Verifier la version 0.6.0 dans l'editeur.
Pas de copie systeme automatique ni lancement audio.

Utiliser une copie du projet. Les anciens Q restent herites jusqu'a une
nouvelle application Auto ou modification manuelle. Le format d'archive
variable de 0.5 est conserve ; 0.5 peut ignorer les nouveaux Q mais ne peut
pas reproduire leur son, et les versions 0.4 ne lisent pas ce format.
Conserver une sauvegarde avant toute mise a jour ou retour a l'ancienne version.

Captures : [EQ et spectre](../build/KickInterface.png),
[Filtres](../build/KickFiltres.png), [Expander](../build/KickExpander.png),
[Compresseur](../build/KickCompresseur.png), [Focus / Presence](../build/KickPresence.png).