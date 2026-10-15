# DRUM / KICK 0.5.0

Historique de la serie 0.5. Livraison actuelle :
[EQ et Interface 0.6.0](KickInterface06.md).

## Livraison Et Installation

Cette passe ajoute detection ciblee, nettoyage selectif et presence au moteur
commun insert/ARA. Cubase Gain reste separe et inchange. Le nom du bundle,
les codes du plugin, les identifiants ARA et les 29 anciens parametres restent
identiques ; 14 parametres sont ajoutes a la fin.

Fermer Cubase. Copier manuellement le dossier ENTIER
`build/DrumARAPrototype_artefacts/Release/VST3/Drum ARA Prototype.vst3`
dans `C:\Program Files\Common Files\ARA`, en remplacant l'ancienne copie.
Mettre aussi a jour la copie dans Common Files/VST3 si elle est conservee.
Aucune copie vers un dossier protege ni aucun lancement audio automatique.
Verifier la version 0.5.0 apres reouverture.

Travailler sur une COPIE du projet. Les archives ARA anterieures sont lues,
avec les nouveaux modules desactives. Le nouveau format stocke le nombre
de controles et leurs valeurs ; les anciennes versions ne peuvent pas relire
les nouvelles archives ARA. Les anciens etats insert remettent egalement les
nouveaux controles a leur valeur par defaut, meme dans une instance deja editee.

## Traitements

- Kick Focus : detecteur passe-bande reglable 20-100 Hz / 120-500 Hz,
  enveloppes rapide/lente et declenchement relatif au fond. Avec Focus active,
  l'expander utilise cette enveloppe au lieu de la large bande. Son seuil,
  ratio, range, attack et release restent sur la page principale.
- Hold 0-100 ms et hysteresis 0-12 dB limitent les fermetures entre oscillations.
  Le detecteur est lie stereo par maximum, pas par moyenne L/R.
- Bleed Clean : attenuation dynamique de la composante au-dessus d'environ
  1.6 kHz lorsque le detecteur se ferme. Range reglable 0-30 dB. Le grave reste
  transmis ; ce module peut agir sans activer l'expander large bande.
- Body / Presence : renfort sinusoidal declenche, accordable 30-120 Hz,
  dose 0-100 %, extinction jusqu'a -60 dB en 60-400 ms. La velocite suit
  l'enveloppe detectee ; une entree progressive de 2 ms adoucit son depart.
- Punch : accent transitoire 0-6 dB, enveloppe courte de 12 ms.
- Drive : saturation douce tanh, parallele a faible dose, sur le grave filtre
  vers 250 Hz, pas une distorsion de tout le signal.
- Body polarity inverse uniquement le renfort, pas la piste originale.

Ordre : entree, HP/LP, detection, expander, nettoyage/presence, quatre bells EQ,
compresseur/mix, sortie. Les trois nouvelles activations sont OFF par defaut.
Les parametres sont lisses ; les filtres et l'oscillateur sont prepares avant
le rendu. Pas de lecture disque, allocation ni verrou ajoute dans ce DSP audio.
Le bypass global contourne aussi les nouveaux traitements.

## Auto Et Bilan

Choisir Naturel/Rock/Metal et l'intensite, puis Analyser. En ARA, une seule
region effective est requise ; en insert, la capture reste de 8 secondes.
L'analyse est suivie de deux rendus incrementaux du vrai moteur pour calibrer
la compression puis mesurer le resultat. Rien ne change avant Appliquer.

A 100 %, les propositions utilisent les doses suivantes :

| Profil | Nettoyage max | Renfort | Extinction -60 dB | Punch | Drive |
| --- | ---: | ---: | ---: | ---: | ---: |
| Naturel | 6 dB | 15 % | 220 ms | 0.5 dB | 1 dB |
| Rock | 12 dB | 35 % | 180 ms | 2 dB | 4 dB |
| Metal | 18 dB | 45 % | 140 ms | 3 dB | 6 dB |

Ces doses sont reduites par l'intensite ; a zero les nouveaux modules sont OFF.
Le nettoyage Auto reste OFF si le contraste d'analyse ne justifie pas EXP.
Le renfort est accorde a 75 % du pic de corps estime, borne a 40-80 Hz.
Ce choix est une intention de mixage, pas l'estimation certifiee d'une fondamentale.
Naturel ajoute maintenant une presence legere, il n'est pas un bypass.

