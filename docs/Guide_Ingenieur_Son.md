# Guide rapide : préparer une batterie pour ZeroEffort Drum

Merci de donner un coup de main. L'idée : le plug-in doit apprendre **ce qu'un ingé son fait entre la piste brute et la piste mixée**. Pour ça il lui faut les mêmes pistes de batterie, **avant** et **après** ton traitement. Rien de plus.

Compte environ **15 à 30 minutes** par morceau, une fois la session ouverte.

---

## Ce qu'il me faut

Pour **un morceau** (la batterie seule suffit, 30 s à 2 min de jeu suffisent, au moins 20 s) :

| Dossier | Contenu |
|---|---|
| `RAW` | chaque piste de batterie **brute**, telle qu'enregistrée |
| `MIX` | les **mêmes pistes**, avec ton traitement |
| `INFO.txt` | 4 lignes (voir plus bas) |

### RAW : les pistes brutes

- Aucun insert : ni EQ, ni compresseur, ni gate, ni saturation, ni sample.
- Le niveau (fader, trim) n'a pas d'importance.
- Pas de normalisation.

### MIX : les mêmes pistes, traitées

- **Avec** tous tes inserts de piste (EQ, compresseur, gate, saturation, etc.).
- **Sans** le bus de batterie, le master, les reverbs et les delays en envoi.
- Si tu as inversé la polarité ou décalé une piste dans le temps, c'est bon, mais **note-le** dans `INFO.txt`.

### Les deux dossiers

- Format **WAV**, même fréquence d'échantillonnage partout (par exemple 48 kHz), 24 bits ou 32 bits flottants.
- **Même début** pour toutes les pistes (export depuis le même point de la session). Ça évite les décalages.
- **Même nom de fichier** dans `RAW` et `MIX` : `01_KickIn.wav`, `02_KickOut.wav`, `03_Snare.wav`, etc.
- Une piste mono reste mono, une piste stéréo reste stéréo.

---

## `INFO.txt` : 4 lignes

```
Ingé : (ton prénom ou un pseudo)
Style : natural, rock ou metal
Notes : (ex. sample sur la caisse claire, compression parallèle, polarité inversée sur le Snare Bottom)
Le morceau : (titre ou genre, facultatif)
```

---

## Les pistes les plus utiles, par ordre de priorité

1. **Kick** (In et Out si tu as les deux), **Snare** (Top, et Bottom si tu l'as).
2. **Overheads**.
3. **Toms**.
4. Hi-hat, Room.

Si c'est trop long, **Kick, Snare et Overheads suffisent déjà.**

---

## Bonus : le bus de batterie (très utile, 2 fichiers de plus)

Si ta batterie passe par un **bus** (compression, EQ, saturation de bus), donne-moi :

| Fichier | Contenu |
|---|---|
| `BUS_IN.wav` | le bus de batterie **avec tous ses inserts désactivés** (c'est la somme de tes pistes mixées) |
| `BUS_OUT.wav` | le même bus **avec ses inserts actifs** (ce que tu envoies au master) |

Même début, même longueur, même fréquence d'échantillonnage. Si tu utilises des reverbs ou des parallèles sur ce bus, désactive-les ou note-le dans `INFO.txt`. On voit ainsi **uniquement** ce que fait ton bus.

---

## Un autre bonus qui vaut de l'or

Si tu as le **même morceau mixé dans un autre style** (par exemple une version rock et une version metal), garde le même `RAW` et donne deux dossiers `MIX_rock` et `MIX_metal`. On voit alors ce qui change quand le style change.

---

## Confidentialité

Les fichiers restent **privés**, sur le disque de Robin, hors du dépôt du plug-in. Ils servent uniquement à **mesurer** (courbes, niveaux, dynamique). Ils ne sont ni publiés ni partagés.
