# Test Du Prototype ARA Dans Cubase Pro 12

## But Et Statut

Ce document conserve l'historique et les essais du prototype d'integration
0.1.x, qui restitue les regions depuis un cache RAM hors processBlock.
La livraison actuelle 0.2.0 ajoute traitement Kick et interface manuelle :
voir [Essai Kick](KickTest.md). Les validations utilisateur ci-dessous
concernent 0.1.6, pas les nouveaux traitements/reglages ARA de 0.2.0.

- Cible : Cubase Pro 12.0.52 build 393, Windows x64.
- JUCE 8.0.6 et SDK ARA releases/2.2.0, avec sous-modules.
- Compilation Release reussie et quatre suites de tests automatiques reussies.
- Detection et liaison ARA dans Cubase confirmees par capture utilisateur.
- Capture 0.1.4 : 1 region mono en cache, 0 indisponible, rendu stereo 48 kHz,
  callbacks en lecture et crete rendue -11.3 dBFS, mais toujours aucun son entendu.
- Capture 0.1.5 : cache/rendu -6.1 dBFS, toujours aucun son entendu.
- Version 0.1.6 : secours independant de l'ordre de liaison, flags de silence
   recalcules et mesure apres copie hote. Sortie audible confirmee par
   l'utilisateur le 14 octobre 2026 dans Cubase Pro 12.0.52.
- Capture 0.1.6 : 1 region en cache, 0 indisponible, 48 kHz / 2 canaux,
   cache/rendu/sortie hote -6.1 dBFS, bus actif, fallback yes. Capture prise
   transport arrete apres lecture : 1850 appels dont 941 en lecture.
   Les cretes retenues et le retour utilisateur confirment la restitution,
   pas une sortie continue pendant l'arret du transport.
- Essais complementaires confirmes reussis par l'utilisateur le 14 octobre
   2026 avec 0.1.6 : lecture depuis le milieu, boucles, sauvegarde/fermeture/
   reouverture et export ecoute, sur son projet de test. Validation manuelle
   rapportee, pas mesure automatique du placement a l'echantillon pres.
- Jalon d'integration du prototype valide dans ces conditions. Montage
   intensif, longues sessions et cache de production non valides par ces essais.
   Le succes ne permet pas d'isoler lequel des changements 0.1.6 etait decisif.
- Identite distincte de Cubase Gain : aucun remplacement du plugin existant.

### Correctif De Detection 0.1.1

Premier retour Cubase : plugin visible en insert, mais non trouve dans Extensions.
Le bundle installe a ete compare au build : DLL identiques (SHA-256), et la
classe ARA Main Factory est presente dans le manifeste genere.

Une anomalie de metadonnees a ete corrigee : informationURL etait vide, alors
que le validateur officiel du SDK ARA exige une valeur non vide. La version
0.1.1 renseigne ce champ et ajoute un test de non-regression. L'adresse
example.invalid est un placeholder explicite de developpement, pas un site
de support ; elle doit etre remplacee avant distribution. Aucun acces reseau
n'est ajoute par ce correctif. Son effet sur la detection Cubase reste a tester.

Ce correctif de metadonnees n'a pas suffi : le menu Extensions est reste absent.
La procedure d'installation ci-dessous corrige maintenant le chemin specifique
de decouverte ARA de Cubase. Une reanalyse VST seule ne remplace pas cette etape.

### Diagnostic Apres Les Captures Utilisateur

- Le menu contextuel de l'evenement est confirme : aucune entree Extensions.
   Le clic droit ouvre les outils dans cette configuration ; Ctrl + clic droit
   a permis d'afficher le menu contextuel. Le bouton e est celui de la voie,
   pas un selecteur ARA. Ne plus demander ces manipulations comme correctif.
- Le cache VST3 de Cubase contient le prototype 0.1.1 et sa classe
   ARA Main Factory Class. L'absence du menu ne prouve pas un oubli du scan.
- Components/araservice.dll existe dans l'installation Cubase ; son ARA Service
   est repertorie dans le cache des modules. Cela ne prouve pas son activation.
