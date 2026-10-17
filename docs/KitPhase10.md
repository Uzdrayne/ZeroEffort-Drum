# ZeroEffort Drum 0.10.0 - Kit / Phase Et Nouvelle Interface

Guide courant de la version 0.10.0. Les reglages par instrument, Auto, Bus et
finitions restent decrits dans [DrumInstruments08.md](DrumInstruments08.md).
Source de la demande : `MajorUpdate/ARA_update1.md` et `MajorUpdate/prompt.txt`.

## Etat Des Lieux Avant 0.10

Constat fait dans le code 0.9.0 (et non dans la description de `prompt.txt`,
qui melange existant et souhaits) :

| Sujet | Reellement present en 0.9 | Ecart avec prompt.txt |
|---|---|---|
| Plateformes | Windows x64, VST3 + ARA, pas de Standalone | macOS non construit ni teste |
| Roles | 10 roles explicites (Kick, Snare, Tom 1-3, Tom basse, Hi-hat, Overheads, Room, Bus) | pas de Snare Bottom, Kick Out, OH L/R separes |
| Analyse Auto | evenement ARA entier ou capture insert de 8 s ; crete, RMS, crest, spectre, enveloppes | pas de reconnaissance d'instrument |
| Traitements | filtres, expander, EQ 4 bandes, compresseur / multibande, Focus/Presence Kick, Glue, room, Crush, couleur, clip, Attack/Sustain, EQ dynamique, A/B RMS | "Sustain processor" = Attack/Sustain ; "Gate" = expander |
| Bus | role "Drum Master Bus" + Glue, analyse de la somme | pas de mode "Auto Detect 30 s" distinct : meme flux Analyser |
| Inter-pistes | aucune communication entre instances | c'est l'objet de cette mise a jour |
| Interface | editeur dans le fichier du processeur, coordonnees fixes, onglets hors ordre du signal (EQ avant Focus, Bus apres Finitions...), noms anglais generiques (Body / Box / Attack / Air) quel que soit l'instrument | demande : onglets dans le bon ordre, noms selon le profil |

## Ce Qui Est Livre

### Kit / Phase (multi-instances)

- Chaque instance s'enregistre dans un registre commun au processus hote
  (64 pistes et plus si besoin, slots reutilises), dans un **groupe de kit**
  (Kit 1 a Kit 8 : un groupe par batterie / projet ouvert).
- Nouveau parametre de **position micro** selon le role : Kick In / Kick Out,
  Snare Top / Snare Bottom, Overheads ou Room Stereo / Gauche / Droite.
