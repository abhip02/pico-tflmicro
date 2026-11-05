"""Test the actual upstream file layouts and callback-to-model preprocessing."""

import csv
import struct
import tempfile
import unittest
from pathlib import Path

import numpy as np

from data import PROFILES, extract_features, make_windows, read_trials
from pyomyo_input import (MANIFEST_COLUMNS, PYOMYO_COLUMNS, PyomyoWindow,
                          read_manifest, read_pyomyo_dat, write_training_csv)


class PyomyoTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def csv(self, name, columns, rows):
        path = self.root / name
        with path.open("w", newline="") as destination:
            writer = csv.writer(destination)
            writer.writerow(columns)
            writer.writerows(rows)
        return path

    def manifest(self, rows, slices=False):
        columns = MANIFEST_COLUMNS + (("start_row", "stop_row") if slices else ())
        return self.csv("manifest.csv", columns, rows)

    def test_upstream_csv_round_trip_preserves_channel_order(self):
        sample = [-128, 127, -64, 32, -1, 0, 1, 10]
        # Deliberately shuffled columns must still map Channel_1 to ch0, etc.
        self.csv("upstream.csv", PYOMYO_COLUMNS[::-1], [sample[::-1]] * 50)
        manifest = self.manifest([["upstream.csv", "s1", "t1", "FORWARD", "RAW"]])
        trials = read_manifest(manifest)
        self.assertEqual(trials[0].label, 1)
        np.testing.assert_array_equal(trials[0].samples[0], sample)
        converted = self.root / "training.csv"
        write_training_csv(trials, converted)
        restored = read_trials([converted])
        np.testing.assert_array_equal(restored[0].samples, trials[0].samples)
        self.assertEqual(make_windows(restored)[0].shape, (2, 40, 8))
        with self.assertRaises(FileExistsError):
            write_training_csv(trials, converted)

    def test_preprocessed_uint16_is_not_narrowed_to_int8(self):
        sample = [0, 128, 256, 512, 1024, 4096, 32768, 65535]
        self.csv("upstream.csv", PYOMYO_COLUMNS, [sample] * 12)
        manifest = self.manifest([["upstream.csv", "s1", "t1", "STOP", "PREPROCESSED"]])
        trials = read_manifest(manifest, "PREPROCESSED")
        windows, _ = make_windows(trials, "PREPROCESSED")
        self.assertEqual(windows.shape, (2, 10, 8))
        features = extract_features(windows, "PREPROCESSED")
        expected = np.log1p(sample) / np.log(65536)
        np.testing.assert_allclose(features[0], np.tile(expected, 2), rtol=1e-6)
        self.assertEqual(float(features[0, 7]), 1.0)
        converted = self.root / "training.csv"
        write_training_csv(trials, converted)
        np.testing.assert_array_equal(read_trials([converted], "PREPROCESSED")[0].samples[0], sample)
        with self.assertRaisesRegex(ValueError, "mode"):
            read_trials([converted])

    def test_dat_uses_little_endian_uint16_and_requires_known_boundaries(self):
        sample = (1, 256, 511, 1024, 4096, 16384, 32768, 65535)
        binary = self.root / "vals4.dat"
        binary.write_bytes(struct.pack("<8H", *sample) * 15)
        row = ["vals4.dat", "s1", "gesture1", "LEFT", "PREPROCESSED"]
        with self.assertRaisesRegex(ValueError, "boundaries"):
            read_manifest(self.manifest([row]), "PREPROCESSED")
        trials = read_manifest(self.manifest([row + [2, 12]], slices=True), "PREPROCESSED")
        self.assertEqual(trials[0].samples.shape, (10, 8))
        self.assertEqual(trials[0].label, 3)  # Not guessed from vals4 filename.
        np.testing.assert_array_equal(trials[0].samples[0], sample)
        np.testing.assert_array_equal(trials[0].indices, np.arange(2, 12))
        for payload in (b"", b"\x00" * 17):
            binary.write_bytes(payload)
            with self.assertRaises(ValueError):
                read_pyomyo_dat(binary)

    def test_modes_cannot_be_mixed_even_when_numbers_fit_both(self):
        self.csv("sample.csv", PYOMYO_COLUMNS, [[1] * 8] * 50)
        manifest = self.manifest([["sample.csv", "s1", "t1", "STOP", "FILTERED"]])
        with self.assertRaisesRegex(ValueError, "mode"):
            read_manifest(manifest, "RAW")
        trials = read_manifest(manifest, "FILTERED")
        with self.assertRaisesRegex(ValueError, "mix"):
            make_windows(trials, "RAW")

    def test_duplicate_source_slices_cannot_leak_across_sessions(self):
        self.csv("sample.csv", PYOMYO_COLUMNS, [[0] * 8] * 80)
        rows = [["sample.csv", "train", "t1", "STOP", "RAW", 0, 50],
                ["sample.csv", "test", "t2", "STOP", "RAW", 40, 80]]
        with self.assertRaisesRegex(ValueError, "reused"):
            read_manifest(self.manifest(rows, slices=True))

    def test_missing_headers_and_out_of_range_samples_fail(self):
        for columns, sample in ((["ch0"], [1]), (PYOMYO_COLUMNS, [128] * 8)):
            self.csv("sample.csv", columns, [sample] * 40)
            manifest = self.manifest([["sample.csv", "s", "t", "STOP", "RAW"]])
            with self.assertRaises(ValueError):
                read_manifest(manifest)

    def test_callback_matches_offline_windows_in_each_mode(self):
        rng = np.random.default_rng(3)
        for mode, profile in PROFILES.items():
            with self.subTest(mode=mode):
                samples = rng.integers(profile.minimum, profile.maximum + 1,
                                       size=(profile.window_samples + 2 * profile.stride_samples, 8))
                adapter = PyomyoWindow(mode)
                actual = []
                for i, sample in enumerate(samples):
                    feature = adapter(tuple(sample), movement=255)
                    if feature is not None:
                        actual.append(feature)
                        expected = extract_features(samples[i - profile.window_samples + 1:i + 1], mode)
                        np.testing.assert_array_equal(feature, expected)
                self.assertEqual(len(actual), 3)
                adapter.reset()
                self.assertIsNone(adapter(samples[0]))
                with self.assertRaises(ValueError):
                    adapter([profile.maximum + 1] * 8)
                self.assertEqual(adapter.count, 0)


if __name__ == "__main__":
    unittest.main()