Focus / Presence ouvre les reglages manuels. Le bouton Bilan ouvre un texte
defilant contenant proposition, EXP/COMP et RMS mesures, nombre de vrais
declenchements du renfort, nettoyage maximal, crete du renfort avant EQ/COMP,
et les 43 valeurs finales. Revenir restaure les reglages precedant Appliquer
tant que la cible et les parametres appliques n'ont pas ete modifies.

Le nombre d'attaques candidates de l'analyse est distinct des declenchements
du nouveau detecteur. Ni l'un ni l'autre n'est un comptage annote de kicks.
La part de blocs avec EXP >1 dB ne mesure pas un taux de separation de sources.
La courbe de la page principale represente uniquement HP/LP et EQ, pas les
traitements dynamiques ni le renfort synthetique.

## Mesures Sur Les Prises Utilisateur

Rock 100 %, fichiers lus sans modification, rendu a 48 kHz :

| Mesure | trerst.wav, stereo 16.5 s | Kick_12.wav, mono 165.129 s |
| --- | ---: | ---: |
| RMS brut et traite | -34.3675 dBFS | -30.517 dBFS |
| Crete brute | -9.1403 dBFS | -5.1334 dBFS |
| Crete traitee | -13.0591 dBFS | -8.0134 dBFS |
| Attaques candidates analyse | 85 | 741 |
| Declenchements Focus / renfort | 35 | 298 |
| EXP maximal | 7.1 dB | 8.0 dB |
| EXP >1 dB, blocs audibles | 49.4 % | 20.9 % |
| Nettoyage aigu maximal commande | 10.6 dB | 12.0 dB |
| COMP actif P75 / maximum | 3.5 / 8.0 dB | 4.0 / 8.0 dB |
| Renfort crete avant EQ/COMP | -24.0 dBFS | -19.0 dBFS |

Les deux propositions accordent ici le renfort a 80 Hz. Ces maxima de commande
ne sont pas une mesure isolee de la repisse supprimee. La compensation RMS
facilite la comparaison sans garantir une sonie perceptive identique.
Pas d'ecoute realisee automatiquement, ni validation musicale revendiquee.

## Verification Et Limites

Build Release windows-vs2019, quatre suites CTest reussies. Tests a
44.1/48/96 kHz : pas de declenchement sur un sinus aigu seul, renfort accorde
a 55 Hz, mono/stereo proportionnel, silence et reset, rejet du 8 kHz en
nettoyage avec preservation du 80 Hz, bypass, calibration des trois profils
avec et sans presence, preservation des attaques faibles synthetiques,
persistence et migration des etats insert/archives. Les deux pages JUCE sont
capturees et inspectees ; le vrai bundle 0.5.0 passe scan VST3, initialisation
ARA, bus mono/stereo et regressions de sortie Cubase.

Le detecteur n'est pas une separation de sources. Une caisse claire riche en
grave peut le declencher ; des cymbales simultanees au kick restent presentes.
Le seuil Auto reste estime depuis l'enveloppe large bande initiale : les coups
faibles reels peuvent necessiter un seuil manuel plus bas. L'anti-redeclenchement
est fixe a 90 ms ; les frappes plus rapprochees peuvent ne pas obtenir leur
propre renfort. Aucun lookahead ni remplacement par echantillon.

Le corps ajoute ne transpose PAS le kick original. Sa polarite est amorcee
depuis le signal local, sans alignement garanti avec les autres microphones.
Comparer Body polarity en contexte ; reduire Body amount/decay en cas de
masquage ou de clic sur des doubles coups. La saturation n'est pas sur-echantillonnee.
Aucun limiteur, true-peak ni protection contre les boosts manuels excessifs.
Le plafond Auto est une mesure du rendu analyse, pas une garantie pour une
autre section ou plusieurs regions ARA sommees.

Le cache ARA reste borne a 128 Mio/128 regions, sans streaming ni reconstruction
automatique apres edition. Les limites de selection, sample-rate et stretch
anterieures restent valables. La validation complete de cette nouvelle chaine
dans Cubase doit encore etre effectuee par l'utilisateur : ecoute avec les autres
micros, attaques faibles, doubles coups, boucles, sauvegarde/reouverture et export.

Commencer avec Rock 50-70 %, Analyser, lire Bilan, Appliquer, comparer et Revenir.
Tester le renfort seul, puis le nettoyage, avant de monter leurs doses.