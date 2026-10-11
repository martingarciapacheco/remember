"""Freeze development/holdout assignments before inspecting predictions.

GiantSteps publishes MD5 identities for its audio. Identical audio stays in
one fold; the assignment uses SHA256(MD5), first 32-bit word modulo two.
Previously inspected pilot IDs must be explicitly assigned to development.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import uuid


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--acquisition", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--development-id", action="append", default=[], help="Inspected pilot ID; repeat if needed")
    args = parser.parse_args()
    manifest = read_json(args.manifest)
    acquisition = read_json(args.acquisition)
    if not acquisition.get("complete") or acquisition.get("partialCorpus"):
        raise ValueError("Freeze folds only for a complete acquired corpus")
    entries = manifest["entries"]
    originals = {row["id"]: row for row in acquisition["entries"]}
    identifiers = {row["id"] for row in entries}
    if (not entries or len(identifiers) != len(entries) or len(originals) != len(acquisition["entries"])
            or identifiers != set(originals)):
        raise ValueError("Manifest/acquisition identities are incomplete or duplicated")
    development = set(args.development_id)
    if not development <= identifiers:
        raise ValueError("A development pilot is absent from the acquired corpus")
    identities = {}
    for row in entries:
        original = originals[row["id"]]
        md5 = original["md5"].lower()
        if (not re.fullmatch(r"[0-9a-f]{32}", md5) or original["sha256"].lower() != row["sha256"].lower()
                or original["file"] != row["file"]):
            raise ValueError("Published audio identity does not match the pinned manifest")
        identities[row["id"]] = md5
    forced = {identities[identifier] for identifier in development}
    assignments = {}
    for identifier in sorted(identifiers):
        digest = hashlib.sha256(identities[identifier].encode("ascii")).hexdigest()
        assignments[identifier] = "development" if identities[identifier] in forced or int(digest[:8], 16) % 2 == 0 else "holdout"
    protected = {args.manifest.resolve(), args.acquisition.resolve()}
    protected |= {(args.manifest.parent / row["file"]).resolve() for row in entries}
    if args.output.resolve() in protected:
        raise ValueError("Fold output cannot replace a corpus input")
    result = {"manifestSha256": hashlib.sha256(args.manifest.read_bytes()).hexdigest(),
              "policy": "Split by SHA256(published audio MD5), first 32-bit word modulo two; identical audio stays together. "
                        "Explicitly inspected pilots and identical audio are development. Freeze before inspecting other predictions.",
              "developmentIds": sorted(development), "assignments": assignments}
    # A saved split is immutable to this command. Re-running the same command
    # is harmless, but changed pilots/manifest require another output path.
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.output.exists():
        if read_json(args.output) != result:
            raise ValueError("An existing frozen split differs; use another output path")
    else:
        temporary = args.output.with_name(f".folds-{uuid.uuid4().hex}.tmp")
        try:
            with temporary.open("x", encoding="utf-8", newline="\n") as stream:
                json.dump(result, stream, ensure_ascii=False, indent=2, allow_nan=False)
                stream.write("\n"); stream.flush(); os.fsync(stream.fileno())
            # Publish without overwriting a split created concurrently.
            if os.name == "nt":
                os.rename(temporary, args.output)  # Windows rename never replaces an existing target.
            else:
                os.link(temporary, args.output)
        finally:
            if temporary.exists():
                temporary.unlink()
    print(f"FROZEN development={sum(name == 'development' for name in assignments.values())} "
          f"holdout={sum(name == 'holdout' for name in assignments.values())} output={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
