"""Strict reader for REMFEAT1 calibration files (not a product dependency)."""
import hashlib
from pathlib import Path
import struct

import numpy as np


def read_features(path, checksum):
    path = Path(path)
    if not 28 <= path.stat().st_size <= 4 * 1024 * 1024:
        raise ValueError("Calibration file exceeds its bounded format")
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != checksum:
        raise ValueError("Calibration checksum differs from the recorded trace")
    if raw[:8] != b"REMFEAT1":
        raise ValueError("Unsupported calibration identity")
    rate, samples, channels, keys, tempos = struct.unpack_from("<5i", raw, 8)
    expected_keys = 0 if samples < 8192 else 1 + (samples - 8192) // 2048
    expected_tempos = 0 if samples < 5 * 22050 else 1 + (samples - 2048) // 256
    if rate != 22050 or not 0 < samples <= 120 * rate or channels not in (1, 2) or keys != expected_keys or tempos != expected_tempos:
        raise ValueError("Calibration dimensions differ from the canonical timeline")
    chroma = np.empty((keys, 12), dtype=np.float32)
    energy = np.empty(keys, dtype=np.float64)
    counts, peaks = [], []
    cursor = 28
    for index in range(keys):
        if cursor + 60 > len(raw):
            raise ValueError("Truncated calibration frame")
        chroma[index] = np.frombuffer(raw, dtype="<f4", count=12, offset=cursor)
        energy[index], count = struct.unpack_from("<di", raw, cursor + 48)
        cursor += 60
        if not 0 <= count <= channels * 100 or cursor + count * 12 > len(raw):
            raise ValueError("Invalid calibration peak count")
        counts.append(count)
        peaks.append(np.frombuffer(raw, dtype="<f4", count=count * 3, offset=cursor).reshape(count, 3))
        cursor += count * 12
    if cursor + tempos * 24 != len(raw):
        raise ValueError("Missing/trailing calibration bytes")
    tempo = np.frombuffer(raw, dtype="<f8", count=tempos * 3, offset=cursor).reshape(tempos, 3).copy()
    spectral = np.concatenate(peaks) if peaks else np.empty((0, 3), dtype=np.float32)
    frames = np.repeat(np.arange(keys), counts)
    for values in (chroma, energy, spectral, tempo):
        if not np.isfinite(values).all() or (values < 0).any():
            raise ValueError("Non-finite/negative calibration values")
    if spectral.size and ((spectral[:, 0] < 55).any() or (spectral[:, 0] > 5000).any() or (spectral[:, 1:] <= 0).any()):
        raise ValueError("Invalid spectral frequency/magnitude/background")
    return {"rate": rate, "samples": samples, "channels": channels, "chroma": chroma,
            "energy": energy, "peaks": spectral, "peakFrames": frames, "tempo": tempo}
