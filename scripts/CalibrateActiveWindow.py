"""Calibration of the active window threshold (decision 01-Q1, document 05).

Reads the snapshots written by `ZeroEffortDrumTests.exe --export-wav` (or by the developer export) and, for each take,
recomputes the active crest from the stored histograms for a range of "P95 minus X dB" thresholds. The plateau
criterion is a spread of the crest below 0.5 dB over X +- 10 dB. Only the Python standard library is used.

    python scripts/CalibrateActiveWindow.py <snapshot folder> [--above 12] [--skip-prefix mix]
"""
import argparse
import base64
import glob
import json
import math
import os
import statistics
import struct


def percentile_of(histogram, windows, fraction):
    target = max(1, int(windows * fraction))
    count = 0
    for index, value in enumerate(histogram):
        count += value
        if count >= target:
            return index - 100
    return 0


def active_crest(snapshot, below, above):
    """Same arithmetic as DrumAnalysis::activeCrestDb (peak minus the RMS of the active windows)."""
    analysis = snapshot["analysis"]
    histogram = analysis["histogram"]
    energies = struct.unpack("<101d", base64.b64decode(analysis["windowEnergyByPeak"]))
    p95 = percentile_of(histogram, analysis["windows"], 0.95)
    floor = percentile_of(histogram, analysis["windows"], 0.10)
    threshold = min(p95, max(p95 - below, floor + above))
    count = sum(histogram[i] for i in range(1, 101) if i - 100 >= threshold)
    if count < 1:
        return None
    energy = sum(energies[i] for i in range(1, 101) if i - 100 >= threshold)
    length = max(1, int(analysis["rate"] * 0.01))
    rms = max(20 * math.log10(math.sqrt(energy / (count * length))) if energy > 0 else -100, -100)
    peak = max(20 * math.log10(analysis["peak"]), -100) if analysis["peak"] > 0 else -100
    return peak - rms


def spread(snapshot, centre, above, half=10):
    values = [active_crest(snapshot, below, above) for below in range(centre - half, centre + half + 1)]
    values = [value for value in values if value is not None]
    return max(values) - min(values) if values else float("nan")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folder")
    parser.add_argument("--above", type=float, default=12, help="noise floor offset in dB (default 12)")
    parser.add_argument("--skip-prefix", default="mix", help="ignore sub-folders starting with this (default: mix)")
    arguments = parser.parse_args()

    takes = []
    for path in sorted(glob.glob(os.path.join(arguments.folder, "**", "*.json"), recursive=True)):
        group = os.path.basename(os.path.dirname(path))
        if arguments.skip_prefix and group.startswith(arguments.skip_prefix):
            continue
        with open(path, encoding="utf-8") as handle:
            takes.append((group, os.path.basename(path), json.load(handle)))
    print("%d takes" % len(takes))
    print("X   | median spread | max spread | takes under 0.5 dB | under 1.0 dB")
    for centre in range(30, 71, 5):
        spreads = [spread(snapshot, centre, arguments.above) for _, _, snapshot in takes]
        print("%3d | %6.2f dB     | %6.1f dB  | %2d / %d             | %2d / %d" % (
            centre, statistics.median(spreads), max(spreads), sum(s < 0.5 for s in spreads), len(spreads),
            sum(s < 1.0 for s in spreads), len(spreads)))


if __name__ == "__main__":
    main()
