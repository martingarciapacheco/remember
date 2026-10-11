"""Audit frozen v5 PCM results and aggregate the untouched holdout comparison."""
import argparse
import math
from pathlib import Path

import numpy as np

from CalibrateMusicKey import NAMES, VARIANTS, predict
from CalibrateMusicTempo import prediction
from EvaluateGiantSteps import fingerprint, save_json
from MergeMusicCorpus import key, metrics, read_json


def independent_counts(rows, metric):
    reference, estimate = ("referenceBpm", "bpm") if metric == "tempo" else ("referenceKey", "key")
    known = emitted = correct = unknown = unknown_correct = octave = 0
    for row in rows:
        if reference not in row:
            continue
        truth, value = row[reference], row[estimate]
        absent = truth == (0 if metric == "tempo" else "Unknown")
        unknown += absent; known += not absent
        missing = value == (0 if metric == "tempo" else "Unknown")
        if absent:
            unknown_correct += missing
            continue
        emitted += not missing
        if metric == "tempo":
            correct += value > 0 and abs(value / truth - 1) <= 0.04 + 1.0e-12
            octave += value > 0 and any(abs(value / (truth * scale) - 1) <= 0.04 + 1.0e-12 for scale in (0.5, 1, 2))
        else:
            correct += key(value) == key(truth)
    result = {"knownReferences": known, "unknownReferences": unknown, "knownEstimatesForKnownReferences": emitted,
              "correctKnown": correct, "coverage": emitted / known if known else None,
              "accuracy": correct / known if known else None, "unknownRecall": unknown_correct / unknown if unknown else None}
    if metric == "tempo":
        result["halfDoubleAccuracy"] = octave / known if known else None
    return result