- ARADiscoveryTests scanne le vrai bundle, instancie son processeur VST3,
   obtient et initialise la factory ARA et verifie la compatibilite ARA 2.0.
   Test reussi sur le build et sur le bundle installe dans Common Files/VST3.
- Le chemin JUCE createARAFactoryAsync(description) a plante dans notre test
   lors du chargement direct du bundle ; le test utilise maintenant
   createARAFactoryAsync(instance). Ce crash du test n'est pas une preuve d'un
   probleme equivalent dans Cubase.
- Ces controles ne valident pas encore creation du document ARA, binding des
   roles, acces aux sources, restitution ou archivage dans un hote reel.
- Defaut d'installation identifie : le prototype est seulement dans VST3 et
   le dossier Common Files/ARA n'existe pas sur cette machine. Steinberg indique
   que les plugins ARA non encore acceptes dans son dossier VST3 doivent etre
   installes dans Common Files/ARA. Cela explique pourquoi le scan VST3 seul
   ne garantit pas leur presence dans Extensions. Confirmation dans Cubase
   encore necessaire apres correction du chemin.
- Source Steinberg, Michael / Spork, 6 janvier 2022 :
   https://forums.steinberg.net/t/ara-plug-in-not-loaded-in-cubase/756711/3
- Cas JUCE sous Cubase 12 Windows resolu avec ce chemin, juillet 2022 :
   https://forum.juce.com/t/steinbergs-daw-does-not-enable-the-ara-function/52320
- Aucun changement DSP ou version requis pour cette correction d'installation.
   Ne pas supprimer les preferences ou caches Cubase.

## Installer Manuellement

### Formats Audio Automatiques 0.1.3

La capture utilisateur confirme ARA editor apres installation dans Common Files/ARA.
Le probleme de detection est donc leve. La capture 0.1.2 a identifie le refus :
source mono 48 kHz, rendu stereo 48 kHz, region de 17.5 secondes.
Le constructeur declarait stereo par defaut ; cela ne prouve pas que Cubase
imposait ce format. La version 0.1.3 demarre en mono, accepte aussi stereo et
detecte les canaux de chaque source ARA sans reglage manuel.

- Mono vers mono : un canal natif, sans duplication.
- Stereo vers stereo : deux canaux distincts, sans modification de largeur.
- Mono vers stereo : duplication a gain unitaire sur chaque sortie.
- Stereo vers mono : moyenne 0.5 * gauche + 0.5 * droite. Les composantes en
   opposition de phase peuvent s'annuler ; ce repli n'est pas une restitution
   stereo ni une reproduction de la loi de panoramique de Cubase.

Les sources sont lues et stockees dans leur format natif. Le bus de rendu est
negocie par l'hote avant lecture ; le plugin ne le reconfigure pas dans le
callback audio pour chaque region. La RAM est comptee selon les canaux stockes.
Le test sur le vrai VST3 confirme mono par defaut et renegociation stereo/mono.
La capture 0.1.3 confirme un rendu stereo et une region mono chargee ; aucun son
n'est entendu. Cela ne suffit pas a identifier la cause du silence.

Le diagnostic affiche :

- Last rejected : raison du dernier refus lors de la preparation du cache.
- Render : frequence et nombre de canaux du dernier renderer prepare, meme
   sans echec. Cached sources : nombres de regions mono et stereo en cache.
- Source : format du dernier refus, si une region est indisponible.
- Region duration : duree de la region concernee, en secondes.
- Cache empty ou Cache partial au lieu de Cache ready si des regions manquent.

Les raisons distinguent acces source desactive, frequence/canaux incompatibles,
temps invalides, limite de 30 secondes, etirement, limite RAM/nombre de regions,
plage au-dela du fichier et lecture source en echec. Si plusieurs regions sont
refusees, seul le dernier refus est detaille. Les valeurs sont celles de la
derniere preparation, pas une lecture continue de la source depuis l'editeur.
Pas de nouvel acces source ni allocation ajoutee dans processBlock.

### Silence En Lecture : Diagnostic 0.1.4

