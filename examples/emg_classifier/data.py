"""Data contract and preprocessing shared by training and offline evaluation."""

import csv
from dataclasses import dataclass
from pathlib import Path

import numpy as np

LABELS = ("STOP", "FORWARD", "REVERSE", "LEFT", "RIGHT")
CHANNELS = 8
SAMPLE_RATE_HZ = 200
WINDOW_SAMPLES = 40
STRIDE_SAMPLES = 10
FEATURE_COUNT = 2 * CHANNELS
CSV_COLUMNS = ("session_id", "trial_id", "sample_index", "label") + tuple(
    f"ch{i}" for i in range(CHANNELS)
)


@dataclass(frozen=True)
class EmgProfile:
    mode: str
    sample_rate_hz: int
    window_samples: int
    stride_samples: int
    minimum: int
    maximum: int
    dtype: str


PROFILES = {
    "RAW": EmgProfile("RAW", 200, 40, 10, -128, 127, "int8"),
    "FILTERED": EmgProfile("FILTERED", 200, 40, 10, -128, 127, "int8"),
    "PREPROCESSED": EmgProfile("PREPROCESSED", 50, 10, 2, 0, 65535, "uint16"),
}


def get_profile(mode):
    try:
        return PROFILES[mode]
    except KeyError as error:
        raise ValueError(f"Unknown EMG mode {mode!r}; choose {tuple(PROFILES)}") from error


def validate_samples(samples, mode="RAW"):
    """Validate before casting, so unsigned/float input cannot wrap silently."""
    profile = get_profile(mode)
    values = np.asarray(samples)
    if values.ndim != 2 or values.shape[1] != CHANNELS:
        raise ValueError("Expected an N by 8 array of EMG readings")
    if values.dtype.kind not in "iuf" or not np.all(np.isfinite(values)):
        raise ValueError("EMG readings must be finite numbers")
    if (np.any(values < profile.minimum) or np.any(values > profile.maximum) or
            np.any(values != np.trunc(values))):
        raise ValueError(f"{mode} requires integer readings in "
                         f"[{profile.minimum}, {profile.maximum}]")
    return values.astype(profile.dtype)


@dataclass
class Trial:
    session_id: str
    trial_id: str
    label: int
    indices: np.ndarray
    samples: np.ndarray
    mode: str = "RAW"


def read_trials(paths, mode="RAW"):
    """Read ordered samples; a trial contains one held gesture and EMG mode."""
    profile = get_profile(mode)
    groups = {}
    for path in paths:
        with Path(path).open(newline="") as source:
            reader = csv.DictReader(source)
            missing = set(CSV_COLUMNS) - set(reader.fieldnames or ())
            if missing:
                raise ValueError(f"{path}: missing columns: {sorted(missing)}")
            for line, row in enumerate(reader, start=2):
                try:
                    session = row["session_id"].strip()
                    trial = row["trial_id"].strip()
                    if not session or not trial:
                        raise ValueError("session_id and trial_id must be nonempty")
                    label = LABELS.index(row["label"].strip())
                    if "mode" in row and row["mode"] != mode:
                        raise ValueError(f"row mode {row['mode']!r} does not match {mode}")
                    index = int(row["sample_index"])
                    values = [int(row[f"ch{i}"]) for i in range(CHANNELS)]
                    if index < 0 or any(v < profile.minimum or v > profile.maximum for v in values):
                        raise ValueError(f"expected nonnegative index and {mode} readings in "
                                         f"[{profile.minimum}, {profile.maximum}]")
                    key = (session, trial)
                    previous = groups.get(key)
                    if previous is None:
                        groups[key] = (label, [index], [values])
                    else:
                        old_label, indices, samples = previous
                        if label != old_label:
                            raise ValueError("a trial must have one label; start a new trial")
                        if index <= indices[-1]:
                            raise ValueError("sample_index must increase within each trial")
                        indices.append(index)
                        samples.append(values)
                except (ValueError, TypeError, AttributeError) as error:
                    raise ValueError(f"{path}:{line}: {error}; labels are {LABELS}") from error
    if not groups:
        raise ValueError("No EMG samples found")
    return [
        Trial(session, trial, label, np.asarray(indices, dtype=np.int64),
              np.asarray(samples, dtype=profile.dtype), mode)
        for (session, trial), (label, indices, samples) in groups.items()
    ]


def split_trials(trials, validation_session, test_session):
    """Hold out entire sessions BEFORE generating overlapping windows."""
    sessions = {trial.session_id for trial in trials}
    if validation_session == test_session:
        raise ValueError("Validation and test sessions must be different")
    for session in (validation_session, test_session):
        if session not in sessions:
            raise ValueError(f"Session {session!r} not found; available: {sorted(sessions)}")
    parts = {"train": [], "validation": [], "test": []}
    for trial in trials:
        split = ("validation" if trial.session_id == validation_session else
                 "test" if trial.session_id == test_session else "train")
        parts[split].append(trial)
    if not parts["train"]:
        raise ValueError("Need at least one training session plus validation and test sessions")
    return parts


def make_windows(trials, mode="RAW"):
    """Never cross trial boundaries or gaps in sample_index."""
    profile = get_profile(mode)
    windows, labels = [], []
    for trial in trials:
        if trial.mode != mode:
            raise ValueError(f"Cannot mix {trial.mode} recordings with a {mode} model")
        boundaries = np.flatnonzero(np.diff(trial.indices) != 1) + 1
        for segment in np.split(trial.samples, boundaries):
            for start in range(0, len(segment) - profile.window_samples + 1, profile.stride_samples):
                windows.append(segment[start:start + profile.window_samples])
                labels.append(trial.label)
    if not windows:
        raise ValueError(f"No complete windows: need {profile.window_samples} consecutive "
                         "samples per held gesture")
    return np.stack(windows), np.asarray(labels, dtype=np.int64)


def extract_features(windows, mode="RAW"):
    """MAV ch0..7, then RMS ch0..7; mode-specific scaling to [0, 1]."""
    profile = get_profile(mode)
    values = np.asarray(windows)
    if values.ndim < 2 or values.shape[-2:] != (profile.window_samples, CHANNELS):
        raise ValueError(f"Expected shape (..., {profile.window_samples}, 8)")
    validate_samples(values.reshape(-1, CHANNELS), mode)
    # Double precision avoids uint16 square overflow and preserves sums for
    # PREPROCESSED. Convert the final model input to float32.
    values = values.astype(np.float64)
    # Cast before abs/square so -128 and squared int8 values cannot overflow.
    mav = np.mean(np.abs(values), axis=-2)
    rms = np.sqrt(np.mean(values * values, axis=-2))
    features = np.concatenate((mav, rms), axis=-1)
    if mode == "PREPROCESSED":
        # The uint16 container has a much wider range than typical readings.
        # Log scaling retains low-amplitude detail without guessing a cutoff.
        features = np.log1p(features) / np.log(65536.0)
    else:
        features /= 128.0
    return features.astype(np.float32)


def quantize_features(features, scale, zero_point):
    if not np.isfinite(scale) or scale <= 0:
        raise ValueError("Quantization scale must be finite and positive")
    # Features are nonnegative. Match floor(x + 0.5) in emg_features.h.
    quantized = np.floor(features / np.float32(scale) + np.float32(0.5)) + zero_point
    return np.clip(quantized, -128, 127).astype(np.int8)
