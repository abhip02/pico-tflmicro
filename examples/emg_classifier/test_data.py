"""Checks for preprocessing, malformed recordings and split leakage (no TensorFlow)."""

import csv
import tempfile
import unittest
from pathlib import Path

import numpy as np

from data import (CSV_COLUMNS, Trial, extract_features, make_windows,
                  quantize_features, read_trials, split_trials)


class DataTests(unittest.TestCase):
    def trial(self, count=50, session="train", name="trial", label=0, indices=None):
        return Trial(session, name, label,
                     np.arange(count) if indices is None else np.asarray(indices),
                     np.zeros((count, 8), dtype=np.int8))

    def write_csv(self, path, rows):
        with path.open("w", newline="") as destination:
            writer = csv.writer(destination)
            writer.writerow(CSV_COLUMNS)
            writer.writerows(rows)

    def test_feature_order_and_negative_int8_endpoint(self):
        window = np.tile(np.array([-128, 127, -64, 32, 0, 1, -1, 16], dtype=np.int8), (40, 1))
        expected = np.array([128, 127, 64, 32, 0, 1, 1, 16]) / 128
        np.testing.assert_allclose(extract_features(window), np.tile(expected, 2))

    def test_mav_and_rms_are_distinct(self):
        window = np.zeros((40, 8), dtype=np.int8)
        window[::2] = -64
        features = extract_features(window)
        np.testing.assert_allclose(features[:8], 0.25)
        np.testing.assert_allclose(features[8:], np.sqrt(0.125), rtol=1e-6)

    def test_windows_never_cross_trials_or_sample_gaps(self):
        with self.assertRaisesRegex(ValueError, "No complete windows"):
            make_windows([self.trial(30), self.trial(30, name="other")])
        with self.assertRaisesRegex(ValueError, "No complete windows"):
            make_windows([self.trial(60, indices=list(range(30)) + list(range(31, 61)))])
        windows, _ = make_windows([self.trial(50)])
        self.assertEqual(windows.shape, (2, 40, 8))

    def test_session_split_precedes_windowing(self):
        trials = [self.trial(session=name) for name in ("train", "validation", "test")]
        parts = split_trials(trials, "validation", "test")
        for name, part in parts.items():
            self.assertEqual({t.session_id for t in part}, {name})
        with self.assertRaises(ValueError):
            split_trials(trials, "test", "test")
        with self.assertRaises(ValueError):
            split_trials(trials, "missing", "test")
        with self.assertRaises(ValueError):
            split_trials(trials[1:], "validation", "test")

    def test_csv_validation(self):
        good_row = ["session", "trial", 0, "STOP"] + [0] * 8
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "samples.csv"
            self.write_csv(path, [good_row])
            self.assertEqual(len(read_trials([path])), 1)
            for column, bad_value in ((0, ""), (2, -1), (3, "UNKNOWN"),
                                      (4, 128), (4, "nan"), (4, 1.5)):
                bad_row = good_row.copy()
                bad_row[column] = bad_value
                self.write_csv(path, [bad_row])
                with self.subTest(column=column, value=bad_value):
                    with self.assertRaises(ValueError):
                        read_trials([path])
            self.write_csv(path, [good_row, good_row])
            with self.assertRaisesRegex(ValueError, "must increase"):
                read_trials([path])
            changed_label = ["session", "trial", 1, "LEFT"] + [0] * 8
            self.write_csv(path, [good_row, changed_label])
            with self.assertRaisesRegex(ValueError, "one label"):
                read_trials([path])
            path.write_text("label,ch0\nSTOP,0\n")
            with self.assertRaisesRegex(ValueError, "missing columns"):
                read_trials([path])

    def test_quantization_rounds_and_saturates(self):
        result = quantize_features(np.array([0, 0.005, 0.01, 1.0], dtype=np.float32), 0.01, -128)
        np.testing.assert_array_equal(result, [-128, -127, -127, -28])
        np.testing.assert_array_equal(quantize_features(np.array([0.0, 1.0]), 0.001, -128), [-128, 127])
        for scale in (0, -1, np.nan):
            with self.assertRaises(ValueError):
                quantize_features(np.zeros(16), scale, 0)


if __name__ == "__main__":
    unittest.main()