def with_precision(rows):
    result = metrics(rows)
    for metric in ("tempo", "key"):
        independent = independent_counts(rows, metric)
        if independent != result[metric]:
            raise ValueError("Independent count differs from the report metric definition")
        emitted = independent["knownEstimatesForKnownReferences"]
        result[metric]["precision"] = independent["correctKnown"] / emitted if emitted else None
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--folds", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--key-experiments", type=Path, required=True)
    parser.add_argument("--tempo-experiments", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    manifest, division = read_json(args.manifest), read_json(args.folds)
    baseline, native = read_json(args.baseline), read_json(args.native / "report.json")
    snapshot, candidate = read_json(args.snapshot), read_json(args.candidate)
    expected = {row["id"]: row for row in manifest["entries"]}
    if (len(expected) != len(manifest["entries"]) or division["manifestSha256"] != fingerprint(args.manifest)
            or set(division["assignments"]) != set(expected)):
        raise ValueError("Original membership or fold definition changed")
    if (not native["complete"] or native["errors"] or native["analysisVersion"] != 5
            or native["provenance"]["manifestSha256"] != fingerprint(args.manifest)
            or native["foldsSha256"] != fingerprint(args.folds)
            or native["provenance"]["executableSha256"] != snapshot["executable"]["sha256"]
            or snapshot["candidateSha256"] != fingerprint(args.candidate)
            or candidate["foldsSha256"] != fingerprint(args.folds) or candidate["holdoutUsedForSelection"]):
        raise ValueError("Native run is incomplete or differs from the frozen candidate")
    if baseline["analysisVersion"] != 4 or not baseline["complete"] or baseline["errors"]:
        raise ValueError("Baseline is not the complete frozen version 4")
    for name, hash_name in (("executable", "executableSha256"), ("ffmpeg", "ffmpegSha256")):
        if fingerprint(Path(native["provenance"][name])) != native["provenance"][hash_name]:
            raise ValueError("Native executable or PCM decoder changed")
    for relative, hash_name in (("Tests/EvaluateGiantSteps.py", "runnerSha256"),
                               ("Tests/MergeMusicCorpus.py", "metricsSha256")):
        if fingerprint(args.snapshot.parent / relative) != native["provenance"][hash_name]:
            raise ValueError("Archived runner or metric source differs from native provenance")
    original = {row["id"]: row for row in baseline["entries"]}
    if len(native["entries"]) != len(expected) or {row["id"] for row in native["entries"]} != set(expected) or set(original) != set(expected):
        raise ValueError("Native/baseline omitted or duplicated a source")
    for record in snapshot["sources"]:
        archived = args.snapshot.parent / record["path"]
        if fingerprint(archived) != record["sha256"]:
            raise ValueError("Archived native source changed")
    key_index = next(i for i, variant in enumerate(VARIANTS) if variant["name"] == candidate["key"]["signal"]["name"])
    profiles = np.array([candidate["key"]["profiles"][mode] for mode in ("major", "minor")])
    key_error, bpm_error = 0.0, 0.0
    for index, row in enumerate(native["entries"]):
        source = expected[row["id"]]; old = original[row["id"]]
        path = (args.manifest.parent / source["file"]).resolve()
        if (Path(row["file"]).resolve() != path or row["sha256"] != source["sha256"]
                or fingerprint(path) != source["sha256"] or row["decodedPcmSha256"] != old["decodedPcmSha256"]):
            raise ValueError("Source or decoded PCM differs from baseline")
        for name in ("referenceKey", "referenceBpm"):
            wanted = key(source[name]) if name == "referenceKey" and name in source else source.get(name)
            if (name in source) != (name in row) or row.get(name) != wanted or old.get(name) != wanted:
                raise ValueError("Reference annotation changed")
        for name in ("durationSeconds", "sampleRate", "channels"):
            if row[name] != old[name]:
                raise ValueError("Decoded format changed")
        for name in ("bpm", "bpmConfidence", "keyConfidence", "keyCorrelation", "keyMargin", "analysisMs"):
            if not math.isfinite(row[name]):
                raise ValueError("Native result contains a non-finite value")
        case = read_json(args.native / "cases" / row["id"] / "result.json")
        if case["observation"] != row or case["provenance"] != native["provenance"]:
            raise ValueError("Native case cache differs from the complete report")
        # Only development features are reconstructed. Holdout remains an
        # aggregate acceptance comparison, without per-case tuning feedback.
        if division["assignments"][row["id"]] == "development":
            cache = args.key_experiments / f"{row['id']}.npz"
            if fingerprint(cache) != candidate["developmentVectorSha256"][row["id"]]:
                raise ValueError("Frozen development vector changed")
            with np.load(cache, allow_pickle=False) as data:
                value = data["vectors"][key_index][None].copy()
            labels, correlations, margins, support = predict(value, profiles)
            accepted = bool(support[0] and correlations[0] >= 0.6 and margins[0] >= 0.04)
            label = NAMES[labels[0] // 2] + (" minor" if labels[0] % 2 else " major") if accepted else "Unknown"
            key_error = max(key_error, abs(float(correlations[0]) - row["keyCorrelation"]), abs(float(margins[0]) - row["keyMargin"]))
            with np.load(args.tempo_experiments / f"{row['id']}.npz", allow_pickle=False) as data:
                values = data["curves"].copy(), data["regular"].copy(), bool(data["eligible"])
            bpm, varied = prediction(*values, candidate["tempo"]["variant"])
            bpm_error = max(bpm_error, abs(bpm - row["bpm"]))
            if label != row["key"] or key_error > 2.0e-5 or bpm_error > 0.100001 or varied != row["possibleTempoVariation"]:
                raise ValueError("Native development result differs from its frozen prototype")
        if (index + 1) % 100 == 0:
            print(f"NATIVE AUDIT verified={index + 1}/{len(expected)}", flush=True)
    if list(args.native.rglob(".pcm-*.wav")):
        raise ValueError("Completed native run retained temporary PCM")
    comparisons = {}
    for name in ("all", "development", "holdout"):
        selected = [row for row in native["entries"] if name == "all" or division["assignments"][row["id"]] == name]
        before = [original[row["id"]] for row in selected]
        comparisons[name] = {"entries": len(selected), "v4": with_precision(before), "v5": with_precision(selected)}
        for report, rows in ((baseline, before), (native, selected)):
            stored = report if name == "all" else report["folds"][name]
            recomputed = metrics(rows)
            if any(stored[metric] != recomputed[metric] for metric in recomputed):
                raise ValueError("Stored corpus/fold metrics differ from their observations")
    report = {"complete": True, "entries": len(expected), "errors": 0, "sameOriginalAndDecodedPcm": True,
              "nativeReportSha256": fingerprint(args.native / "report.json"), "baselineSha256": fingerprint(args.baseline),
              "snapshotSha256": fingerprint(args.snapshot), "candidateSha256": fingerprint(args.candidate),
              "auditScriptSha256": fingerprint(Path(__file__)),
              "maxDevelopmentKeyNumericError": key_error, "maxDevelopmentBpmError": bpm_error,
              "holdoutCaseDiagnosticsProduced": False, "temporaryPcmRemaining": 0, "comparisons": comparisons}
    save_json(args.output, report)
    print(f"NATIVE AUDIT COMPLETE entries={len(expected)}; same PCM, development parity, aggregate holdout only", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
