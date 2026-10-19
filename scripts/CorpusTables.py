"""Tables de lecture des sorties de scripts/RunCorpus.ps1 (aucune ecriture, aucun calcul audio).

    python scripts/CorpusTables.py pairs  <dossier out>     une ligne par paire brut / mixe (verdict, decalage, cohérence, dynamique)
    python scripts/CorpusTables.py versus <dossier out>     plug-in contre ingenieur, par piste (necessite le champ "role" du manifeste)
    python scripts/CorpusTables.py shapes <dossier out> [nom ...]  courbes d'egalisation equivalentes, ingenieur et plug-in
    python scripts/CorpusTables.py buses  <dossier out>     courbe appliquee a chaque micro d'un bus (regression multi-entrees)

Les courbes sont ecrites en dB, relativement au gain moyen 100 Hz - 5 kHz ; un "?" marque une bande peu fiable.
"""
import glob
import json
import os
import sys

LABELS = ["20", "25", "31.5", "40", "50", "63", "80", "100", "125", "160", "200", "250", "315", "400", "500", "630", "800",
          "1000", "1250", "1600", "2000", "2500", "3150", "4000", "5000", "6300", "8000", "10000", "12500", "16000", "20000"]
SHOWN = ["63", "100", "160", "250", "400", "630", "1000", "1600", "2500", "4000", "6300", "10000", "16000"]


def load(path):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def number(value, width=6, decimals=1):
    return ("{:%d.%df}" % (width, decimals)).format(value) if isinstance(value, (int, float)) else " " * (width - 3) + "n/a"


def pairs(root):
    for path in sorted(glob.glob(os.path.join(root, "pairs", "*.json"))):
        j = load(path)
        print("{:<30} acc={!s:<5} decalage {} ms inverse={!s:<5} corr {} coherence {} jeu {}s frappes {}->{} crest {}/{} plancher {} derive {}".format(
            os.path.basename(path)[:-5][:30], j.get("accepted"), number(j.get("lag_ms"), 6, 2), j.get("inverted"),
            number(j.get("align_correlation"), 5, 2), number(j.get("coherence_mediane"), 5, 2), number(j.get("playing_seconds"), 5, 1),
            j.get("hits_raw"), j.get("hits_mix"), number(j.get("delta_crest_db")), number(j.get("delta_crest_actif_db")),
            number(j.get("delta_plancher_db")), number(j.get("gain_drift_db"))))
        if not j.get("accepted"):
            print("      raison :", j.get("reason"))


def versus(root):
    print("rms = ecart plug-in / ingenieur ; ing = ecart de l'ingenieur a une courbe plate : rms < ing signifie que le plug-in")
    print("se rapproche de l'ingenieur plus que ne le ferait un reglage neutre. Bandes fiables 100 Hz - 8 kHz seulement.\n")
    print("{:<24} {:>4} {:>6} {:>6} {:>6} {:>7} | crest actif {:>6} {:>6} | plancher {:>6} {:>6} | frappes".format(
        "piste", "paire", "bandes", "rms", "ing", "meme", "ing", "plug", "ing", "plug"))
    for path in sorted(glob.glob(os.path.join(root, "versus", "*.json"))):
        j = load(path)
        m, p = j["mixer"], j["plugin"]
        print("{:<24} {:>4} {:>6} {} {} {:>3}/{:<3} | {} {} | {} {} | {}>{} {}>{}".format(
            os.path.basename(path)[:-5][:24], "oui" if m.get("accepted") else "non", j["bands_compared"], number(j["rms_db"]),
            number(j["mixer_rms_db"]), j["agree"], j["significant"], number(m.get("delta_crest_actif_db"), 17),
            number(p.get("delta_crest_actif_db")), number(m.get("delta_plancher_db"), 14), number(p.get("delta_plancher_db")),
            m.get("hits_raw"), m.get("hits_mix"), p.get("hits_raw"), p.get("hits_mix")))


def curve_row(tag, values, reliable):
    cells = []
    for label in SHOWN:
        i = LABELS.index(label)
        v = values[i]
        cells.append("{:>6}".format("" if v is None else "{:+.1f}{}".format(v, "" if reliable[i] else "?")))
    print("{:<22}|".format(tag) + " ".join(cells))


def header():
    print("{:<22}|".format("") + " ".join("{:>6}".format(l) for l in SHOWN))


def shapes(root, names):
    header()
    for name in names or [os.path.basename(p)[:-5] for p in sorted(glob.glob(os.path.join(root, "versus", "*.json")))]:
        j = load(os.path.join(root, "versus", name + ".json"))
        for who, tag in (("mixer", "INGE"), ("plugin", "plug")):
            curve_row(name[:16] + " " + tag, j[who]["forme_db_1_3_octave"], j[who]["bande_fiable"])
        print()


def buses(root):
    header()
    for path in sorted(glob.glob(os.path.join(root, "buses", "*.json"))):
        j = load(path)
        print("== {} : accepte={} coherence multiple {:.2f}".format(os.path.basename(path)[:-5], j["accepted"], j["coherence_multiple_mediane"]))
        multiple = j["coherence_multiple_1_3_octave"]
        reliable = [(c is not None and c >= 0.8) for c in multiple]
        for entry in j["entrees"]:
            gain = entry["gain_db_1_3_octave"]
            usable = [i for i in range(31) if gain[i] is not None and 100 <= float(LABELS[i]) <= 5000 and reliable[i]] or \
                     [i for i in range(31) if gain[i] is not None and 100 <= float(LABELS[i]) <= 5000]
            mean = sum(gain[i] for i in usable) / len(usable)
            curve_row("{} ({:+.1f} dB)".format(entry.get("nom", "?")[:14], mean), [None if v is None else v - mean for v in gain], reliable)


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("pairs", "versus", "shapes", "buses"):
        print(__doc__)
        return 1
    command, root = sys.argv[1], sys.argv[2]
    if command == "shapes":
        shapes(root, sys.argv[3:])
    else:
        {"pairs": pairs, "versus": versus, "buses": buses}[command](root)
    return 0


if __name__ == "__main__":
    sys.exit(main())
