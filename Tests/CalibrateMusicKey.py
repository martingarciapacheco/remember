"""Development-only chroma/profile experiments; never reads holdout predictions.

Feature choices and three internal folds are deterministic. Cross-validation
scores help select a candidate; acceptance requires a separately frozen run on
the original holdout. NumPy is a developer dependency only.
"""
import argparse
import hashlib
import json
from pathlib import Path
import time

import numpy as np

from EvaluateGiantSteps import fingerprint, save_json
from MergeMusicCorpus import key, read_json
from ReadMusicFeatures import read_features


NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
PROFILES = np.array([[6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88],
                     [6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17]])
# Freeze this grid before looking at experiment scores. Each variant uses the
# same peaks; no MP3 decoding or per-song parameter selection happens here.
VARIANTS = [{"name": "v4-chroma", "original": True}]
for upper in (2093, 5000):
    for whitening in (0.0, 0.5, 1.0):
        for power in (1.0, 0.5):
            for tuning in (False, True):
                VARIANTS.append({"name": f"peaks-{upper}-w{whitening}-p{power}-t{int(tuning)}",
                                 "upper": upper, "whitening": whitening, "power": power, "tuning": tuning})
BLENDS = (0.0, 0.5, 1.0)


def feature_vectors(trace):
    original = trace["chroma"].mean(axis=0, dtype=np.float64)
    peaks, frames = trace["peaks"].astype(np.float64), trace["peakFrames"]
    count = len(trace["chroma"])
    vectors = [original]
    if len(peaks) == 0:
        return np.vstack([original] + [np.zeros(12) for _ in VARIANTS[1:]])
    pitch = 69 + 12 * np.log2(peaks[:, 0] / 440)
    # Circular tuning is bounded to half a semitone, and weighted by magnitude;
    # peaks between notes cannot drag a well-supported note by a whole class.
    phase = np.exp(2j * np.pi * pitch)
    for variant in VARIANTS[1:]:
        use = peaks[:, 0] <= variant["upper"]
        positions, index = pitch[use], frames[use]
        magnitude = peaks[use, 1]
        weight = (magnitude / np.maximum(peaks[use, 2], 1.0e-12) ** variant["whitening"]) ** variant["power"]
        shift = 0.0
        if variant["tuning"] and magnitude.sum() > 0:
            vector = np.sum(magnitude * phase[use]) / magnitude.sum()
            if abs(vector) >= 0.1:
                shift = np.angle(vector) / (2 * np.pi)
        positions = positions - shift
        nearest = np.floor(positions + 0.5).astype(np.int64)
        # Cosine-squared support within half a semitone avoids distributing a
        # strong, accurately interpolated peak between two adjacent notes.
        weight *= np.cos(np.pi * (positions - nearest)) ** 2
        chroma = np.bincount(index * 12 + nearest % 12, weights=weight, minlength=count * 12).reshape(count, 12)
        sums = chroma.sum(axis=1)
        valid = (trace["energy"] / 8192 >= 1.0e-9) & (sums > 0)
        chroma[valid] /= sums[valid, None]
        chroma[~valid] = 0
        vectors.append(chroma.mean(axis=0))
    return np.vstack(vectors)


def key_index(value):
    if value == "Unknown":
        return -1
    root, mode = value.split()
    return NAMES.index(root) * 2 + (mode == "minor")


def unit(values):
    centred = values - values.mean(axis=-1, keepdims=True)
    length = np.linalg.norm(centred, axis=-1, keepdims=True)
    return np.divide(centred, length, out=np.zeros_like(centred), where=length > 0)


