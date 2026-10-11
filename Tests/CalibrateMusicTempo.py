"""Bounded development experiments combining lag and Fourier pulse evidence.

The fixed grid compares measured periodicities, including endpoints 40/240;
there is no range prior or rule multiplying every estimated tempo by two.
This is developer tooling, not an additional runtime dependency.
"""
import argparse
import json
from pathlib import Path
import time

import numpy as np

from EvaluateGiantSteps import fingerprint, save_json
from MergeMusicCorpus import key, metrics, read_json
from ReadMusicFeatures import read_features


FPS = 22050 / 256
TEMPOS = np.arange(400, 2401, dtype=np.float64) / 10
LAGS = FPS * 60 / TEMPOS
VARIANTS = [{"power": power, "fourier": fourier, "selection": selection, "minimum": minimum, "localGate": local}
            for power in (1.0, 0.5) for fourier in (0.0, 0.5, 1.0)
            for selection in ("strongest", "fastest-supported") for minimum in (0.3, 0.5) for local in (False, True)]


def detrend(values):
    radius = int(FPS * 0.25)
    positions = np.arange(len(values))
    first, last = np.maximum(0, positions - radius), np.minimum(len(values), positions + radius + 1)
    prefix = np.concatenate(([0.0], np.cumsum(values)))
    return np.maximum(0, values - (prefix[last] - prefix[first]) / (last - first))


def periodicity(values):
    size = 1 << (2 * len(values) - 1).bit_length()
    centred = values - values.mean()
    spectrum = np.fft.rfft(centred, n=size)
    power = abs(spectrum) ** 2
    # Zero-padding the frequency domain interpolates the correlation at quarter
    # hops. The old Nyquist coefficient becomes an interior pair, hence /2.
    power[-1] *= 0.5
    interpolated = np.fft.irfft(power, n=size * 4)[:int(np.ceil(LAGS.max())) * 4 + 5] * 4
    grid = np.arange(len(interpolated)) / 4
    energy = np.concatenate(([0.0], np.cumsum(centred ** 2)))
    coordinates = np.arange(len(energy))
    left = np.interp(len(values) - grid, coordinates, energy)
    right = energy[-1] - np.interp(grid, coordinates, energy)
    denominator = np.sqrt(np.maximum(0, left * right))
    correlation = np.divide(interpolated, denominator, out=np.zeros_like(interpolated), where=denominator > 1.0e-12)
    autocorrelation = np.clip(np.interp(LAGS, grid, correlation), 0, 1)
    window = np.hanning(len(values))
    weights = values * window
    fourier_size = 1 << (4 * len(values) - 1).bit_length()
    coefficients = abs(np.fft.rfft(weights, n=fourier_size))
    frequencies = np.arange(len(coefficients)) * FPS / fourier_size
    fourier = np.clip(2 * np.interp(TEMPOS / 60, frequencies, coefficients) / max(weights.sum(), 1.0e-12), 0, 1)
    return autocorrelation, fourier


def regular_interval(values):
    if values.max() <= 1.0e-6:
        return 0.0
    indices = np.flatnonzero((values[1:-1] >= values.max() * 0.4)
                             & (values[1:-1] >= values[:-2]) & (values[1:-1] > values[2:])) + 1
    positions = []
    for index in indices:
        a, b, c = values[index - 1:index + 2]
        denominator = a - 2 * b + c
        position = index + (np.clip(0.5 * (a - c) / denominator, -0.5, 0.5) if abs(denominator) > 1.0e-9 else 0)
        if not positions or position - positions[-1] >= np.floor(FPS * 60 / 240) * 0.7:
            positions.append(position)
    if len(positions) < 5:
        return 0.0
    intervals = np.diff(positions)
    period = np.median(intervals)
    deviations = abs(intervals - period) / period
    tempo = FPS * 60 / period
    return float(tempo) if deviations.max() < 0.2 and np.median(deviations) < 0.06 and 39 <= tempo <= 241 else 0.0


