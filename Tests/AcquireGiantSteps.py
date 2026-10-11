"""Acquire the official GiantSteps annotation union for local offline evaluation.

Only downloads data; never executes scripts from annotation archives. Audio is
kept under the requested output directory, outside distributable app/plugin data.
The archives and every audio file are fingerprinted; the authors' MD5 values
must match before an evaluation manifest can be published.
"""
import argparse
import concurrent.futures
import hashlib
import json
import math
import os
from pathlib import Path
import re
import urllib.request
import uuid
import zipfile


BASE = "https://www.cp.jku.at/datasets/giantsteps/backup/"
MAX_AUDIO_BYTES = 16 * 1024 * 1024


def fingerprint(path, algorithm="sha256"):
    result = hashlib.new(algorithm)
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(65536), b""):
            result.update(block)
    return result.hexdigest()


def annotations(path, directory, extension):
    with zipfile.ZipFile(path) as archive:
        result = {}
        for name in archive.namelist():
            if directory not in name or not name.endswith(extension):
                continue
            identifier = name.rsplit("/", 1)[-1][:-len(extension)]
            if not re.fullmatch(r"[0-9]+\.LOFI", identifier):
                raise ValueError(f"Unexpected annotation name: {name}")
            if identifier in result:
                raise ValueError(f"Duplicate annotation: {name}")
            result[identifier] = archive.read(name).decode("utf-8").strip()
        return result


