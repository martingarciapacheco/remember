"""Evaluate every GiantSteps preview through decoded float PCM, two workers.

MP3 bytes stay pinned to the official download manifest. FFmpeg preserves the
original rate/channels and writes float WAV without clipping to integer PCM.
Each detector process has a deadline and reads the same PCM format as capture.
Only two temporary WAVs exist at a time; per-case results survive interruption.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time
import uuid

from MergeMusicCorpus import key, metrics, read_json


def fingerprint(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(65536), b""):
            digest.update(block)
    return digest.hexdigest()


def save_json(path, value):
    temporary = path.with_name(f".corpus-{uuid.uuid4().hex}.tmp")
    try:
        with temporary.open("x", encoding="utf-8", newline="\n") as stream:
            json.dump(value, stream, ensure_ascii=False, indent=2, allow_nan=False)
            stream.write("\n"); stream.flush(); os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def evaluate(entry, options, provenance):
    identifier = entry["id"]
    if not re.fullmatch(r"[0-9]+\.LOFI", identifier):
        raise ValueError("Unexpected GiantSteps ID")
    case_folder = options.output / "cases" / identifier
    case_folder.mkdir(parents=True, exist_ok=True)
    case_result = case_folder / "result.json"
    source = (options.manifest.parent / entry["file"]).resolve()
    # The invocation explicitly names the complete corpus manifest; derivative
    # destinations are confined to its separate generated result directory.
    if not source.is_relative_to(options.manifest.parent.resolve()):
        raise ValueError("Audio source escaped the named corpus directory")
    observation = {"id": identifier, "file": str(source), "sha256": entry["sha256"].lower()}
    for field in ("referenceBpm", "referenceKey"):
        if field in entry:
            observation[field] = key(entry[field]) if field == "referenceKey" else entry[field]
    if case_result.exists():
        # An interrupted or manually damaged cache is a miss, not a reason to
        # abandon every other independent case in this run.
        try:
            previous = read_json(case_result)
            cached = previous["observation"]
            if not isinstance(previous, dict) or not isinstance(cached, dict):
                raise ValueError("Invalid case cache")
            artifact_valid = not options.features
            if options.features:
                artifact = Path(cached["featuresFile"]).resolve()
                artifact_valid = (artifact.parent == case_folder.resolve() and artifact.is_file()
                                  and fingerprint(artifact) == cached.get("featuresSha256"))
            if (previous.get("provenance") == provenance and cached.get("sha256") == observation["sha256"]
                    and artifact_valid and "error" not in cached and fingerprint(source) == observation["sha256"]):
                return cached
        except (OSError, ValueError, TypeError, KeyError):
            pass
    wav = case_folder / f".pcm-{uuid.uuid4().hex}.wav"
    manifest = case_folder / "input.json"
    report = case_folder / "detector.json"
    started = time.monotonic()
    try:
        if fingerprint(source) != observation["sha256"]:
            raise ValueError("Original MP3 differs from the pinned manifest")
        conversion = time.monotonic()
        command = [str(options.ffmpeg), "-nostdin", "-hide_banner", "-loglevel", "error", "-n", "-threads", "1",
                   "-i", str(source), "-map", "0:a:0", "-vn", "-map_metadata", "-1", "-c:a", "pcm_f32le",
                   "-threads", "1", "-f", "wav", str(wav)]
        converted = subprocess.run(command, capture_output=True, timeout=60, check=False)
        if converted.returncode != 0:
            raise ValueError("PCM conversion failed: " + converted.stderr.decode("utf-8", errors="replace")[:2000])
        conversion_ms = (time.monotonic() - conversion) * 1000
        wav_hash = fingerprint(wav)
        record = {"id": identifier, "file": str(wav), "sha256": wav_hash}
        for field in ("referenceBpm", "referenceKey"):
            if field in entry:
                record[field] = entry[field]
        save_json(manifest, {"schemaVersion": 1, "dataset": "GiantSteps decoded PCM case", "source": provenance["source"],
                             "entries": [record]})
        # Both child executables belong to this case. subprocess.run terminates
        # its own child on a timeout, without touching a DAW or the browser app.
        mode = "--music-features" if options.features else "--music-corpus"
        detected = subprocess.run([str(options.detector), mode, str(manifest), str(report)],
                                  cwd=case_folder, capture_output=True, timeout=60, check=False)
        if detected.returncode != 0:
            raise ValueError("Detector failed: " + (detected.stdout + detected.stderr).decode("utf-8", errors="replace")[:2000])
        result = read_json(report)
        if (not result["complete"] or result["errors"] != 0 or len(result["entries"]) != 1
                or result["executableSha256"] != provenance["executableSha256"]
                or result["manifestSha256"] != fingerprint(manifest)):
            raise ValueError("Case returned an incomplete result or another executable")
        measured = result["entries"][0]
        if measured["id"] != identifier or measured["sha256"] != wav_hash or Path(measured["file"]).resolve() != wav.resolve():
            raise ValueError("Case result changed the input identity or PCM checksum")
        if options.features:
            artifact = Path(measured["featuresFile"]).resolve()
            if artifact.parent != case_folder.resolve() or not artifact.is_file() or fingerprint(artifact) != measured["featuresSha256"]:
                raise ValueError("Feature export escaped its case or has a stale checksum")
        for field in ("referenceBpm", "referenceKey"):
            if (field in measured) != (field in observation) or (field in observation and measured[field] != observation[field]):
                raise ValueError("Case result changed its reference annotation")
        observation.update({name: value for name, value in measured.items() if name not in ("file", "sha256")})
        observation["decodedPcmSha256"] = wav_hash
        observation["conversionMs"] = conversion_ms
        observation["analysisVersion"] = result["analysisVersion"]
        observation["pcmRetained"] = False
    except Exception as error:
        observation["error"] = str(error)
    finally:
        if wav.exists():
            wav.unlink()
    observation["caseMs"] = (time.monotonic() - started) * 1000
    save_json(case_result, {"provenance": provenance, "observation": observation})
    return observation


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--detector", type=Path, required=True)
    parser.add_argument("--ffmpeg", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="Separate result/cache directory")
    parser.add_argument("--folds", type=Path)
    parser.add_argument("--features", action="store_true", help="Export calibration features through the canonical PCM path")
    args = parser.parse_args()
    args.manifest = args.manifest.resolve(); args.detector = args.detector.resolve()
    args.ffmpeg = args.ffmpeg.resolve(); args.output = args.output.resolve()
    definition = read_json(args.manifest)
    entries = definition["entries"]
    if len({entry["id"] for entry in entries}) != len(entries) or not entries:
        raise ValueError("Empty or duplicated corpus inputs")
    if args.output == args.manifest.parent or args.output == args.manifest:
        raise ValueError("Results need a separate generated directory")
    args.output.mkdir(parents=True, exist_ok=True)
    provenance = {"source": definition["source"], "manifest": str(args.manifest), "manifestSha256": fingerprint(args.manifest),
                  "executable": str(args.detector), "executableSha256": fingerprint(args.detector),
                  "ffmpeg": str(args.ffmpeg), "ffmpegSha256": fingerprint(args.ffmpeg),
                  "runnerSha256": fingerprint(Path(__file__)),
                  "metricsSha256": fingerprint(Path(__file__).with_name("MergeMusicCorpus.py")),
                  "featureExport": args.features,
                  "pcmConversion": "Native rate/channels, pcm_f32le; two workers; 60 s per decoder/detector child"}
    save_json(args.output / "provenance.json", provenance)
    rows = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
        for row in executor.map(lambda entry: evaluate(entry, args, provenance), entries):
            rows.append(row)
            if len(rows) % 25 == 0 or len(rows) == len(entries):
                print(f"PCM CORPUS evaluated={len(rows)}/{len(entries)} errors={sum('error' in item for item in rows)}", flush=True)
    errors = sum("error" in row for row in rows)
    versions = {row["analysisVersion"] for row in rows if "error" not in row}
    if len(versions) > 1:
        raise ValueError("Cases mixed analysis versions")
    result = {"schemaVersion": 2, "dataset": definition["dataset"], "provenance": provenance,
              "complete": errors == 0, "errors": errors, "entries": rows, "tempoToleranceRelative": 0.04, **metrics(rows)}
    if versions:
        result["analysisVersion"] = versions.pop()
    if args.folds:
        folds = read_json(args.folds)
        if folds["manifestSha256"].lower() != provenance["manifestSha256"] or set(folds["assignments"]) != {row["id"] for row in rows}:
            raise ValueError("Fold assignment does not match the exact original corpus")
        result["foldsSha256"] = fingerprint(args.folds)
        result["folds"] = {}
        for name in sorted(set(folds["assignments"].values())):
            selected = [row for row in rows if folds["assignments"][row["id"]] == name]
            result["folds"][name] = {"entries": len(selected), **metrics(selected)}
    save_json(args.output / "report.json", result)
    print(f"PCM COMPLETE entries={len(rows)} errors={errors} report={args.output / 'report.json'}", flush=True)
    return 0 if result["complete"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