def feature_vectors(trace):
    tempo = trace["tempo"]
    curves = np.zeros((2, 3, 2, len(TEMPOS)))
    regular = np.zeros((2, 3))
    eligible = (len(tempo) >= 5 * FPS and tempo[:, 0].sum() / (len(tempo) * 2048 * trace["channels"]) >= 1.0e-9
                and tempo[:, 1].sum() > 0 and tempo[:, 2].sum() / tempo[:, 1].sum() >= 0.002)
    if eligible:
        for index, power in enumerate((1.0, 0.5)):
            onset = detrend(tempo[:, 2] ** power)
            for segment, values in enumerate((onset, onset[:len(onset) // 2], onset[len(onset) // 2:])):
                curves[index, segment] = periodicity(values)
                regular[index, segment] = regular_interval(values)
    return curves, regular, eligible


def prediction(curves, regular, eligible, variant):
    if not eligible:
        return 0.0, False
    index = 0 if variant["power"] == 1.0 else 1
    correlation, fourier = curves[index, 0]
    scores = correlation * fourier ** variant["fourier"]
    # Candidate maxima must have independent support at their actual period.
    maxima = np.flatnonzero(np.r_[True, scores[1:] >= scores[:-1]] & np.r_[scores[:-1] >= scores[1:], True])
    maxima = maxima[(correlation[maxima] >= variant["minimum"]) & (scores[maxima] >= 0.02)]
    if len(maxima) == 0:
        return 0.0, False
    best = maxima[np.argmax(scores[maxima])]
    if variant["selection"] == "fastest-supported":
        best = maxima[scores[maxima] >= scores[best] * 0.85][-1]
    selected = float(TEMPOS[best])
    # Pure regular pulses supply an unambiguous period, preserving 60 and 200
    # BPM as well as endpoints rather than imposing an EDM tempo range.
    if regular[index, 0] > 0:
        pulse = regular[index, 0]
        support = np.interp(pulse, TEMPOS, correlation)
        if support >= variant["minimum"]:
            selected = min(240.0, max(40.0, pulse))
            best = int(np.argmin(abs(TEMPOS - selected)))
    varied = False
    if variant["localGate"]:
        start, end = regular[index, 1:]
        varied = start > 0 and end > 0 and abs(start - end) / max(start, end) > 0.05
        for segment in (1, 2):
            local = curves[index, segment, 0]
            if local.max() >= 0.65 and local[best] < max(0.25, local.max() * 0.45):
                varied = True
    return (0.0 if varied else round(selected, 1)), bool(varied)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--folds", type=Path, required=True)
    parser.add_argument("--traces", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--prepare-only", action="store_true")
    args = parser.parse_args()
    manifest, division = read_json(args.manifest), read_json(args.folds)
    entries = manifest["entries"]
    expected = {identifier for identifier, name in division["assignments"].items() if name == "development"}
    if {row["id"] for row in entries} != expected or len(entries) != len(expected) or manifest["fullManifestSha256"] != division["manifestSha256"]:
        raise ValueError("Tempo calibration accepts the exact frozen development membership only")
    args.output.mkdir(parents=True, exist_ok=True)
    provenance = {"scriptSha256": fingerprint(Path(__file__)), "readerSha256": fingerprint(Path(__file__).with_name("ReadMusicFeatures.py")),
                  "manifestSha256": fingerprint(args.manifest), "foldsSha256": fingerprint(args.folds),
                  "traceProvenance": read_json(args.traces / "provenance.json"), "variants": VARIANTS,
                  "methodReferences": ["https://www.audiolabs-erlangen.de/resources/MIR/FMP/C6/C6S2_TempogramAutocorrelation.html",
                                       "https://www.audiolabs-erlangen.de/resources/MIR/tempogramtoolbox"]}
    provenance_file = args.output / "provenance.json"
    if provenance_file.exists() and read_json(provenance_file) != provenance:
        raise ValueError("Changed experiment requires a new output directory")
    save_json(provenance_file, provenance)
    records, values = [], []
    started = time.monotonic()
    for row in entries:
        folder = args.traces / "cases" / row["id"]
        result_path = folder / "result.json"
        if not result_path.is_file() and args.prepare_only:
            continue
        result = read_json(result_path); measured = result["observation"]
        if result["provenance"] != provenance["traceProvenance"] or measured["id"] != row["id"] or measured["sha256"] != row["sha256"] or "error" in measured:
            raise ValueError("Trace identity/provenance changed or failed")
        for name in ("referenceKey", "referenceBpm"):
            expected_annotation = key(row[name]) if name == "referenceKey" and name in row else row.get(name)
            if measured.get(name) != expected_annotation:
                raise ValueError("Trace changed its annotation")
        artifact = Path(measured["featuresFile"]).resolve()
        if artifact.parent != folder.resolve() or fingerprint(artifact) != measured["featuresSha256"]:
            raise ValueError("Trace artifact escaped its case or changed")
        cache = args.output / f"{row['id']}.npz"
        if cache.is_file():
            with np.load(cache, allow_pickle=False) as saved:
                if saved["checksum"].item() != measured["featuresSha256"] or saved["curves"].shape != (2, 3, 2, len(TEMPOS)):
                    raise ValueError("Stale experiment cache")
                value = (saved["curves"].copy(), saved["regular"].copy(), bool(saved["eligible"]))
        else:
            value = feature_vectors(read_features(artifact, measured["featuresSha256"]))
            with cache.open("xb") as stream:
                np.savez_compressed(stream, curves=value[0], regular=value[1], eligible=value[2], checksum=measured["featuresSha256"])
        if not np.isfinite(value[0]).all() or not np.isfinite(value[1]).all():
            raise ValueError("Invalid tempo experiment vector")
        records.append(measured); values.append(value)
        if len(records) % 25 == 0:
            print(f"TEMPO VECTORS prepared={len(records)}/{len(entries)} seconds={time.monotonic() - started:.1f}", flush=True)
    if args.prepare_only:
        print(f"TEMPO PREPARE available={len(records)}/{len(entries)}; no label scores computed", flush=True)
        return 0
    results = []
    for variant in VARIANTS:
        observations = []
        for measured, value in zip(records, values):
            bpm, varied = prediction(*value, variant)
            observations.append({**measured, "bpm": bpm, "possibleTempoVariation": varied})
        results.append({"variant": variant, "variationFlags": sum(row["possibleTempoVariation"] for row in observations),
                        "tempo": metrics(observations)["tempo"]})
    results.sort(key=lambda row: (row["tempo"]["accuracy"], row["tempo"]["halfDoubleAccuracy"]), reverse=True)
    save_json(args.output / "report.json", {"stage": "development model selection, not final acceptance", "provenance": provenance,
                                           "entries": len(records), "baseline": metrics(records)["tempo"], "results": results})
    for row in results[:12]:
        print(json.dumps(row), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
