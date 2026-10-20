"""Verify complete development traces against the frozen PCM baseline."""
import argparse
from pathlib import Path

import numpy as np

from CalibrateMusicKey import PROFILES, predict, unit
from EvaluateGiantSteps import fingerprint, save_json
from MergeMusicCorpus import key, metrics, read_json
from ReadMusicFeatures import read_features


FIELDS = ("bpm", "bpmConfidence", "alternativeBpm", "possibleTempoVariation", "startBpm", "endBpm",
          "key", "keyConfidence", "keyCorrelation", "keyMargin", "decodedPcmSha256", "durationSeconds", "channels", "sampleRate")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--folds", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--traces", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    manifest, division = read_json(args.manifest), read_json(args.folds)
    expected = {row["id"]: row for row in manifest["entries"]}
    if (set(expected) != {identifier for identifier, name in division["assignments"].items() if name == "development"}
            or len(expected) != len(manifest["entries"]) or manifest["fullManifestSha256"] != division["manifestSha256"]):
        raise ValueError("Audit requires exactly the frozen development membership")
    baseline, report = read_json(args.baseline), read_json(args.traces / "report.json")
    if (not baseline["complete"] or baseline["errors"] or not report["complete"] or report["errors"]
            or baseline["provenance"]["manifestSha256"] != division["manifestSha256"]
            or report["provenance"]["manifestSha256"] != fingerprint(args.manifest)
            or report["analysisVersion"] != baseline["analysisVersion"] or report["analysisVersion"] != 4):
        raise ValueError("Incomplete, changed or mixed baseline/trace provenance")
    original = {row["id"]: row for row in baseline["entries"] if row["id"] in expected}
    # Only development predictions are compared or diagnosed here.
    del baseline
    if len(report["entries"]) != len(expected) or {row["id"] for row in report["entries"]} != set(expected) or set(original) != set(expected):
        raise ValueError("Trace report omitted or duplicated an input")
    seen, total_bytes, key_frames, tempo_frames, peak_count = set(), 0, 0, 0, 0
    differences = {name: 0.0 for name in FIELDS}
    reconstruction_error = 0.0
    for index, row in enumerate(report["entries"]):
        wanted, before = expected[row["id"]], original[row["id"]]
        source = (args.manifest.parent / wanted["file"]).resolve()
        if row["sha256"] != wanted["sha256"] or Path(row["file"]).resolve() != source or fingerprint(source) != wanted["sha256"]:
            raise ValueError("Original source identity changed")
        for name in ("referenceKey", "referenceBpm"):
            annotation = key(wanted[name]) if name == "referenceKey" and name in wanted else wanted.get(name)
            if (name in wanted) != (name in row) or row.get(name) != annotation or before.get(name) != annotation:
                raise ValueError("Reference presence or annotation changed")
        for name in FIELDS:
            if isinstance(before[name], (int, float)) and not isinstance(before[name], bool):
                delta = abs(before[name] - row[name])
                differences[name] = max(differences[name], delta)
                if delta > 1.0e-6:
                    raise ValueError(f"Frozen production prediction changed: {row['id']} {name}")
            elif before[name] != row[name]:
                raise ValueError(f"Frozen categorical result or decoded PCM changed: {row['id']} {name}")
        folder = (args.traces / "cases" / row["id"]).resolve()
        case = read_json(folder / "result.json")
        if case["observation"] != row or case["provenance"] != report["provenance"]:
            raise ValueError("Case cache differs from the complete report")
        artifact = Path(row["featuresFile"]).resolve()
        if artifact.parent != folder or artifact in seen:
            raise ValueError("Feature artifact escaped its case or was reused")
        seen.add(artifact)
        trace = read_features(artifact, row["featuresSha256"])
        # Reconstruct the original profile estimate independently from exported
        # chroma. Float accumulations in C++ permit a small numeric tolerance.
        vector = trace["chroma"].mean(axis=0, dtype=np.float64)[None]
        predicted, correlation, margin, support = predict(vector, unit(PROFILES))
        reconstruction_error = max(reconstruction_error, abs(float(correlation[0]) - row["keyCorrelation"]), abs(float(margin[0]) - row["keyMargin"]))
        accepted = bool(support[0] and correlation[0] >= 0.6 and margin[0] >= 0.04)
        names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
        estimate = names[predicted[0] // 2] + (" minor" if predicted[0] % 2 else " major") if accepted else "Unknown"
        if estimate != row["key"] or reconstruction_error > 1.0e-5:
            raise ValueError("Exported chroma cannot reconstruct its original estimate")
        total_bytes += artifact.stat().st_size
        key_frames += len(trace["chroma"]); tempo_frames += len(trace["tempo"]); peak_count += len(trace["peaks"])
        if (index + 1) % 50 == 0:
            print(f"FEATURE AUDIT verified={index + 1}/{len(expected)}", flush=True)
    temporaries = list(args.traces.rglob(".pcm-*.wav"))
    if temporaries:
        raise ValueError("Finished trace extraction retained temporary PCM")
    result = {"complete": True, "entries": len(expected), "holdoutCompared": False,
              "manifestSha256": fingerprint(args.manifest), "foldsSha256": fingerprint(args.folds),
              "baselineSha256": fingerprint(args.baseline), "traceReportSha256": fingerprint(args.traces / "report.json"),
              "artifacts": len(seen), "totalFeatureBytes": total_bytes, "keyFrames": key_frames, "tempoFrames": tempo_frames, "spectralPeaks": peak_count,
              "maxBaselineDifferences": differences, "maxReconstructedKeyNumericError": reconstruction_error,
              "temporaryPcmRemaining": 0, "metrics": metrics(report["entries"])}
    save_json(args.output, result)
    print(f"FEATURE AUDIT COMPLETE entries={len(expected)} bytes={total_bytes}; baseline equivalent, no holdout comparison", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
