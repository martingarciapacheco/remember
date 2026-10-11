"""Freeze a development candidate after rejecting synthetic regressions.

No holdout predictions are loaded. This file records the candidate parameters,
learnt profiles, input hashes and acceptance constraints for a later native run.
"""
import argparse
from pathlib import Path

import numpy as np

from CalibrateMusicKey import VARIANTS, fit_profiles, key_index, predict
from EvaluateGiantSteps import fingerprint, save_json
from MergeMusicCorpus import key, read_json


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--folds", type=Path, required=True)
    parser.add_argument("--key-experiments", type=Path, required=True)
    parser.add_argument("--tempo-experiments", type=Path, required=True)
    parser.add_argument("--synthetic", type=Path, required=True)
    parser.add_argument("--frame-local", action="store_true", help="Preserve exact tail reuse without a global tuning dependency")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    manifest, division = read_json(args.manifest), read_json(args.folds)
    entries = manifest["entries"]
    expected = {identifier for identifier, name in division["assignments"].items() if name == "development"}
    if {row["id"] for row in entries} != expected or len(entries) != len(expected) or manifest["fullManifestSha256"] != division["manifestSha256"]:
        raise ValueError("Candidate must use the exact frozen development membership")
    key_report = read_json(args.key_experiments / "report.json")
    tempo_report = read_json(args.tempo_experiments / "report.json")
    for report in (key_report, tempo_report):
        if (report["entries"] != len(entries) or report["provenance"]["manifestSha256"] != fingerprint(args.manifest)
                or report["provenance"]["foldsSha256"] != fingerprint(args.folds)):
            raise ValueError("Incomplete experiment or another development definition")
    if key_report["provenance"]["variants"] != VARIANTS:
        raise ValueError("Key variant implementation changed")
    with np.load(args.synthetic / "vectors.npz", allow_pickle=False) as data:
        synthetic = data["vectors"].copy(); truth = data["labels"].copy()
    if synthetic.shape != (50, len(VARIANTS), 12) or truth.shape != (50,) or (truth[:48] < 0).any() or (truth[48:] != -1).any():
        raise ValueError("Synthetic acceptance fixture changed dimensions")
    values, labels, training_ids, cache_hashes = [], [], [], {}
    for entry in entries:
        cache = args.key_experiments / f"{entry['id']}.npz"
        cache_hashes[entry["id"]] = fingerprint(cache)
        if "referenceKey" in entry and key(entry["referenceKey"]) != "Unknown":
            with np.load(cache, allow_pickle=False) as data:
                values.append(data["vectors"].copy())
            labels.append(key_index(key(entry["referenceKey"]))); training_ids.append(entry["id"])
    values, labels = np.stack(values), np.array(labels)
    rejected, chosen = [], None
    for row in key_report["results"]:
        if row["gate"] != "v4":
            continue
        index = next(i for i, variant in enumerate(VARIANTS) if variant["name"] == row["variant"])
        if args.frame_local and VARIANTS[index].get("tuning", False):
            continue
        profiles = fit_profiles(values[:, index], labels, row["profileBlend"])
        predicted, correlation, margin, support = predict(synthetic[:, index], profiles)
        accepted = support & (correlation >= 0.6) & (margin >= 0.04)
        actual = np.where(accepted, predicted, -1)
        if not np.array_equal(actual, truth):
            rejected.append({"variant": row["variant"], "profileBlend": row["profileBlend"],
                             "syntheticCorrect": int((actual == truth).sum()), "syntheticTotal": len(truth)})
            continue
        chosen = {"developmentCrossValidation": row, "signal": VARIANTS[index],
                  "profiles": {"major": profiles[0].tolist(), "minor": profiles[1].tolist()},
                  "trainingReferences": len(training_ids), "trainingIds": training_ids,
                  "syntheticCorrect": len(truth), "syntheticTotal": len(truth)}
        break
    if chosen is None:
        raise ValueError("No key candidate passed the unchanged ambiguity and tonal contracts")
    # Local variation, including a true octave change, remains a required
    # product behavior. Preserve both explicitly unknown development labels.
    tempo = next((row for row in tempo_report["results"] if row["variant"]["localGate"]
                  and row["tempo"]["unknownRecall"] == tempo_report["baseline"]["unknownRecall"]), None)
    if tempo is None:
        raise ValueError("No tempo candidate preserved variation/unknown behavior")
    candidate = {"stage": "frozen development candidate; native implementation and holdout acceptance pending",
                 "developmentManifestSha256": fingerprint(args.manifest), "foldsSha256": fingerprint(args.folds),
                 "keyExperimentSha256": fingerprint(args.key_experiments / "report.json"),
                 "tempoExperimentSha256": fingerprint(args.tempo_experiments / "report.json"),
                 "syntheticVectorsSha256": fingerprint(args.synthetic / "vectors.npz"),
                 "selectorSha256": fingerprint(Path(__file__)), "developmentVectorSha256": cache_hashes,
                 "frameLocalConstraint": args.frame_local,
                 "key": chosen, "keyCandidatesRejectedBySyntheticContract": rejected, "tempo": tempo,
                 "holdoutUsedForSelection": False}
    if args.output.exists() and read_json(args.output) != candidate:
        raise ValueError("A frozen candidate cannot be replaced by a different one")
    save_json(args.output, candidate)
    print(f"FROZEN KEY {chosen['signal']['name']} blend={chosen['developmentCrossValidation']['profileBlend']} CV={chosen['developmentCrossValidation']['accuracy']:.4f} synthetic={len(truth)}/{len(truth)}")
    print(f"FROZEN TEMPO {tempo['variant']} development={tempo['tempo']['accuracy']:.4f}; native/holdout pending")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