def save_json(path, value):
    temporary = path.with_name(f".corpus-{uuid.uuid4().hex}.tmp")
    try:
        with temporary.open("x", encoding="utf-8", newline="\n") as stream:
            json.dump(value, stream, ensure_ascii=False, indent=2, allow_nan=False)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def acquire(output, row):
    identifier = row["id"]
    target = output / "audio" / f"{identifier}.mp3"
    temporary = target.with_name(f".audio-{uuid.uuid4().hex}.tmp")
    result = dict(row)
    result["url"] = BASE + f"{identifier}.mp3"
    try:
        if target.exists():
            if fingerprint(target, "md5") != row["md5"]:
                raise ValueError("Existing audio differs from published MD5; it was preserved")
            result["reused"] = True
        else:
            with urllib.request.urlopen(result["url"], timeout=30) as response:
                length = response.headers.get("Content-Length")
                if response.status != 200 or (length and int(length) > MAX_AUDIO_BYTES):
                    raise ValueError("Unexpected HTTP status or oversized audio")
                total = 0
                with temporary.open("xb") as stream:
                    for block in iter(lambda: response.read(65536), b""):
                        total += len(block)
                        if total > MAX_AUDIO_BYTES:
                            raise ValueError("Audio exceeds the bounded download size")
                        stream.write(block)
                    stream.flush()
                    os.fsync(stream.fileno())
            if fingerprint(temporary, "md5") != row["md5"]:
                raise ValueError("Downloaded audio differs from published MD5")
            # A different writer appearing at this unique dataset ID is not
            # silently replaced. Existing wrong audio always needs review.
            if target.exists():
                raise ValueError("Audio target appeared during download; review it before retry")
            temporary.rename(target)
            result["reused"] = False
        result["sha256"] = fingerprint(target)
        result["bytes"] = target.stat().st_size
        result["file"] = target.relative_to(output).as_posix()
    except Exception as error:
        result["error"] = str(error)
    finally:
        if temporary.exists():
            temporary.unlink()
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("out/corpus-giantsteps"))
    parser.add_argument("--key-archive", type=Path)
    parser.add_argument("--tempo-archive", type=Path)
    parser.add_argument("--only", nargs="+", help="Explicit preview IDs for a clearly marked pilot run")
    parser.add_argument("--prepare-only", action="store_true", help="Save the full reference plan without downloading audio")
    parser.add_argument("--manifest", default="manifest.json", help="Output manifest basename")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if Path(args.manifest).name != args.manifest or args.manifest in ("", ".", ".."):
        raise ValueError("Manifest must be a basename in the corpus output directory")
    key_archive = args.key_archive or output / "key-source.zip"
    tempo_archive = args.tempo_archive or output / "tempo-source.zip"
    key_labels = annotations(key_archive, "/annotations/key/", ".key")
    tempo_labels = annotations(tempo_archive, "/annotations_v2/tempo/", ".bpm")
    old_tempos = annotations(tempo_archive, "/annotations/tempo/", ".bpm")
    key_md5 = annotations(key_archive, "/md5/", ".md5")
    tempo_md5 = annotations(tempo_archive, "/md5/", ".md5")
    if len(key_labels) != 604 or len(tempo_labels) != 664:
        raise ValueError("Unexpected published corpus counts; review this annotation revision")
    rows = []
    for identifier in sorted(set(key_labels) | set(tempo_labels)):
        md5 = key_md5.get(identifier, tempo_md5.get(identifier, "")).lower()
        if not re.fullmatch(r"[0-9a-f]{32}", md5):
            raise ValueError(f"Missing or invalid published MD5: {identifier}")
        if identifier in key_md5 and identifier in tempo_md5 and key_md5[identifier] != tempo_md5[identifier]:
            raise ValueError(f"Conflicting published MD5: {identifier}")
        row = {"id": identifier, "md5": md5}
        if identifier in key_labels:
            row["referenceKey"] = key_labels[identifier]
        if identifier in tempo_labels:
            bpm = float(tempo_labels[identifier])
            if not math.isfinite(bpm) or not 0 <= bpm <= 400:
                raise ValueError(f"Invalid tempo annotation: {identifier}")
            row["referenceBpm"] = bpm
            row["originalTempoAnnotation"] = float(old_tempos[identifier])
        rows.append(row)
    provenance = {
        "schemaVersion": 1,
        "dataset": "GiantSteps key + tempo v2 annotation union",
        "source": "https://www.cp.jku.at/datasets/giantsteps/",
        "audioSource": BASE,
        "keyAnnotationArchiveSha256": fingerprint(key_archive),
        "tempoAnnotationArchiveSha256": fingerprint(tempo_archive),
        "keyAnnotationSource": "https://github.com/GiantSteps/giantsteps-key-dataset",
        "tempoAnnotationSource": "https://github.com/GiantSteps/giantsteps-tempo-dataset",
        "citation": "Knees et al., ISMIR 2015; Schreiber and Mueller, ISMIR 2018 (tempo v2)",
        "fullUnionEntries": len(rows), "keyReferences": len(key_labels), "tempoReferences": len(tempo_labels),
        "selection": "All published IDs; no selection by detector predictions",
        "entries": rows,
    }
    save_json(output / "reference-plan.json", provenance)
    print(f"PLAN union={len(rows)} keys={len(key_labels)} tempoV2={len(tempo_labels)}", flush=True)
    if args.prepare_only:
        return 0
    if args.only:
        selected = {identifier if identifier.endswith(".LOFI") else identifier + ".LOFI" for identifier in args.only}
        available = {row["id"] for row in rows}
        if not selected <= available:
            raise ValueError("Requested pilot ID is absent from the published annotations")
        rows = [row for row in rows if row["id"] in selected]
        provenance["selection"] = "Explicit pilot IDs, not the complete published corpus"
    (output / "audio").mkdir(exist_ok=True)
    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
        for result in executor.map(lambda row: acquire(output, row), rows):
            results.append(result)
            if len(results) % 25 == 0 or len(results) == len(rows):
                print(f"ACQUIRE completed={len(results)}/{len(rows)} errors={sum('error' in r for r in results)}", flush=True)
    acquisition = dict(provenance)
    acquisition["entries"] = results
    acquisition["complete"] = all("error" not in row for row in results)
    acquisition["partialCorpus"] = len(results) != provenance["fullUnionEntries"]
    save_json(output / (args.manifest + ".acquisition.json"), acquisition)
    if not acquisition["complete"]:
        print("INCOMPLETE: acquisition report saved; no evaluation manifest was published", flush=True)
        return 1
    manifest = dict(acquisition)
    manifest["entries"] = [{field: row[field] for field in ("id", "file", "sha256", "referenceBpm", "referenceKey") if field in row}
                           for row in results]
    save_json(output / args.manifest, manifest)
    print(f"READY manifest={output / args.manifest} bytes={sum(row['bytes'] for row in results)}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