La version 0.1.4 instrumente le lecteur sans contourner la cause inconnue.
Elle conserve les limites et le routage mono/stereo de la version precedente.

- Playback : aucun callback, transport arrete, cache invalide, position absente,
   position hors des regions ou rendu d'une region.
- Calls / playing : callbacks du lecteur et callbacks ou l'hote annonce lecture.
- Host sample : derniere position fournie au lecteur, en echantillons projet.
- Cache samples : bornes du cache, fin exclusive ; intervalle global pouvant
   contenir des trous si plusieurs regions sont disjointes.
- Cached peak : crete maximale des echantillons lus dans le cache.
- Rendered peak : crete maximale produite par le lecteur avant le gain de
   l'instance, pas une mesure de la sortie physique de Cubase.

Les cretes sont retenues depuis la derniere preparation et affichees en dBFS
avec plancher -160 dBFS. Compteurs/etat viennent du thread audio via atomiques
verifiees sans verrou a la compilation ; pas d'allocation, journalisation,
acces fichier ou lecture source ajoute dans processBlock. Les valeurs partagees
decrivent la derniere preparation et les derniers appels, pas toutes les pistes.

### Contournement Des Bus Cubase 0.1.5

La capture utilisateur 0.1.4 montre 2206 callbacks dont 992 en lecture, position
541966 dans le cache [0, 840000[, crete cache -6.1 dBFS et rendu -11.3 dBFS.
Le lecteur produit donc un signal non nul avant gain ; cela ne prouve pas que
le wrapper le transmet a Cubase. L'utilisateur confirme toujours aucun son.

Un defaut rapporte pour Cubase 12.0.52 concerne l'absence d'appel activateBus
sur les instances ARA. JUCE 8.0.6 conserve alors les bus inactifs cote hote et
son remappeur ne copie pas leurs echantillons de sortie. Sources historiques :

- https://forum.juce.com/t/ara-plugin-not-supported-by-cubase-on-macos-arm/52477
- https://forum.juce.com/t/strange-behaviour-of-ara-enabled-plugin-on-steinberg-daws/53970

La 0.1.5 active les mappings selon les arrangements acceptes uniquement pour
Cubase/Nuendo et une instance liee a ARA, sans appel audio activateBus deja recu.
Les activations/desactivations explicites de l'hote gardent la priorite.
Le correctif est applique a JUCE au configure CMake par un script versionne
qui echoue si ses ancres changent. Une macro limite son effet au prototype ;
Cubase Gain et les instances non liees conservent leur comportement normal.
Pas de nouvelle operation dans processBlock, ni changement des identifiants.

ARADiscoveryTests utilise le vrai remappeur JUCE : signal client non nul sans
copie hote quand le bus est inactif, puis copie exacte de chaque echantillon
mono/stereo quand il est actif. Les quatre suites passent, dont le chargement
du vrai bundle 0.1.5. Ce test ne reproduit pas le cycle ARA complet de Cubase ;
le declenchement du contournement et le retour audible restent a confirmer.

### Sortie Hote Et Liaison Tardive 0.1.6

La capture 0.1.5 montre 1237 callbacks dont 1006 en lecture, position 607342,
cache [0, 840000[, cache et rendu -6.1 dBFS. Toujours aucun son selon l'utilisateur.
Le premier contournement n'a donc pas resolu le probleme dans cet hote.

Deux limites locales sont corrigees : la 0.1.5 exigeait deja une liaison ARA au
moment de la negociation des bus, et tout appel activateBus, meme sur l'entree
seule, bloquait le secours de la sortie. La 0.1.6 verifie aussi les mappings
avant remappage de chaque bloc, une fois la liaison effective. Les appels
explicites concernant l'entree et la sortie sont suivis separement ; une sortie
explicitement desactivee reste desactivee. Ce secours reste limite au prototype,
a Cubase/Nuendo lies a ARA, et a ses bus uniques d'entree/sortie.

Une sonde est detruite apres le buffer remappe JUCE : elle inspecte donc les
echantillons deja copies dans les buffers fournis par l'hote, apres gain.
Elle recalcule aussi les flags VST3 de silence canal par canal : un canal non
nul ne doit pas rester marque silencieux. Ce deuxieme defaut est une hypothese,
pas une observation du contenu des flags dans Cubase pour le moment.

- Host output : crete retenue depuis preparation, canaux presents dans le
   dernier bloc et nombre de mesures. Ce n'est pas une mesure de la sortie physique.
- Output bus : dernier etat actif du mapping de sortie.
- fallback : secours de sortie applique pour ce bloc, pas confirmation d'audio audible.
- Host silence flags before check : flags avant recalcul du dernier bloc,
   pas une preuve que l'hote les a initialement fournis ni une mesure retenue.

Les compteurs sont partages au document ARA uniquement depuis le role de lecture.
Les nouvelles operations audio sont des boucles bornees et des atomiques,
sans allocation, acces fichier, journalisation ou nouveau verrou. L'editeur
mesure maintenant 640 x 520 pour afficher ces lignes. Tests du code utilise
par le wrapper : liaison tardive, entree seule, hote non concerne, instance
non liee, desactivation explicite, mesure apres copie, flags par canal et
buffers mono/stereo float/double. Les quatre suites passent. Ces tests ne
reproduisent toujours pas l'ensemble du cycle document/binding de Cubase.

Pour cet essai, remplacer le bundle par le build 0.1.6 ci-dessous, puis
retirer/reappliquer l'extension au meme evenement dans un projet de test.
Verifier 0.1.6 dans le titre et ecouter a faible volume pendant la lecture
d'une partie audible. En cas de silence, relever le diagnostic complet avant
de changer le fichier ou les reglages, y compris Host output et Output bus.

1. Fermer Cubase.
2. Copier le dossier entier `Drum ARA Prototype.vst3` depuis
   `build/DrumARAPrototype_artefacts/Release/VST3/` vers
   `C:\Program Files\Common Files\ARA\`.
3. Creer le dossier `ARA` dans l'explorateur Windows s'il n'existe pas, et
   autoriser la copie si Windows le demande. Le chemin final doit etre
   `C:\Program Files\Common Files\ARA\Drum ARA Prototype.vst3\Contents\...`.
   Copier le bundle entier, pas seulement le binaire interne.
4. Relancer Cubase, selectionner un evenement audio et verifier Audio > Extensions.

Important : pour ce prototype de developpement, le dossier VST3 seul ne suffit
pas a la decouverte ARA dans Cubase 12. La copie VST3 deja installee peut rester
pour les tests en insert ; les deux copies doivent avoir la meme version.
Le dossier ARA est celui decrit par Steinberg pour les plugins non encore
valides pour sa decouverte ARA depuis VST3. Aucune approbation commerciale
n'est necessaire pour essayer ce chemin de developpement.

Le build ne fait aucune copie dans ce dossier et ne lance aucune application audio.
Utiliser un projet de test, pas l'unique copie d'une session importante.

## Test Principal : Extension ARA

1. Creer un projet vide a 44.1 ou 48 kHz. Commencer avec un volume faible.
2. Importer un WAV mono ou stereo de 5 a 20 secondes a la meme frequence.
   Sans time-stretch, AudioWarp,
   changement de hauteur ni fades pour ce premier essai.
3. Laisser quelques mesures vides avant l'evenement pour verifier le placement.
4. Selectionner l'evenement audio, puis chercher **Drum ARA Prototype** dans
   **Audio > Extensions** ou le selecteur d'extension de l'evenement.
   Ce test n'utilise pas un emplacement Inserts du mixeur.
5. Ouvrir l'editeur de l'extension et lancer la lecture.
6. Relever le diagnostic : **ARA editor** ou **ARA playback**, **Regions cached**,
   **Unavailable** et l'etat du cache. En cas de refus, relever **Last rejected**,
   **Source**, **render** et **Region duration**. Ne pas valider la restitution
   tant que le cache est vide ou partiel.
7. Verifier le placement et l'absence de doublage ou craquement, ainsi que le
   silence avant/apres l'evenement. Sur des formats identiques, niveau et canaux
   doivent etre preserves ; en conversion, comparer avec les regles ci-dessus.
8. Lire depuis le milieu puis boucler sur une partie de l'evenement, sans
   deplacer ou retailler celui-ci. Le placement doit rester correct.
9. Sauvegarder le projet, le fermer, le rouvrir et verifier que l'extension
   et la restitution reviennent. **Archive restored** confirme la relecture
   du marqueur d'archive ARA ; aucun etat musical d'analyse n'est encore stocke.
10. Si tout fonctionne, essayer un export audio de la selection et verifier
    l'absence de silence inattendu, de decalage ou de doublage.

Les roles ARA d'edition et de restitution peuvent etre des instances differentes.
Le diagnostic est partage au niveau du document ; les compteurs representent le
dernier cache prepare, pas un inventaire global de toutes les pistes du projet.
Le gain de verification est donc reserve au mode insert a ce stade.

## Limites Volontaires

- Maximum 30 secondes par region, 128 regions et 128 Mio d'audio par renderer.
- Frequence source identique a celle du rendu ; formats mono/stereo uniquement,
  avec adaptation automatique entre ces formats. Pas de multicanal surround.
- Pas de resampling, time-stretch, preview ARA lors d'un clic ni analyse neuronale.
- Prechargement synchrone dans prepareToPlay : peut retarder la preparation.
  Ce choix borne et simple sert au test ; ce n'est pas le cache de production.
- L'hote doit avoir autorise l'acces aux echantillons au moment du prechargement.
  Sinon la region est comptee comme indisponible. Ce comportement reste a verifier
  dans Cubase ; ne pas conclure a une incompatibilite ARA sur ce seul symptome.
- Une region indisponible n'est pas restituee. Un cache invalide produit du silence
  plutot que de rejouer une version devenue incorrecte.
- Les notifications d'edition/source invalident le cache de facon conservative.
  Reconstruction automatique non implementee : retirer/reappliquer l'extension
  ou reouvrir le projet peut provoquer une nouvelle preparation par Cubase.
- Ne pas tester le montage intensif ou une chanson entiere avant ce premier jalon.
- La recompilation reussie ne prouve pas encore le bon comportement dans l'hote.

## Test Secondaire : Insert Classique

Dans un autre essai, charger le meme plugin comme insert normal. Le diagnostic
doit indiquer **VST3 insert / ARA inactive**. A 0 dB, l'audio passe sans changement.
A -6 dB, le niveau diminue avec lissage. Verifier la restauration de ce reglage
apres sauvegarde/reouverture. Ce test seul ne valide pas ARA.

## Retour Utile

- Plugin visible dans le gestionnaire VST et dans Audio > Extensions ?
- Texte exact du diagnostic, idealement capture d'ecran.
- Frequence du projet et du fichier, mono/stereo, duree de l'evenement.
- Lecture normale, depuis le milieu, boucle et reouverture : resultat de chacun.
- Si un probleme survient : etape exacte, message Cubase et presence de silence,
  decalage, doublage ou crash. Ne pas continuer dans une session importante.

## Recompiler

Depuis PowerShell a la racine du projet :

```powershell
$cmake = Join-Path $env:LOCALAPPDATA 'CubaseVST\tools\cmake-3.31.8-windows-x86_64\bin\cmake.exe'
& $cmake --preset windows-vs2019 -DENABLE_ARA_PROTOTYPE=ON
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Build.ps1 -Action Verify
```

L'option est OFF dans un build neuf, puis conservee par le cache CMake.
Pour revenir au build sans ARA, reconfigurer avec `-DENABLE_ARA_PROTOTYPE=OFF`.
Le test RegionCacheTests reste disponible sans SDK ARA ; DrumPrototypeTests
est ajoute uniquement quand l'option est active.

## Dependances Et Licences

Le SDK ARA indique Apache-2.0 pour API/Library/Examples ; conserver ses notices.
Ce prototype utilise API et Library, pas les plugins d'exemple du SDK.
Il ne change pas les obligations JUCE ni celles du SDK VST3 fourni avec JUCE.
Verifier toutes les conditions avant une distribution, distincte de ce test local.