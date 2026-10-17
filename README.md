# ZeroEffort Drum

Plugin batterie actuel : **ZeroEffort Drum 0.10.0**, VST3 avec extension ARA,
profils instruments, **alignement de phase entre pistes (Kit / Phase)**,
Bus Glue, ambiance et finitions manuelles.
[Guide 0.10 Kit / Phase et interface](docs/KitPhase10.md) ;
[reglages par instrument et installation](docs/DrumInstruments08.md).
L'ancien exemple Cubase Gain et sa suite de tests ont ete retires du projet.

Base C++17 / JUCE 8.0.6 pour un effet batterie VST3/ARA Windows x64 utilisable
dans Cubase, mono/stereo, avec parametres lisses et sauvegardes. Pas de cible
Standalone. Le gain positif peut provoquer un ecretage : aucun limiteur ajoute.

## Prerequis

- Visual Studio Build Tools 2019 avec outils C++ x64 et Windows SDK.
- Git accessible dans le PATH ; Internet pour le premier telechargement de JUCE.
- CMake 3.31.8 portable, installe par scripts/Bootstrap.ps1 dans
  `%LOCALAPPDATA%/CubaseVST/tools`, sans installation systeme.
- Extensions VS Code Microsoft C/C++ pour F5 et CMake Tools pour les commandes
  CMake integrees, facultatives pour les taches de compilation PowerShell.

## Compiler Et Tester

Depuis VS Code : executer la tache **Build and Test Release**.
La premiere configuration telecharge JUCE et compile son outil juceaide.
Les compilations suivantes reutilisent les dependances dans build/.