def fit_profiles(vectors, labels, blend):
    if blend == 0:
        return unit(PROFILES)
    aligned = np.vstack([np.roll(vector / max(vector.sum(), 1.0e-12), -(label // 2)) for vector, label in zip(vectors, labels)])
    learnt = np.vstack([aligned[labels % 2 == mode].mean(axis=0) for mode in (0, 1)])
    # Blend centred unit profiles so amplitude scaling cannot change its weight.
    return unit((1 - blend) * unit(PROFILES) + blend * unit(learnt))


def predict(vectors, profiles):
    bank = np.vstack([np.roll(profiles[mode], root) for root in range(12) for mode in (0, 1)])
    scores = unit(vectors) @ bank.T
    labels = scores.argmax(axis=1)
    sorted_scores = np.sort(scores, axis=1)
    correlation, margin = sorted_scores[:, -1], sorted_scores[:, -1] - sorted_scores[:, -2]
    maxima, minima, sums = vectors.max(axis=1), vectors.min(axis=1), vectors.sum(axis=1)
    supported = (maxima > 0) & (maxima <= sums * 0.6) & (maxima - minima >= maxima * 0.12)
    return labels, correlation, margin, supported


def score(labels, predicted, accepted):
    correct = (labels == predicted) & accepted
    count = len(labels)
    emitted = int(accepted.sum())
    by_mode = {mode: {"references": int((labels % 2 == index).sum()),
                     "correct": int(correct[labels % 2 == index].sum()),
                     "emitted": int(accepted[labels % 2 == index].sum())}
               for index, mode in enumerate(("major", "minor"))}
    return {"references": count, "correct": int(correct.sum()), "emitted": emitted,
            "accuracy": float(correct.mean()), "coverage": float(accepted.mean()),
            "precision": int(correct.sum()) / emitted if emitted else None,
            "balancedAccuracy": float(np.mean([row["correct"] / row["references"] for row in by_mode.values()])),
            "byMode": by_mode}


def experiment(records, vectors, folds):
    selected = np.array(["referenceKey" in row and row["referenceKey"] != "Unknown" for row in records])
    labels = np.array([key_index(row["referenceKey"]) for row in records if "referenceKey" in row and row["referenceKey"] != "Unknown"])
    values, assignments = vectors[selected], folds[selected]
    results = []
    for index, variant in enumerate(VARIANTS):
        for blend in BLENDS:
            predictions = np.empty(len(labels), dtype=np.int64)
            correlations, margins = np.empty(len(labels)), np.empty(len(labels))
            supported = np.empty(len(labels), dtype=bool)
            for fold in range(3):
                train, test = assignments != fold, assignments == fold
                if len(np.unique(labels[train] % 2)) != 2:
                    raise ValueError("An internal training fold lost a mode")
                model = fit_profiles(values[train, index], labels[train], blend)
                prediction, correlation, margin, support = predict(values[test, index], model)
                predictions[test], correlations[test], margins[test], supported[test] = prediction, correlation, margin, support
            # Current production gates are compared with an ungated diagnostic;
            # ungated scores are not an acceptance candidate for noise/ambiguity.
            for gate in ("v4", "diagnostic-no-score-gate"):
                accepted = supported & ((correlations >= 0.6) & (margins >= 0.04) if gate == "v4" else True)
                results.append({"variant": variant["name"], "profileBlend": blend, "gate": gate,
                                **score(labels, predictions, accepted)})
    results.sort(key=lambda row: (row["balancedAccuracy"], row["accuracy"], row["precision"] or 0), reverse=True)
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--folds", type=Path, required=True)
    parser.add_argument("--traces", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--prepare-only", action="store_true", help="Cache available development vectors without computing label scores")
    args = parser.parse_args()
    manifest = read_json(args.manifest)
    entries, division = manifest["entries"], read_json(args.folds)
    expected = {identifier for identifier, name in division["assignments"].items() if name == "development"}
    if {row["id"] for row in entries} != expected or len(entries) != len(expected) or manifest["fullManifestSha256"] != division["manifestSha256"]:
        raise ValueError("Calibration accepts the exact frozen development membership only")
    args.output.mkdir(parents=True, exist_ok=True)
    provenance = {"scriptSha256": fingerprint(Path(__file__)), "readerSha256": fingerprint(Path(__file__).with_name("ReadMusicFeatures.py")),
                  "manifestSha256": fingerprint(args.manifest), "foldsSha256": fingerprint(args.folds),
                  "traceProvenance": read_json(args.traces / "provenance.json"),
                  "variants": VARIANTS, "profileBlends": BLENDS,
                  "internalPolicy": "SHA256(original audio SHA256 text), first 32-bit word modulo 3; identical bytes stay together",
                  "internalAssignments": {row["id"]: int(hashlib.sha256(row["sha256"].encode("ascii")).hexdigest()[:8], 16) % 3 for row in entries}}
    provenance_file = args.output / "provenance.json"
    if provenance_file.exists() and read_json(provenance_file) != json.loads(json.dumps(provenance)):
        raise ValueError("Changed experiment requires a new output directory")
    save_json(provenance_file, provenance)
    vectors, records, folds = [], [], []
    started = time.monotonic()
    for row in entries:
        folder = args.traces / "cases" / row["id"]
        result_path = folder / "result.json"
        if not result_path.is_file() and args.prepare_only:
            continue
        result = read_json(result_path)
        measured = result["observation"]
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
                if saved["checksum"].item() != measured["featuresSha256"] or saved["vectors"].shape != (len(VARIANTS), 12):
                    raise ValueError("Stale experiment cache")
                value = saved["vectors"].copy()
        else:
            value = feature_vectors(read_features(artifact, measured["featuresSha256"]))
            with cache.open("xb") as stream:
                np.savez_compressed(stream, vectors=value, checksum=measured["featuresSha256"])
        if not np.isfinite(value).all() or (value < 0).any():
            raise ValueError("Invalid experiment vector")
        records.append(measured); vectors.append(value); folds.append(provenance["internalAssignments"][row["id"]])
        if len(records) % 25 == 0:
            print(f"KEY VECTORS prepared={len(records)}/{len(entries)} seconds={time.monotonic() - started:.1f}", flush=True)
    if args.prepare_only:
        print(f"KEY PREPARE available={len(records)}/{len(entries)}; no label scores computed", flush=True)
        return 0
    if len(records) != len(entries):
        raise ValueError("Incomplete development features")
    results = experiment(records, np.stack(vectors), np.array(folds))
    save_json(args.output / "report.json", {"stage": "development model selection, not final acceptance", "provenance": provenance,
                                           "entries": len(records), "results": results})
    for row in results[:12]:
        print(json.dumps(row, ensure_ascii=False), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
