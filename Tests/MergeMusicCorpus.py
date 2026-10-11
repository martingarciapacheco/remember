"""Merge disjoint corpus evaluations, checking identity and recomputing metrics.

Abstentions and failed files remain in the reference denominators. A completed
evaluation is different from an accuracy acceptance gate. Identical executable
and analysis versions are required; the original manifest covers every ID once.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import statistics
import uuid


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def fingerprint(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def key(value):
    if value == "Unknown":
        return value
    match = re.fullmatch(r"([A-G])([#b]?) (major|minor)", value)
    if not match:
        raise ValueError(f"Invalid key: {value}")
    root = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}[match[1]]
    root += {"": 0, "#": 1, "b": -1}[match[2]]
    return ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"][root % 12] + " " + match[3]


def metrics(rows):
    result = {}
    for metric, reference, estimate in (("tempo", "referenceBpm", "bpm"), ("key", "referenceKey", "key")):
        counts = {"knownReferences": 0, "unknownReferences": 0,
                  "knownEstimatesForKnownReferences": 0, "correctKnown": 0}
        unknown_correct = half_double = 0
        for row in rows:
            if reference not in row:
                continue
            truth = row[reference]
            unknown = truth == (0 if metric == "tempo" else "Unknown")
            counts["unknownReferences" if unknown else "knownReferences"] += 1
            if "error" in row:
                continue
            predicted = row[estimate]
            if metric == "tempo":
                if not isinstance(predicted, (int, float)) or not math.isfinite(predicted) or predicted < 0:
                    raise ValueError("Invalid estimated tempo")
                predicted_unknown = predicted == 0
                exact = not predicted_unknown and abs(predicted - truth) <= truth * 0.04
                alternative = any(predicted > 0 and abs(predicted - truth * multiple) <= truth * multiple * 0.04
                                  for multiple in (0.5, 1, 2))
            else:
                predicted = key(predicted)
                predicted_unknown = predicted == "Unknown"
                exact = predicted == key(truth)
            if unknown:
                unknown_correct += predicted_unknown
            else:
                counts["knownEstimatesForKnownReferences"] += not predicted_unknown
                counts["correctKnown"] += exact
                if metric == "tempo":
                    half_double += alternative
        known = counts["knownReferences"]
        unknown = counts["unknownReferences"]
        counts["coverage"] = counts["knownEstimatesForKnownReferences"] / known if known else None
        counts["accuracy"] = counts["correctKnown"] / known if known else None
        counts["unknownRecall"] = unknown_correct / unknown if unknown else None
        if metric == "tempo":
            counts["halfDoubleAccuracy"] = half_double / known if known else None
        result[metric] = counts
    timings = sorted(row["analysisMs"] for row in rows if "error" not in row)
    if timings:
        if any(not math.isfinite(value) or value < 0 for value in timings):
            raise ValueError("Invalid analysis timing")
        result["analysisLatency"] = {"meanMs": statistics.mean(timings), "medianMs": statistics.median(timings),
                                     "p95Ms": timings[math.ceil(0.95 * len(timings)) - 1], "maxMs": timings[-1]}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--reports", type=Path, nargs="+", required=True)
    parser.add_argument("--folds", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    definition = read_json(args.manifest)
    expected = {entry["id"]: entry for entry in definition["entries"]}
    if len(expected) != len(definition["entries"]):
        raise ValueError("Original manifest contains duplicate IDs")
    forbidden = {args.manifest.resolve(), *(path.resolve() for path in args.reports)}
    forbidden |= {(args.manifest.parent / entry["file"]).resolve() for entry in expected.values()}
    observations = {}
    parts = []
    versions = set()
    executable_hashes = set()
    for path in args.reports:
        report = read_json(path)
        manifest_path = Path(report["manifest"])
        forbidden.add(manifest_path.resolve())
        if fingerprint(manifest_path) != report["manifestSha256"]:
            raise ValueError("A partition manifest changed after evaluation")
        partition = read_json(manifest_path)
        partition_ids = {entry["id"] for entry in partition["entries"]}
        observed_ids = {row["id"] for row in report["entries"]}
        if partition_ids != observed_ids or len(observed_ids) != len(report["entries"]):
            raise ValueError("Partition report is missing or duplicates an input")
        versions.add(report["analysisVersion"])
        executable_hashes.add(report["executableSha256"])
        for row in report["entries"]:
            identifier = row["id"]
            if identifier not in expected or identifier in observations:
                raise ValueError("Partition reports overlap or include an unrequested ID")
            reference = expected[identifier]
            if row["sha256"].lower() != reference["sha256"].lower():
                raise ValueError("Partition audio checksum differs from the original manifest")
            if Path(row["file"]).resolve() != (args.manifest.parent / reference["file"]).resolve():
                raise ValueError("Partition audio path differs from the original manifest")
            for field in ("referenceBpm", "referenceKey"):
                if (field in row) != (field in reference):
                    raise ValueError("Partition invented or omitted a reference annotation")
                if field in reference:
                    want = key(reference[field]) if field == "referenceKey" else reference[field]
                    if row[field] != want:
                        raise ValueError("Partition reference differs from the original annotation")
            observations[identifier] = row
        independent = metrics(report["entries"])
        for metric in ("tempo", "key"):
            if independent[metric] != report[metric]:
                raise ValueError(f"Partition {metric} metrics disagree with its observations")
        parts.append({"path": str(path.resolve()), "sha256": fingerprint(path), "entries": len(report["entries"]),
                      "manifestSha256": report["manifestSha256"], "complete": report["complete"]})
    if set(observations) != set(expected) or len(versions) != 1 or len(executable_hashes) != 1:
        raise ValueError("Incomplete union or mixed detector builds; no combined report published")
    if args.output.resolve() in forbidden:
        raise ValueError("Combined report would overwrite an input")
    rows = [observations[entry["id"]] for entry in definition["entries"]]
    errors = sum("error" in row for row in rows)
    result = {"schemaVersion": 1, "dataset": definition["dataset"], "source": definition["source"],
              "manifest": str(args.manifest.resolve()), "manifestSha256": fingerprint(args.manifest),
              "analysisVersion": versions.pop(), "executableSha256": executable_hashes.pop(),
              "complete": errors == 0 and all(part["complete"] for part in parts), "errors": errors,
              "parts": parts, "tempoToleranceRelative": 0.04, "entries": rows, **metrics(rows)}
    if args.folds:
        folds = read_json(args.folds)
        if folds["manifestSha256"].lower() != fingerprint(args.manifest) or set(folds["assignments"]) != set(expected):
            raise ValueError("Fold assignment does not cover this exact original manifest")
        result["foldsSha256"] = fingerprint(args.folds)
        result["folds"] = {}
        for name in sorted(set(folds["assignments"].values())):
            selected = [row for row in rows if folds["assignments"][row["id"]] == name]
            result["folds"][name] = {"entries": len(selected), **metrics(selected)}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_name(f".corpus-{uuid.uuid4().hex}.tmp")
    try:
        with temporary.open("x", encoding="utf-8", newline="\n") as stream:
            json.dump(result, stream, ensure_ascii=False, indent=2, allow_nan=False)
            stream.write("\n"); stream.flush(); os.fsync(stream.fileno())
        os.replace(temporary, args.output)
    finally:
        if temporary.exists():
            temporary.unlink()
    print(f"MERGED entries={len(rows)} errors={errors} complete={result['complete']}")
    return 0 if result["complete"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