Depuis un terminal PowerShell a la racine du projet :

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Build.ps1 -Action Verify
```

Pour Debug : tache **Build Debug** ; F5 avec Microsoft C/C++ lance ensuite
**ZeroEffort Drum Tests (Debug)**, sans ouvrir de peripherique audio.
Debugger le plugin charge dans Cubase demande une configuration d'attachement
au processus Cubase, pas le lancement du programme de tests.
Trois suites : RegionCacheTests, ZeroEffortDrumTests et ARADiscoveryTests.

## Utiliser Dans Cubase

Le bundle genere est
`build/ZeroEffortDrum_artefacts/Release/VST3/ZeroEffort Drum.vst3`.
Fermer Cubase et copier le dossier .vst3 entier, pas seulement sa DLL, dans
`C:\Program Files\Common Files\ARA` pour l'extension, et dans le dossier VST3
scanne pour l'insert. Aucune copie systeme ni lecture audio automatique.
Ajouter une nouvelle instance **ZeroEffort Drum** sur une copie du projet.
Voir [Installation et essais](docs/DrumInstruments08.md).

## ZeroEffort Drum

### Kit / Phase Et Interface 0.10.0

Les instances d'un meme **groupe de kit** se decouvrent entre elles. **Analyser
le kit** puis lire le projet : retard d'arrivee et polarite de chaque micro
proche sont mesures contre les overheads (premiere arrivee, pas les
reflexions), puis appliques en tete de chaine sans latence (mode Auto) ou sur
**Appliquer** (mode Suggerer), doses par **Force**. Positions Kick In/Out,
Snare Top/Bottom, Overheads et Room Stereo/Gauche/Droite. Scores de coherence
de phase avant/apres, stereo et room. Sur la prise de reference : Kick +3.03 ms,
Snare +2.91 ms, coherence 30 -> 91 / 100.

Nouvelle interface : potards a arc lumineux, panneaux, onglets dans l'ordre du
signal (Kit / Phase, Filtres, Expander, Focus ou Nettoyage, EQ, Compresseur,
Bus, Precision, Impact) et noms selon l'instrument (Kick : Poids / Carton /
Batte / Air ; Snare : Corps / Bague / Claque / Timbre...).
Architecture, revue critique et limites : [KitPhase10.md](docs/KitPhase10.md).

### Finitions 0.9.0

Deux nouvelles pages : **Impact** (Crush parallele, couleur surechantillonnee
4x, soft clip) et **Precision** (Attack/Sustain, deux bandes d'EQ dynamique,
option de corps mesure Snare/Toms). Le bus ajoute caractere et densite de
room ; l'en-tete propose niveau A/B et ecoute mono.
Style/intensite Auto sont sauvegardes en insert et dans les reglages ARA.
Les anciens reglages restent compatibles ; rien ne s'active au chargement.

Choisir role/style/Intensite puis **Analyser** : apres analyse, calibration et
verification, Auto active les finitions utiles ; **la proposition attend votre choix**.
Un panneau polaire se construit pendant l'analyse (mesures de la source), le calcul
(valeurs visees, source en transparence) et la verification (valeurs obtenues sur le son
traite) ; **Appliquer** applique les reglages, **Ignorer** ne change rien. Les modules
actives et les reglages principaux changes sont listes a droite ; survoler un axe donne
ses valeurs. **Revenir** restaure l'etat precedent. A zero, les nouvelles finitions sont OFF.
Crush, couleur, impact, EQ dynamique, clip et ambiance suivent le role, le crest,
les niveaux et le spectre ; les cymbales ne recoivent pas de distorsion/Crush/room.
Les presets affines utilisent davantage ces outils : Crush/couleur/impact doses
progressivement, EQ dynamique corrective ou de controle large, soft clip a partir
de 35 % en Rock/Metal (65 % Naturel), et Bleed Clean sur Kick/Snare/Toms si la
repisse et le contraste le justifient. Les seuils suivent le niveau de la prise.
Le corps Snare/Toms reste mesure lorsque la confiance est suffisante. Relancer
Analyser pour beneficier des nouveaux presets ; aucun ancien etat n'est reecrit.
La courbe ambre montre l'EQ dynamique mesuree en direct, superposee a l'EQ verte.
Seize indicateurs avec activite, unites, OFF/BYP et correlation restent sous le
graphique, en haut, quelle que soit la page. En ARA ils suivent la premiere
modification selectionnee, pas la derniere piste rendue.

A/B est une compensation RMS avant le gain Output final, pas LUFS ; soft clip est une
coloration parallele, pas un plafond garanti. Pas de vraie plate. La liaison
entre micros est traitee par Kit / Phase (0.10). Details et limites dans le
[guide courant](docs/DrumInstruments08.md).

### Instruments Et Auto Preset 0.8.0

Le plugin contient maintenant une vraie chaine manuelle **DRUM / KICK** :
filtres HP/LP, expander, EQ quatre bells, compresseur lie stereo et mix parallele,
gains, bypass et meters. Le moteur est dans
[Source/Drum/DrumEngine.h](Source/Drum/DrumEngine.h), pas dans le dossier tests.
L'interface JUCE et le traitement reel ARA/insert sont raccordes dans
[Source/ARA/ZeroEffortDrumProcessor.cpp](Source/ARA/ZeroEffortDrumProcessor.cpp).
La chaine manuelle 0.2.0, ses commandes et la sauvegarde/reouverture ont ete
valides par l'utilisateur dans Cubase le 14 octobre 2026. La 0.3.0 ajoute
Analyser / Appliquer / Revenir : analyse ARA incrementale d'un evenement,
ou capture insert de 8 secondes, proposition corrective prudente sans boost.
La 0.3.1 separe les spectres d'attaque/decroissance/intervalles pour cibler
les resonances et l'aigu persistant ; le bilan nomme les corrections reelles
ou indique EQ neutre. Quatre suites automatiques passent, dont resonance,
repisse et coups faibles. La prise utilisateur a aussi ete analysee en lecture
seule : resultat EQ neutre, pas de correction suffisamment fiable.
La 0.4.0 ajoute les intentions Naturel/Rock/Metal, l'intensite et une
calibration du vrai DSP en deux passes apres analyse. La reduction maximale
du compresseur et le RMS/les cretes sont mesures, la sortie est ajustee.
La 0.4.1 calibre le P75 des blocs actifs avec plafond maximal : compression
Rock/Metal renforcee, mix 100 %, makeup mesure et vumetre COMP 0-12 dB avec
relachement visuel. Sur le WAV utilisateur : Rock P75 3.2/max 8 dB, Metal
P75 4.3/max 10 dB ; les deux retrouvent le RMS brut sans ecretage mesure.
La courbe EQ inclut maintenant HP/LP, coupures et pente fixe 12 dB/octave,
avec courbe filtres seule ambre et reponse cumulee verte. Coloration volontaire
distincte de la detection de defauts. Ecoute et validation Cubase requises.
Tout demarre neutre/desactive ; rien n'est applique
automatiquement. Le nom du bundle et ses identifiants restent inchanges.

La 0.4.2 supprime le refus arbitraire des regions ARA de plus de 30 secondes.
Le cache reste borne a 128 Mio et 128 regions par lecteur ; le cas mono
48 kHz de 231.448 secondes et sa lecture a trois minutes sont testes.
Les lectures source et allocations restent hors du callback audio.

La 0.4.2 est confirmee audible par l'utilisateur dans son vrai projet.
La 0.4.3 resserre le Q des quatre bandes EQ Auto (0.85/1.0/1.2) sans modifier
la resonance HP/LP ni les reglages sauvegardes. EXP utilise maintenant les
statistiques de son enveloppe, des releases 160/100/80 ms et une echelle
0-12 dB avec relachement visuel. Le Bilan donne son seuil, ses niveaux de
detection et son action mesuree. Sur la nouvelle prise riche en repisse,
Rock attenue jusqu'a 8 dB, Metal 12 dB, sans changement du RMS global mesure.
Cela ne separe pas les cymbales jouees pendant les frappes de kick.

La 0.5.0 ajoute un detecteur grave filtre avec hold/hysteresis, un nettoyage
selectif de l'aigu entre les frappes, un renfort sinusoidal accordable,
un accent de transitoire et une saturation parallele du grave. Les 14 nouveaux
controles sont dans la vue Focus / Presence ; le Bilan defilant contient les
mesures du rendu et les 43 valeurs finales. Auto les propose selon le profil
et l'intensite ; Analyser ne modifie toujours pas le son avant Appliquer.
Le renfort est synthetique, pas une correction du pitch de l'enregistrement.
Les anciens etats se chargent avec les nouveaux modules desactives ; une
archive sauvegardee en 0.5.0 n'est pas lisible par les anciennes versions.

La 0.6.0 ajoute quatre Q independants, des presets Rock/Metal avec un corps
plus etroit et marque, un creux medium plus profond et une attaque plus ciblee.
La sortie manuelle va maintenant jusqu'a +12 dB ; l'Auto garde sa limite a 0 dB.
Cinq onglets regroupent les reglages et conservent le graphique et les gains
globaux visibles. Le graphique affiche l'echelle EQ en dB, HP/LP separes en
transparence et un spectre temps reel de sortie en dBFS, avec lecture au survol.
La FFT reste sur le thread graphique ; la capture audio est sans verrou.

La 0.7.0 place le graphique et les vumetres au-dessus des onglets et ajoute
un compresseur trois bandes, avec coupures LR24, seuil/ratio/attaque/release,
makeup, plafond de reduction et Solo par bande. Analyse calibre les trois
seuils sur les enveloppes du vrai moteur, puis verifie reductions, RMS et
cretes. Le Bilan distingue cibles et mesures ; Appliquer reste obligatoire.
Les anciens etats conservent le compresseur large bande. Les coupures a
phase minimale demandent une verification a l'ecoute avec les autres micros.

La 0.8.0 ajoute le choix explicite Kick, Snare, Tom 1/2/3, Tom basse, Hi-hat,
Overheads, Room et Drum Master Bus. Le role est sauvegarde ; le choisir ne
change pas le son avant Analyser / Appliquer. Les nouveaux profils sont
heuristiques et prudents, pas une reconnaissance ni une separation de sources.
Kick conserve son analyse 0.7. La page Bus ajoute une compression Glue liee
stereo a knee doux/sidechain HP/GR max, et une petite ambiance JUCE manuelle.
Ce n'est pas une emulation SSL. Auto Master calibre la Glue mais ne regle pas
les niveaux individuels des pistes. Utiliser un insert sur le groupe Drum.
Bypass ARA peut agir sur la derniere selection editee si la selection disparait,
tant que ces cibles existent ; le cas utilisateur reste a confirmer dans Cubase.

Le produit et le bundle sont renommes **ZeroEffort Drum**. Cibles CMake :
`ZeroEffortDrum`, `ZeroEffortDrum_VST3`, `ZeroEffortDrumTests`.
Le code VST3 et les IDs bundle/factory/archive ARA sont nouveaux : les anciennes
instances du prototype ne sont pas remplacees automatiquement dans les projets.
Ajouter de nouvelles instances pour ces essais de developpement.
Bundle actuel :
`build/ZeroEffortDrum_artefacts/Release/VST3/ZeroEffort Drum.vst3`.
ZeroEffort Drum est maintenant la cible principale, toujours compilee ;
l'ancien commutateur `ENABLE_ARA_PROTOTYPE` n'est plus utilise.
Les sections 0.1.x suivantes sont historiques.

Installation et essai actuels : [Instruments et Drum Bus 0.8.0](docs/DrumInstruments08.md).
Historique multibande : [Multibande et Interface 0.7.0](docs/KickMultiband07.md).
Historique EQ : [EQ et Interface 0.6.0](docs/KickInterface06.md).
Historique Presence : [Presence Kick 0.5.0](docs/KickPresence05.md).
Historique Auto : [Auto Musical 0.4.3](docs/KickAuto04.md).
Chaine manuelle et historique Auto : [Drum / Kick](docs/KickTest.md).
Apercu reel : [Interface Kick](build/KickInterface.png).
Vue des nouveaux reglages : [Focus et Presence](build/KickPresence.png).
Compression trois bandes : [Multibande](build/KickMultibande.png).
Vue Bus : [Glue et Ambiance](build/DrumMasterBus.png).

### Historique D'integration 0.1.6

Une cible separee **Drum ARA Prototype** teste l'integration des evenements audio
avec Cubase Pro 12.0.52 sans changer l'identite de Cubase Gain. Ce n'est pas
encore le plugin de traitement drum. Compilation optionnelle via
`-DENABLE_ARA_PROTOTYPE=ON` ; le SDK ARA 2.2.0 est alors telecharge.

Installation manuelle, limites et procedure de validation :
[Test du prototype ARA](docs/ARAPrototypeTest.md).
Pour Cubase 12, copier manuellement ce prototype de developpement dans
`C:\Program Files\Common Files\ARA\`, pas uniquement dans `VST3`.
Ce chemin ARA specifique est requis par la procedure developpeur de Steinberg.
La detection ARA dans Cubase est confirmee. Le prototype 0.1.6 demarre en mono
et adapte automatiquement les sources mono/stereo au bus negocie par l'hote.
Il est compile et teste automatiquement et detaille les refus de chargement.
Les captures 0.1.4 et 0.1.5 confirment un rendu non nul mais aucun son entendu.
La 0.1.6 gere aussi une liaison ARA tardive et une activation de l'entree seule,
sans reactivation d'une sortie explicitement desactivee par l'hote.
Elle mesure les buffers de sortie apres copie vers l'hote et recalcule leurs
indicateurs VST3 de silence selon les echantillons transmis. Tests mono/stereo
float/double reussis. Sortie audible confirmee par l'utilisateur avec 0.1.6
dans Cubase Pro 12.0.52 : cache, rendu et buffers hote a -6.1 dBFS,
bus actif et secours applique. Lecture depuis le milieu, boucles,
sauvegarde/reouverture et export confirmes reussis par l'utilisateur le
14 octobre 2026 sur son projet de test. Ce jalon valide le prototype dans
ces conditions, pas le montage intensif ni les longues sessions.
La chaine manuelle ajoutee en 0.2.0 a ensuite ete validee par l'utilisateur ;
le nouveau workflow Auto 0.3.0 est decrit ci-dessus.
Le correctif JUCE est reapplique au configure CMake depuis
[cmake/PatchJuceARABuses.cmake](cmake/PatchJuceARABuses.cmake), uniquement actif
pour cette cible. L'ancien exemple Gain a depuis ete retire du projet.
Le cahier des charges est dans [plan.md](plan.md).

## Developpement

- [Source/ARA/ZeroEffortDrumProcessor.cpp](Source/ARA/ZeroEffortDrumProcessor.cpp) : processeur et interface ARA/insert.
- [Source/Drum/DrumEngine.h](Source/Drum/DrumEngine.h) : moteur audio partage.
- [Source/Drum/DrumProposal.h](Source/Drum/DrumProposal.h) : `DrumStyle`, `drumStyleName` et `DrumProposal` communs.
- [Source/Drum/DrumAnalysis.h](Source/Drum/DrumAnalysis.h) : mesures, FFT, histogrammes et helpers temporels Kick conserves.
- [Source/Drum/DrumAutoPreset.h](Source/Drum/DrumAutoPreset.h) : `DrumAnalyser`, validation des options et dispatch explicite des roles.
- [Source/Drum/Profiles/KickProfile.h](Source/Drum/Profiles/KickProfile.h) : proposition Kick ; profils Snare, Tom, HiHat, Overheads, Room et MasterBus dans le meme dossier.
- [Source/Drum/Profiles/ProfileAssembly.h](Source/Drum/Profiles/ProfileAssembly.h) : assemblage commun non-Kick, sans decisions sur le role.
- [tests/ZeroEffortDrumProcessorTests.cpp](tests/ZeroEffortDrumProcessorTests.cpp) : tests DSP, processeur et etats.
- [Source/Kit/](Source/Kit/) : registre multi-instances, capture horodatee, analyse de phase et alignement temps reel.
- [Source/UI/](Source/UI/) : editeur, LookAndFeel et noms des reglages selon l'instrument.
- [tests/KitTests.cpp](tests/KitTests.cpp) et [tests/EditorTests.cpp](tests/EditorTests.cpp) : kit (dont les prises reelles Kick_12, Snare_12, OvH L_12, OvH R_12 si `ZED_WAV_DIRECTORY` est defini ; elles sont hors du depot) et interface, dans la suite ZeroEffortDrumTests.
- [tests/Screenshots.cpp](tests/Screenshots.cpp) : captures de presentation (2x) a partir des memes prises (`ZED_WAV_DIRECTORY` requis), hors CTest :
  `build\ZeroEffortDrumTests_artefacts\Release\ZeroEffortDrumTests.exe --screenshots screenshot`.
- [CMakePresets.json](CMakePresets.json) : Visual Studio 2019 x64, Release et Debug.

Les modules communs portent le prefixe `Drum` (archive, capture, analyse,
moteur, filtres, multibande, preview, spectre). `KickPresenceDSP` conserve son
nom : detection grave, nettoyage et renfort synthetique propres au Kick.
Les types communs utilisent aussi `Drum` : moteur, capture, preview, reponse
des filtres, multibande et archives. Les profils portent leurs coupures, EQ,
temps et limites ; les quatre roles Tom partagent le meme profil.
Refactorisation a comportement constant : aucun reglage DSP, resume, ID de
parametre/plugin ni format d'etat/archive modifie. La reference de proposition
figee avant extraction dans [tests/LegacyDrumProposalReference.h](tests/LegacyDrumProposalReference.h)
verifie exactement les 88 reglages, validite, candidats, cibles/plafonds et
resumes sur 4 110 cas a 44.1/48/96 kHz, dont options invalides et refus.

Conserver les identifiants des parametres et du plugin pour la compatibilite
des projets Cubase. Le nom du fabricant et les codes actuels sont des valeurs
de developpement a personnaliser avant une premiere distribution.
Verifier les conditions de licence de JUCE 8 (licence JUCE ou AGPLv3) et du SDK
VST3 fourni avec JUCE avant de distribuer le plugin. Aucune licence n'est
attribuee automatiquement au code de ce projet.