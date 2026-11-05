"""Generate artificial, easily separated signals to check the training pipeline."""

import argparse
import csv
from pathlib import Path

import numpy as np

from data import CHANNELS, CSV_COLUMNS, LABELS, PROFILES, get_profile
from pyomyo_input import MANIFEST_COLUMNS, PYOMYO_COLUMNS


def generate(path, seed=42, mode="RAW", pyomyo=False):
    profile = get_profile(mode)
    rng = np.random.default_rng(seed)
    path.parent.mkdir(parents=True, exist_ok=True)
    # Each invented command activates different channels. These are NOT Myo data.
    amplitudes = np.full((len(LABELS), CHANNELS), 4.0)
    for label in range(1, len(LABELS)):
        amplitudes[label, (label - 1) * 2:label * 2] = 300.0 if mode == "PREPROCESSED" else 45.0
    with path.open("x", newline="") as destination:
        writer = csv.writer(destination)
        writer.writerow(MANIFEST_COLUMNS if pyomyo else CSV_COLUMNS + ("mode",))
        for session in ("synthetic_train", "synthetic_validation", "synthetic_test"):
            for label, name in enumerate(LABELS):
                for trial in range(5):
                    scale = amplitudes[label] * rng.uniform(0.8, 1.2, size=CHANNELS)
                    samples = rng.normal(0, scale, size=(2 * profile.sample_rate_hz, CHANNELS))
                    if mode == "PREPROCESSED":
                        samples = np.abs(samples)
                    samples = np.clip(np.rint(samples), profile.minimum, profile.maximum).astype(profile.dtype)
                    if pyomyo:
                        recording = path.parent / (path.stem + "_recordings") / f"{session}_{name}_{trial}.csv"
                        recording.parent.mkdir(parents=True, exist_ok=True)
                        with recording.open("x", newline="") as file:
                            sample_writer = csv.writer(file)
                            sample_writer.writerow(PYOMYO_COLUMNS)
                            sample_writer.writerows(samples.tolist())
                        writer.writerow([str(recording.relative_to(path.parent)), session,
                                         f"{name}_{trial}", name, mode])
                    else:
                        for index, sample in enumerate(samples):
                            writer.writerow([session, f"{name}_{trial}", index, name] + sample.tolist() + [mode])
    print(f"Wrote synthetic pipeline-check data to {path}; not real EMG training data.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mode", choices=PROFILES, default="RAW")
    parser.add_argument("--pyomyo", action="store_true",
                        help="Write native Channel_1..8 CSVs; --output is the manifest path")
    args = parser.parse_args()
    try:
        generate(args.output, mode=args.mode, pyomyo=args.pyomyo)
    except OSError as error:
        parser.exit(1, f"Error: {error}\n")
