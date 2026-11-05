"""Adapters for the upstream pyomyo CSV, binary records and EMG callback.

Formats inspected at PerlinWarp/pyomyo commit
d3e6720fe0c3996d37b48bc195d977052d1a59f9:
examples/data_collector.py, src/pyomyo/Classifier.py, src/pyomyo/pyomyo.py.
No pyomyo, pandas or TensorFlow dependency is needed to import recordings.
"""

import argparse
import csv
from collections import deque
from pathlib import Path

import numpy as np

from data import (CHANNELS, CSV_COLUMNS, LABELS, PROFILES, Trial, extract_features,
                  get_profile, validate_samples)

PYOMYO_COLUMNS = tuple(f"Channel_{i + 1}" for i in range(CHANNELS))
MANIFEST_COLUMNS = ("file", "session_id", "trial_id", "label", "mode")


def read_pyomyo_csv(path, mode):
    """Read the eight columns written by pyomyo/examples/data_collector.py."""
    with path.open(newline="") as source:
        reader = csv.DictReader(source)
        missing = set(PYOMYO_COLUMNS) - set(reader.fieldnames or ())
        if missing:
            raise ValueError(f"{path}: missing pyomyo columns {sorted(missing)}")
        rows = []
        for line, row in enumerate(reader, 2):
            try:
                rows.append([int(row[column]) for column in PYOMYO_COLUMNS])
            except (ValueError, TypeError) as error:
                raise ValueError(f"{path}:{line}: expected eight integer EMG readings") from error
    if not rows:
        raise ValueError(f"{path}: no EMG samples")
    return validate_samples(rows, mode)


def read_pyomyo_dat(path):
    """Classifier.store_data uses struct.pack('<8H'): 16 bytes per sample."""
    size = path.stat().st_size
    if size == 0 or size % 16:
        raise ValueError(f"{path}: expected nonempty records of eight little-endian uint16 values")
    return np.fromfile(path, dtype="<u2").reshape(-1, CHANNELS)


def read_manifest(path, mode="RAW"):
    """Attach labels, sessions and known trial boundaries to upstream files.

    A CSV is treated as one uninterrupted held gesture unless start_row and
    stop_row are provided. Binary class files lose repetition boundaries, so
    both offsets are REQUIRED for .dat files. Offsets are [start, stop).
    """
    get_profile(mode)
    path = Path(path)
    trials, used_keys, used_ranges = [], set(), {}
    with path.open(newline="") as source:
        reader = csv.DictReader(source)
        missing = set(MANIFEST_COLUMNS) - set(reader.fieldnames or ())
        if missing:
            raise ValueError(f"{path}: missing manifest columns {sorted(missing)}")
        for line, row in enumerate(reader, 2):
            try:
                session, trial = row["session_id"].strip(), row["trial_id"].strip()
                filename = row["file"].strip()
                if not session or not trial or not filename:
                    raise ValueError("file, session_id and trial_id must be nonempty")
                if row["mode"].strip() != mode:
                    raise ValueError(f"manifest mode {row['mode']!r} does not match {mode}")
                label = LABELS.index(row["label"].strip())
                key = (session, trial)
                if key in used_keys:
                    raise ValueError(f"duplicate trial {key}")
                recording = (path.parent / filename).resolve()
                start_text = (row.get("start_row") or "").strip()
                stop_text = (row.get("stop_row") or "").strip()
                if bool(start_text) != bool(stop_text):
                    raise ValueError("provide both start_row and stop_row, or neither")
                if recording.suffix.lower() == ".dat":
                    if mode != "PREPROCESSED":
                        raise ValueError("pyomyo vals*.dat stores unsigned PREPROCESSED readings")
                    if not start_text or not stop_text:
                        raise ValueError(".dat loses trial boundaries: provide known start_row "
                                         "and stop_row for a single continuous repetition")
                    samples = read_pyomyo_dat(recording)
                elif recording.suffix.lower() == ".csv":
                    samples = read_pyomyo_csv(recording, mode)
                else:
                    raise ValueError("expected pyomyo .csv or .dat file")
                start = int(start_text) if start_text else 0
                stop = int(stop_text) if stop_text else len(samples)
                if not 0 <= start < stop <= len(samples):
                    raise ValueError(f"invalid row slice [{start}, {stop}) for {len(samples)} samples")
                for old_start, old_stop in used_ranges.get(recording, []):
                    if start < old_stop and old_start < stop:
                        raise ValueError("same source rows reused: would duplicate data across trials/sessions")
                used_ranges.setdefault(recording, []).append((start, stop))
                used_keys.add(key)
                trials.append(Trial(session, trial, label, np.arange(start, stop),
                                    samples[start:stop], mode))
            except (ValueError, TypeError, AttributeError, OSError) as error:
                raise ValueError(f"{path}:{line}: {error}") from error
    if not trials:
        raise ValueError(f"{path}: no recordings in manifest")
    return trials


def write_training_csv(trials, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", newline="") as destination:
        writer = csv.writer(destination)
        writer.writerow(CSV_COLUMNS + ("mode",))
        for trial in trials:
            for index, sample in zip(trial.indices, trial.samples):
                writer.writerow([trial.session_id, trial.trial_id, int(index), LABELS[trial.label]]
                                + sample.tolist() + [trial.mode])


class PyomyoWindow:
    """Process one add_emg_handler(emg, movement) callback at a time.

    Returns a 16-value feature vector when a window is ready, otherwise None.
    movement is protocol metadata, NOT the desired robot action or a label.
    Call reset() after any interruption. This class does not detect packet loss.
    """

    def __init__(self, mode="RAW"):
        self.profile = get_profile(mode)
        self.reset()

    def reset(self):
        self.samples = deque(maxlen=self.profile.window_samples)
        self.count = 0

    def __call__(self, emg, movement=None):
        try:
            sample = validate_samples(np.asarray(emg)[None, :], self.profile.mode)[0]
        except (ValueError, TypeError, IndexError):
            self.reset()
            raise
        self.samples.append(sample)
        self.count += 1
        if (self.count >= self.profile.window_samples and
                (self.count - self.profile.window_samples) % self.profile.stride_samples == 0):
            return extract_features(np.stack(self.samples), self.profile.mode)
        return None


def main():
    parser = argparse.ArgumentParser(description="Import pyomyo recordings into labeled training CSV")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--mode", choices=PROFILES, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        trials = read_manifest(args.manifest, args.mode)
        write_training_csv(trials, args.output)
    except (ValueError, OSError) as error:
        parser.exit(1, f"Error: {error}\n")
    print(f"Imported {sum(len(t.samples) for t in trials)} {args.mode} samples "
          f"in {len(trials)} trials into {args.output}")


if __name__ == "__main__":
    main()