- **Analyser le kit** (depuis n'importe quelle instance du groupe) puis lire le
  projet : chaque instance publie son audio brut horodate sur la timeline ;
  un thread d'analyse commun mesure arrivees et polarites, puis publie un
  rapport lu par toutes les instances du groupe.
- Corrections appliquees **en tete de chaine** dans chaque instance : retard
  entier (sans filtrage) avec fondu 20 ms et polarite en rampe. **Aucune
  latence ajoutee** : les micros proches sont retardes sur les overheads
  (reference de temps) au lieu d'avancer les overheads.
- Modes par piste : **Analyse** (mesure seule, jamais modifiee), **Suggerer**
  (par defaut : rien ne change avant validation dans l'apercu), **Auto**
  (application a la fin de l'analyse, sans apercu). **Force** 0-100 % dose le retard
  (0 % aucun, 50 % partiel pour garder du realisme, 100 % complet).
- **Apercu du kit** : a la fin de l'analyse, l'instance qui l'a lancee affiche
  toutes les corrections proposees (une case par piste). **Appliquer la selection**
  corrige seulement les pistes cochees, **Fermer sans appliquer** ne change rien,
  **Remettre le kit a zero** annule retard et polarite sur tout le groupe.
  L'apercu se rouvre avec **Apercu et application**.
- **Reglage manuel** apres coup, piste par piste (onglet Kit / Phase, panneau
  Correction) : potard **Delai** (saisie directe, double-clic = 0 ms), boutons
  **- / +** d'un echantillon, interrupteur **Polarite inversee**. La valeur
  proposee par l'analyse reste affichee sous le potard.
- Scores 0-100 : **coherence de phase** avant / apres, **coherence stereo**
  des overheads, **compatibilite room** avant / apres.
- Room : jamais avancee (impossible sans latence, et le retard de la piece fait
  sa profondeur) ; seule sa polarite peut etre corrigee. Le bus maitre est hors kit.
- Bypass compare aussi l'alignement. Tout est sauvegarde avec le projet
  (groupe, position, mode, force, retard, polarite), sur les evenements en ARA.

### Interface

- Code deplace dans `Source/UI` : `ZeroEffortDrumEditor.cpp`,
  `DrumLookAndFeel.h` (potards a arc lumineux et graduation pointillee, switchs
  pilule pour l'activation des modules, onglets pilule, panneaux arrondis),
  `DrumControlNames.h` (vocabulaire par instrument).
- Onglets dans l'**ordre reel du signal** : Kit / Phase, Filtres, Expander,
  Focus / Presence (ou Nettoyage), EQ, Compresseur, Bus, Precision, Impact.
- Noms selon le profil choisi. EQ : Kick = Poids / Carton / Batte / Air,
  Snare = Corps / Bague / Claque / Timbre, Toms = Fondamentale / Carton /
  Attaque / Air, Hi-hat = Repisse / Gong / Durete / Brillance, Overheads =
  Grave / Boue / Presence / Brillance, Room et Bus de meme. EQ dynamique,
  filtres (aide "repisse du Kick", "souffle"...), conseils expander suivent
  aussi le role. Focus / Presence devient "Nettoyage" pour Snare / Toms (sans
  le corps synthetique Kick) et disparait pour Hi-hat / OH / Room / Bus sauf
  s'il est actif. Les IDs et noms de parametres hote ne changent pas.
- Libelles des potards en francais (Seuil, Ratio, Attaque, Relache...), valeurs
  formatees (Hz / kHz, dB, ms, %, :1), Entree / Sortie toujours a droite avec
  Niveau A/B et Mono.

## Utilisation Dans Cubase

1. Inserer ZeroEffort Drum en **premier** insert de chaque piste du kit (un
   plug-in latent place avant fausserait la mesure).
2. Choisir l'instrument puis, dans le **deuxieme menu de l'en-tete** (a droite
   de l'instrument), la position : Kick Out, Snare Bottom, Overheads Gauche /
   Droite... Garder le meme groupe (Kit 1) pour toutes les pistes du kit.
   Un Kick Out seul (sans Kick In) est mesure directement contre les overheads.
3. Onglet **Kit / Phase** : laisser le mode **Suggerer**, cliquer **Analyser le
   kit**, lancer la lecture sur un passage joue (20 a 45 s). La barre progresse
   jusqu'a 24 frappes par micro proche.
4. A la fin, l'apercu s'ouvre : decocher ce qu'on ne veut pas, **Appliquer la
   selection** (ou fermer sans rien changer). Ecouter, ajuster **Force**, le
   **Delai** (- / + un echantillon) ou la polarite piste par piste.
   En ARA, selectionner les evenements de la piste avant un reglage manuel.

## Mesures Sur La Prise De Reference

Fichiers `wav_test_file` (48 kHz / 24 bits, 165 s) : Kick, Snare, OH L, OH R.
Teste dans `tests/KitTests.cpp` (fenetre 5 s - 35 s).

| Paire | Resultat | Remarque |
|---|---|---|
| Kick -> OH | +3.03 ms, polarite normale, r 0.51 | premiere arrivee |
| Snare -> OH | +2.91 ms (L 2.73 / R 3.13), normale, r 0.57 | la reflexion a +9.8 ms est ignoree |
| OH L / OH R | ecart 0.40 ms, correlation 0.65 | coherence stereo 81 / 100 |
| Coherence de phase | 30 -> 91 / 100 | sans correction le Snare annule partiellement les OH (r0 = -0.35) |

Le prototype Python (correlation simple) trouvait Kick +7.0 ms inverse
(r -0.58) : c'est la demi-periode suivante de la composante ~120 Hz
(3.0 + 4.2 ms). Une enveloppe blanchie (SCOT) sur la premiere arrivee evite ce
piege et aligne le transitoire du beater. Une fenetre de +/-5 ms seule aurait
coupe ce kick dans d'autres prises : 12 ms pour le Kick, 10 ms pour Snare /
Toms, 25 ms pour Room, toujours avec la contrainte physique "l'overhead ne peut
pas entendre avant le micro proche" (min -0.5 ms).

## Architecture (Les 20 Points De prompt.txt)

### 1. Vue d'ensemble

```text
 Piste Kick            Piste Snare            Piste OH L ...        (1 instance = 1 piste)
 processBlock          processBlock           processBlock          thread audio de chaque piste
   | audio brut + position timeline (sans verrou, sans allocation)
   v                     v                      v
 Member (anneau)       Member (anneau)        Member (anneau)       kit::Member, 2 x 131072 echantillons
   \_____________________|______________________/
                         v
              kit::Registry (SharedResourcePointer, 1 par processus hote)
                 | thread "ZeroEffort Kit Analysis" (basse priorite)
                 v
              KitSession par groupe : onsets -> spectres croises -> rapport
                 | publication (copie sous mutex, hors thread audio)
                 v
 timer message 5 Hz de chaque instance : mode Auto -> parametres kitDelayMs / kitPolarity
                 v
 KitAligner en tete de chaine (thread audio, lit seulement ses propres parametres)
```

### 2. Classes et responsabilites

| Classe | Fichier | Role |
|---|---|---|
| `kit::Registry` | Kit/KitRegistry.h/.cpp | slots, identites, groupes, thread d'analyse, rapports |
| `kit::Member` | Kit/KitRegistry.h | capture horodatee sans verrou (ecriture audio, lecture validee) |
| `kit::KitSession` | Kit/KitAnalysis.h | detection d'attaques, accumulation GCC, recherche d'arrivee, solveur, scores |
| `kit::OnsetDetector` | Kit/KitAnalysis.h | attaques sur micro proche (cretes par trames, refractaire 120 ms) |
| `kit::KitAligner` | Kit/KitAligner.h | retard entier + polarite, fondus, temps reel |
| `kit::Identity / Report / Recommendation / GroupView` | Kit/KitTypes.h | modele de donnees partage (valeurs, pas de pointeurs) |
| `ZeroEffortDrumProcessor` | ARA/ | parametres kit, capture, alignement, mode Auto (`kitTick`) |
| `drum::ui::DrumEditor` | UI/ZeroEffortDrumEditor.cpp | page Kit / Phase et rapport |

### 3. Propriete des donnees

- Le registre possede les `Member` (unique_ptr jamais liberes avant le registre ;
  un slot libere est reutilise par la prochaine instance).
- Chaque instance possede un pointeur brut vers *son* slot, valable pendant toute
  sa vie ; elle seule y ecrit.
- Le thread d'analyse possede exclusivement les `KitSession`.
- Les rapports sont des valeurs copiees (pas de pointeur partage entre threads).
- Les reglages kit (groupe, position, mode, force, retard, polarite) sont des
  controles `drum::Settings` comme l'instrument : **parametres** en insert,
  **valeurs des evenements ARA** en ARA. Sauvegarde, automation (insert) et
  rappel de projet passent par l'hote, pas par le registre.

### 4. Modele de threads

| Thread | Fait | Interdit |
|---|---|---|
| Audio (par piste) | `Member::write`, `KitAligner::process`, lecture atomique des parametres | verrou, allocation, journal |
| Analyse (1 par processus) | lecture validee des anneaux, FFT, rapports | toucher aux parametres |
| Message | join/leave, identite, start/stop, lecture des rapports, ecriture des parametres en Auto | calcul lourd |

Le mutex du registre n'est jamais pris par le thread audio ; le calcul lourd est
fait hors mutex (seules la preparation et la publication le prennent).

### 5. Enregistrement et decouverte

`join()` dans le constructeur du processeur, `leave()` dans le destructeur.
L'identite (groupe, instrument, position, role, nom de piste Cubase recu par
`updateTrackProperties`) est republiee par `kitTick()` a chaque changement.
`view(groupe)` liste les membres du groupe et leur activite (audio recu depuis
moins de 600 ms).

### 6. Modele partage

`Identity` (groupe, instrument, position, role, libelle), `MemberView`,
`Report` (recommandations par `memberId`, scores, lignes de texte, progression,
final), `GroupView` (membres, statut, rapport, revision atomique par groupe).
Pas d'`AudioPreview` partage : l'audio reste dans l'anneau de son proprietaire.

### 7. Duree de vie

- Instance detruite pendant une analyse : la session est annulee ("composition
  du kit modifiee"), jamais de lecture d'un slot detruit (slots persistants,
  epoch incremente au depart).
- Derniere instance detruite : le registre arrete son thread puis libere tout.
- Changement de role / groupe pendant l'analyse : annulation propre.

### 8. Communication

Aucune communication directe entre instances : ecriture dans son slot,
lecture du rapport du groupe. Le numero de revision par groupe evite les copies
inutiles ; l'application Auto se fait sur le thread message de chaque instance.

### 9. Stockage des resultats

En memoire seulement (rapport du dernier passage par groupe). Les corrections
appliquees sont persistantes car ce sont des parametres. Un rapport n'est jamais
rappele d'un ancien projet : relancer l'analyse apres une modification de prise.

### 10. Alignement temporel

1. Capture : chaque bloc est ecrit a `timeline & (taille - 1)` ; un saut de
   transport incremente l'epoch et redemarre la zone contigue.
2. Fenetre commune : fin = minimum des fins ecrites des membres actifs (ASIO
   Guard traite certaines pistes en avance : le minimum l'absorbe).
3. Attaques sur chaque micro proche (son instrument y domine), fenetre de 43 ms
   commencant 2 ms avant l'attaque ; fenetre +/-25 ms autour pour l'autre micro.
4. Spectres croises accumules (frappes paires / impaires a part) dans la bande
   du role : Kick 30-250 Hz, Snare 150-8000, Toms 60-4000, Tom basse 40-3000,
   Hi-hat 1-12 kHz.
5. Enveloppe analytique de la correlation **blanchie SCOT** : premier pic
   prominent >= 75 % du maximum dans [-0.5 ms ; fenetre du role] = premiere
   arrivee. Puis extremum de la correlation simple le plus proche
   (+/- quart de periode du centroide), interpolation parabolique.
6. Solveur : retard du micro proche = moyenne ponderee |r| des retards vers OH L
   / OH R ; secondaires (Snare Bottom, Kick Out) derives du primaire ; sans
   overheads, alignement primaire / secondaire seulement.

### 11. Polarite

Signe de la correlation simple **au retard de premiere arrivee** (et non
comparaison corr(A,B) / corr(A,-B) a retard nul, qui confond retard et
polarite). Snare Bottom : polarite relative a Snare Top combinee a celle du Top
dans le repere des overheads. Mesure retenue si |r| >= 0.12 sur >= 6 frappes et
correlation max >= 0.15.

### 12. Coherence de phase

Par paire : `0.55 x qualite large bande + 0.30 x qualite grave (30-200 Hz)
+ 0.15 x stabilite` ; qualite = correlation obtenue / meilleure correlation
possible, ramenee de [-1 ; 1] a [0 ; 1] ; stabilite = ecart entre retards des
frappes paires et impaires (exp(-ecart / 0.5 ms)). "Avant" = etat brut,
"apres" = corrections recommandees. Stereo : `0.4 correlation OH a 0 ms +
0.35 exp(-ecart d'arrivee / 1.5 ms) + 0.25 equilibre spectral 4 bandes`.
Room : `0.35 polarite + 0.45 correlation + 0.2 retard plausible (0.3-25 ms)`.

### 13. Cas d'echec

| Cas | Comportement |
|---|---|
| transport arrete / position absente | capture marquee inactive, statut "Lancez la lecture" |
| instance du groupe qui ne joue jamais (instance d'editeur hote, autre projet) | ignoree des le depart, comptee a part dans le rapport |
| piste qui a joue mais se tait au depart | 1.5 s d'attente, puis analyse avec les pistes en lecture |
| frequences differentes | analyse refusee |
| piste mute / desactivee | membre exclu de la fenetre commune, lectures refusees |
| saut, boucle | epoch change, la fenetre repart, l'accumulation continue |
| retard thread d'analyse > anneau | saut en avant, frappes perdues mais jamais de donnees ecrasees |
| aucune frappe franche | "sans mesure fiable" apres 45 s de lecture |
| plug-in latent avant ZeroEffort | non detectable : documente (premier insert) |
| ecart > 20 ms | retard borne a 20 ms et signale |
| correlation la plus forte hors de la fenetre de recherche (par exemple snare 14 ms apres les OH, kick entendu 5 ms avant son micro) | par defaut : correction fausse possible (constat T3). **Garde-fou `KitPolicy::refuseOutsideWindow`, desactive par defaut** : mesure refusee avec une note |
| un autre instrument couvre le micro (snare a -14 dB et plus dans le micro du kick) | par defaut : le retard du kick suit la snare (constat T3). **Garde-fou `KitPolicy::refuseCrosstalk`, desactive par defaut** : mesure refusee au-dessus de -15 dB de diaphonie K04 (hypothese H) |

### 14. Performance

Audio : une copie de 2 canaux par bloc + retard (quelques operations par
echantillon). Analyse : par frappe et par paire 3 FFT reelles de 8192 points
(48 kHz) au plus, cache par frappe ; par rapport 4 IFFT complexes par paire.
Memoire : 1 Mo par instance (anneau 2.7 s a 48 kHz, 0.7 s a 192 kHz) +
~100 ko par paire pendant l'analyse. Mesure de test : la suite kit complete
(4 pistes x 10 s simulees, plus 30 s de prise reelle) s'execute en moins d'une
seconde.

### 15. Evolutions

Le meme registre porte deja les spectres par piste : masquage frequentiel,
conflits Kick / Toms, equilibre du kit, recommandations bus et textes de
diagnostic (V3) peuvent consommer `KitSession` sans toucher au thread audio.

### 16. Diagramme de classes

```text
juce::AudioProcessor <|-- ZeroEffortDrumProcessor ----> kit::KitAligner
        |   1                      | 1                       (DSP, thread audio)
        |                          v
        |              SharedResourcePointer<kit::Registry>
        |                          | 1
        |                          *-- Member[64] (anneau, epoch, writeEnd, Identity)
        |                          *-- GroupState[8] -- KitSession (thread analyse)
        |                          |                       *-- OnsetDetector[n]
        |                          |                       *-- Pair[n] (spectres croises)
        |                          *-- Worker : juce::Thread
        v
drum::ui::DrumEditor --uses--> kit::GroupView / Report (copies)
```

### 17. Ordre d'implementation (suivi)

1. Noyau d'analyse hors ligne + validation sur la prise reelle (Python puis C++).
2. `KitAligner` et parametres d'instance (sans reseau d'instances).
3. `Member` + `Registry` + thread d'analyse ; tests a 4 instances simulees.
4. Modes Auto / Suggerer, persistance.
5. Interface Kit / Phase, rapport, scores.
6. A faire : validation dans Cubase (ASIO Guard, ARA, projets multiples).

### 18. Pieges JUCE

- `getSampleRate()` peut valoir 0 hors hote : le taux de `prepareToPlay` est garde.
- `SharedResourcePointer` est partage par *tous* les projets du processus : d'ou
  les groupes et l'activite.
- Les sous-composants crees avant leur parent ne recoivent pas le LookAndFeel :
  `sendLookAndFeelChange()` en fin de constructeur.
- `getPlayHead()` n'est valable que dans `processBlock`.
- Les timers ne tournent pas sans boucle de messages (tests : `kitTick()` direct).

### 19. Pieges VST3

- Aucune latence reportee : changer la latence a chaud est mal supporte par
  certains hotes ; le choix "retarder les micros proches" l'evite.
- Parametres kit non automatisables pour groupe / mode / position / force ;
  retard et polarite automatisables (lisses, sans clic).
- IDs existants inchanges ; nouveaux IDs `kitGroup`, `kitMic`, `kitMode`,
  `kitStrength`, `kitPolarity`, `kitDelayMs` ; anciens etats -> valeurs neutres.
- Le nom de piste vient de `updateTrackProperties` (Cubase le fournit en VST3).

### 20. Pieges ARA

- En ARA, l'entree audio est ignoree : la capture lit la somme brute des
  regions rendues (`PrototypeRenderer::rawMix`), avant le moteur.
- Cubase peut lier l'edition et la lecture ARA a des instances differentes
  (d'ou les instances "sans lecture"). Les reglages kit sont donc stockes sur les
  evenements ARA, comme l'instrument : la fenetre ecrit sur la selection, l'instance
  qui joue lit ses propres regions. La piste est identifiee par sa region sequence
  ARA, ce qui permet a la fenetre de retrouver la recommandation de la piste jouee.
- L'alignement est le premier etage de `DrumEngine`, donc applique par region ARA
  avant les traitements, exactement comme en insert.
- Le role en ARA vient de la premiere region de la piste. Comme l'instrument, la
  position s'applique aux evenements selectionnes : selectionner les evenements
  de la piste avant de changer Kick Out ou Gauche / Droite.
- Un evenement mono est rendu sur deux canaux identiques : sans position
  Gauche / Droite, la piste est detectee comme mono (pas de faux score stereo).

## Revue Critique De La Proposition Initiale (ARA_update1.md)

Revue faite avant implementation, en supposant des milliers d'utilisateurs.

### Defauts d'architecture

- `TrackInfo` partage un `AudioPreview` et des spectres complets : copie massive
  de donnees, sans dire qui les possede ni quand elles sont valides.
- Aucune notion de **temps commun** : sans horodatage timeline, deux pistes
  traitees dans des blocs differents ne sont pas comparables.
- `UUID trackId` invente un identifiant que l'hote ne fournit pas ; l'identite
  doit etre l'instance (slot) et le nom de piste seulement un libelle.
- "Shared Drum Manager" sans perimetre : plusieurs projets ouverts dans le meme
  Cubase partageraient le meme kit.
- "Move OH L earlier by 2.31 ms" : avancer une piste exige de la latence ou
  une edition hors temps reel ; la proposition ne le traite pas.
- Polarite par `Correlation(A, -B)` a retard nul : confond decalage et polarite
  (sur la prise reelle le Snare donne r0 = -0.35 alors que sa polarite est
  correcte une fois le retard compense).
- `DrumElement` sans Kick Out ni Room L/R ; OH L/R codes comme instruments au
  lieu d'une position de micro.

### Risques de threads

- Un "manager" partage interroge depuis `processBlock` implique un verrou ou un
  `shared_ptr` dont la derniere liberation peut arriver dans le thread audio.
- Calcul de correlation croisee declenche par une instance : sur quel thread ?
  Dans le timer de l'editeur, il bloquerait l'interface ; dans l'audio, il
  casserait le temps reel.

### Risques temps reel

- Allocation de previews / spectres a chaque mise a jour.
- Application "Auto" en ecrivant des parametres depuis l'audio (interdit en
  VST3, et non annulable).
- Changement de latence pour avancer une piste : recalcul PDC, clics.

### Propriete et duree de vie

- Instance detruite pendant une analyse : pointeurs pendants vers son preview.
- Destruction du manager quand la derniere instance part, alors qu'un thread
  d'analyse lit encore.

### Echelle

- Paires "toutes contre toutes" sur 64 pistes = 2016 correlations pleine
  longueur : inutile ; les paires doivent suivre les roles.
- "Never search across the full file" est juste, mais +/-5 ms est trop court
  pour certains Kick ; les fenetres doivent etre par role et bornees par la
  physique (pas d'arrivee negative).

### Specifique JUCE / VST3 / ARA

- JUCE : statiques et `SharedResourcePointer` communs a tous les projets du
  processus ; LookAndFeel et timers dependent de la boucle de messages.
- VST3 : pas d'identifiant de projet ni de piste garanti ; ecriture des
  parametres uniquement depuis le thread message ; la latence variable est mal
  geree par plusieurs hotes.
- ARA : l'audio "entrant" n'existe pas (rendu depuis les sources) ; les
  reglages sont par modification, pas par piste ; le document ARA serait un
  meilleur perimetre de kit mais il n'existe pas en insert.

## Architecture Amelioree (Implementee)

Voir les sections 1 a 20 : capture horodatee par instance sans verrou, slots
persistants reutilises, groupes de kit + activite pour isoler les projets,
thread d'analyse unique proprietaire des sessions, paires dirigees par les
roles, premiere arrivee blanchie + polarite au bon retard, corrections en
parametres d'instance appliquees sans latence en tete de chaine.

## Auto-Critique

### Points faibles

- Le groupe de kit est manuel : un utilisateur qui oublie de le changer entre
  deux projets ouverts simultanement melange les kits (attenue : les pistes
  d'un projet inactif ne jouent pas et sont exclues).
- Correction par retard entier : 0.02 ms de resolution a 48 kHz, suffisant
  pour l'alignement mais pas un alignement de phase fin par bande.
- Room jamais avancee ; certains ingenieurs voudront aligner la room : il
  faudrait un mode latence explicite.
- Les rapports ne sont pas sauvegardes : apres rechargement, seules les
  corrections appliquees restent.
- En ARA les reglages kit suivent les evenements : une piste dont les evenements
  ont des reglages differents prend ceux de sa premiere region.

### Risques

- Plug-in latent avant ZeroEffort, ou piste deplacee a la main apres analyse :
  mesure obsolete, aucune detection automatique.
- Prises tres reverberantes ou micros proches tres repisses : la premiere
  arrivee peut etre ambigue ; la confiance |r| est affichee mais pas encore
  traduite en refus explicite par paire en dessous de 0.15.
- Thread d'analyse de basse priorite sur une machine saturee : progression
  lente (sans danger pour l'audio).

### Echelle

- 64 instances x 1 Mo d'anneaux = 64 Mo dans le processus hote.
- Le cout croit avec (micros proches x canaux overheads) ; 40 micros proches
  x 4 canaux distants = 160 paires, encore raisonnable hors temps reel.
- Un seul thread d'analyse pour tous les groupes : correct tant qu'un seul
  groupe analyse a la fois (cas courant).

### Maintenance

- `KitAnalysis.h` concentre detection, accumulation, recherche et scores : a
  scinder si les evolutions V3 (masquage, equilibre) s'y ajoutent.
- Les ponderations des scores sont empiriques ; elles devront etre recalees
  sur plusieurs prises reelles (une seule prise de reference aujourd'hui).
- Les tests UI verifient la structure par `componentID` (et non des
  coordonnees) : garder ces IDs stables.

## Validation

- `tests/KitTests.cpp` : retard / polarite exacts, transparence a 0 ms,
  rejet des donnees ecrasees ou discontinues, kit synthetique (Kick inverse
  +5.24 ms, Snare +3.21 ms, Snare Bottom inverse +2.90 ms, phase 56 -> 97),
  prise reelle (tableau ci-dessus), 4 instances : decouverte, isolation de
  groupe, Auto a 50 % (+2.62 ms), Suggerer attend Appliquer, apercu (remise a
  zero, application des seules pistes cochees), reglage fin d'un echantillon,
  persistance.
- `tests/EditorTests.cpp` : ordre des onglets, noms par role (Kick / Snare /
  Hi-hat / Overheads), page Kit, absence de chevauchement, captures PNG
  (`KitPhase.png`, `KitReport.png`, pages Kick / Bus / Impact / Precision).
- Trois suites CTest (cache, Drum, decouverte ARA) passent en Release.
- Non verifie : ecoute et comportement dans Cubase (ASIO Guard, ARA multi-pistes,
  deux projets ouverts). A faire sur une copie de projet.

## Prochaines Etapes

- V2 restant : refus explicite des paires peu fiables, affichage de la
  correlation par paire, rapport sauvegarde avec le projet.
- Mode "latence" optionnel pour avancer room / overheads.
- V3 : masquage frequentiel, conflits Kick / Toms, equilibre du kit,
  recommandations bus a partir des memes sessions.
