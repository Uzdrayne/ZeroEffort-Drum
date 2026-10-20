# Décisions proposées pour les questions ouvertes

<!-- consolidated-archive -->
> **Archive historique.** Référence consolidée : [SPECIFICATIONS](../../SPECIFICATIONS.md).
> Avancement vérifié : [PROJET](../../PROJET.md). Les états et chemins ci-dessous
> sont historiques ; une proposition n'est pas une décision automatiquement validée.
<!-- /consolidated-archive -->

**Document 5** | Version 0.1 | Complète les documents 01 à 04

Chaque question ouverte reçoit une **décision proposée**, sa raison et la façon de la vérifier. Ce sont des propositions : tant que vous ne les validez pas, elles restent à l'état « proposé ». Une fois validées, elles **remplacent** les questions ouvertes des documents d'origine pour le codage.

Légende du statut : **P** = proposé, à valider ; **H** = hypothèse chiffrée à calibrer sur des données.

---

## Document 01 : mesures

| # | Question | Décision proposée | Raison | Vérification | Statut |
|---|---|---|---|---|---|
| 01-Q1 | Seuil de la fenêtre active | Fenêtre active = pic > `max(P95 − 55 dB, plancher_bruit + 12 dB)`. Le 40 dB de départ a été calibré à **55 dB le 17/10/2026** en traçant `crest_actif` en fonction du seuil sur 28 prises (3 kits) : plateau où le crest varie de moins de 0,5 dB pour ± 10 dB de seuil. Le 12 dB reste une hypothèse | Un seuil purement relatif échoue sur les prises très bruitées ; le plancher de bruit le stabilise | Test A09 (document 02) ; courbe `crest_actif` / seuil (`scripts/CalibrateActiveWindow.py`) | P, H (40 dB : calibré) |
| 01-Q2 | Garder les WAV des bêta-testeurs | **Non par défaut** : on ne reçoit que les mesures (JSON). Un envoi d'extrait audio (30 s maximum) est possible uniquement sur demande explicite, avec consentement séparé et suppression sur demande | RGPD, confiance, poids ; les mesures suffisent au rejeu | Texte de consentement relu ; export testé sans audio | P |
| 01-Q3 | Mesures par frappe ou globales | **Globales dans toutes les lignes**, plus des statistiques par frappe agrégées (moyenne, écart-type, P10, P90). Les valeurs par frappe elles-mêmes ne sont écrites que dans un fichier séparé `hits.csv`, en mode corpus de développement, limité à 200 frappes | Garde le JSON léger tout en conservant la dispersion utile | Taille du JSON < 300 Ko par analyse | P |
| 01-Q4 | Comparer 44,1 / 48 / 96 kHz | Déjà quasi équivalent : la taille de FFT double au-dessus de 48 kHz, donc la résolution reste d'environ 11 Hz. Décision : bandes définies en Hz, bandes au-dessus de `0,45 × fréquence d'échantillonnage` ignorées, `rate` toujours stocké | Évite une normalisation compliquée | Test A11 (document 02) | P |
| 01-Q5 | Longueur des trames longues (OH, room) | Démarrer à **400 ms** (avance 100 ms). Comparer 200 / 400 / 800 ms sur les overheads du corpus et retenir la plus courte dont les médianes par bande diffèrent de moins de 1 dB de celles à 800 ms | Compromis entre stabilité et temps d'analyse | Script de comparaison de longueurs | P, H |
| 01-Q6 | Room seule ou avec overheads | La room est analysée **seule** pour ses mesures propres (spectre, stéréo, décroissance). `niveau_vs_oh` et `compat_room_oh` sont `null` si les overheads manquent, et le preset prend alors des valeurs prudentes par défaut | La room ne doit pas dépendre d'une autre piste pour fonctionner | Test avec un kit sans overheads | P |

## Document 02 : programme de test

| # | Question | Décision proposée | Raison | Vérification | Statut |
|---|---|---|---|---|---|
| 02-Q1 | Défauts artificiels : réels ou synthétiques | **Les deux**, dans cet ordre : chaque test de défaut existe d'abord en version synthétique (exactitude), puis sur 3 à 5 kits réels (réalisme) | Le synthétique prouve la logique, le réel prouve l'utilité | Les tests D01 à D04 existent dans les deux versions | P |
| 02-Q2 | Tester un « réglage attendu » sans cible externe | Trois couches : propriétés E, cas dorés validés à l'oreille, et **tests de sens de correction** : on perturbe une prise d'une manière connue (shelf aigu +6 dB) et le preset doit corriger **dans le bon sens**, avec une ampleur entre 30 % et 100 % de la perturbation | On vérifie la direction et l'ordre de grandeur sans connaître le réglage parfait | Famille de tests ajoutée au document 02 | P |
| 02-Q3 | Budget CPU | Objectif de départ : analyse hors du thread audio ; coût de `processBlock` sans analyse < 1 % d'un cœur par instance ; 8 instances en analyse simultanée < 10 % d'un cœur ; aucune pointe de FFT qui dépasse le budget d'un bloc. Une fois la première mesure faite, fixer le seuil à **2 × la médiane mesurée** | Les chiffres réels remplacent mes hypothèses après la première mesure | Tests H01 à H04 | P, H |
| 02-Q4 | Tester dans un hôte réel | CI : validateur de plug-ins (par exemple pluginval) à chaque version. Manuel avant chaque bêta : checklist de 10 points sur Cubase, Reaper et Studio One (chargement, ordre des pistes, ARA, sauvegarde du projet, bypass, Revenir, fermeture). L'ARA ne s'automatise pas | L'ARA dépend de l'hôte, donc reste manuel | Checklist signée par version | P |
| 02-Q5 | Combien de kits | **10 kits par jeu** minimum, 15 viser ; au-delà de 30 kits au total, n'ajouter que des cas difficiles | Au-dessous de 10, les conclusions sur les seuils sont fragiles | Rapport de sensibilité aux seuils (5.2) | P, H |

## Document 03 : comparaison brut / mixé

| # | Question | Décision proposée | Raison | Vérification | Statut |
|---|---|---|---|---|---|
| 03-Q1 | Seuil de cohérence et nombre de paires | Départ : cohérence ≥ 0,8, 8 paires, 3 mixeurs. Calibration : avec les tests M01 à M07, tracer l'erreur d'estimation de l'EQ en fonction de la cohérence et choisir le seuil à partir duquel l'erreur reste sous 1 dB | Le seuil vient de l'erreur mesurée de la méthode | Tests M01 à M07 | P, H |
| 03-Q2 | Stem avant ou après le bus | **Avant le bus exigé.** Une paire prise après le bus est acceptée si `bus_inclus = true` dans `meta.json`, mais **exclue** des cibles de dynamique (utilisable pour l'EQ seulement) | Le bus compresse : ça fausserait les cibles de compression | Colonne `bus_inclus` dans `paires.csv` | P |
| 03-Q3 | Samples de remplacement | Exclus des cibles standard. Marqués `hybride = true` et conservés à part, pour un futur profil « kick hybride » | Leur relation avec le brut n'est pas linéaire | Cohérence basse détectée à l'alignement | P |
| 03-Q4 | Fiabilité des temps d'attaque / release | Utilisés **à titre d'information seulement** dans la base. Un temps n'entre dans un preset que si les tests M02 le retrouvent à moins d'un facteur 2 dans 80 % des cas ; sinon on garde les valeurs de style | Évite de caler un réglage sur une estimation fragile | Tests M02 | P |
| 03-Q5 | Stockage des paires | Les WAV restent **hors du dépôt Git** (disque ou archive chiffrée) ; seules les mesures et `meta.json` (licence incluse) sont versionnés ; aucune redistribution | Licences et poids du dépôt | Revue de la licence de chaque source | P |

## Document 04 : presets

| # | Question | Décision proposée | Raison | Vérification | Statut |
|---|---|---|---|---|---|
| 04-Q1 | Valeur de `k` et zone morte | Départ : `k = 0,5`, zone morte 1 dB (5 % en fréquence). Calibration par fonction sur le jeu de réglage : choisir `k` qui réduit l'écart médian à la cible sans dépasser ; départager par écoute A/B aveugle | Une même valeur pour tout est simple à lancer et facile à affiner | Rapport de comparaison, A/B | P, H |
| 04-Q2 | Afficher la dispersion dans le Bilan | **Oui en mode détaillé** (une ligne optionnelle : « Poids +4,8 dB, plage des mixeurs +1,5 à +4,6 »), masquée en mode simple | Honnêteté sur l'incertitude sans surcharger les débutants | Revue de l'interface | P |
| 04-Q3 | Variantes « Doux / Précis » | **Pas en version 1.** Le curseur d'intensité suffit. Plus tard : deux jeux de `k` | Garde l'interface simple pour le public amateur | — | P |
| 04-Q4 | Plafond global de traitement | Définir un **budget de traitement** : somme pondérée de `|gain EQ|`, réduction de gain visée, `range` de l'expander / 2 et drive. Plafond par style ; au-delà, mise à l'échelle proportionnelle en protégeant d'abord les corrections de phase et de niveau | Évite le cumul gate + compresseur + saturation | Nouvelle propriété E dans le document 02 | P, H |
| 04-Q5 | Prises hybrides (kick + samples) | Case « Kick avec samples » dans l'interface, plus un **avertissement automatique** si les frappes sont presque identiques (écart-type des pics très faible, spectres d'onset quasi identiques). Dans ce cas : expander et nettoyage plus doux, pas de renfort accordé | Un sample parfaitement régulier se prête mal à un gate agressif | Test avec un kick synthétique répété | P |

## Récapitulatif des hypothèses chiffrées à calibrer

`55 dB` (fenêtre active, calibré le 17/10/2026 ; l'hypothèse de départ était 40 dB) | `400 ms` (trame longue) | `0,8` (cohérence) | `8 paires / 3 mixeurs` | `k = 0,5` | `1 dB` (zone morte) | `10 kits par jeu` | `2 × médiane CPU`.

Hormis le seuil de la fenêtre active, aucune de ces valeurs ne vient d'une mesure : ce sont les points de départ de la première calibration.
