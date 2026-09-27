"""Linux integration tests. No PMU availability or fabricated counts required."""
import re
import subprocess
import sys
import unittest

BINARY = sys.argv.pop(1) if len(sys.argv) > 1 else "./build/linkbench"


def run(*args):
    return subprocess.run(
        [BINARY, "--lists", "2", "--nodes", "17", "--queries", "20",
         "--repeat", "1", *args], text=True, capture_output=True, timeout=30
    )


class CommandLineTests(unittest.TestCase):
    def test_all_modes_patterns_and_extreme_sizes(self):
        for mode in ("head", "tail", "hit", "miss", "mixed"):
            for size in ("32", "512"):
                for pattern in ("varied", "shared-prefix"):
                    with self.subTest(mode=mode, size=size, pattern=pattern):
                        result = run("--mode", mode, "--size", size, "--pattern", pattern)
                        self.assertEqual(result.returncode, 0, result.stderr)
                        checksums = re.findall(r"checksum=(\d+) verified=yes", result.stdout)
                        self.assertEqual(len(checksums), 2)
                        self.assertEqual(checksums[0], checksums[1])
                        expected_hits = 0 if mode == "miss" else 10 if mode == "mixed" else 20
                        self.assertIn(f"expected_hits={expected_hits}", result.stdout)
                        if mode == "miss":
                            self.assertEqual(checksums[0], "0")
                        for key in ("instructions:", "cycles:", "memory_accesses_L1D",
                                    "L1D_load_hit_rate_pct:"):
                            self.assertIn(key, result.stdout)
                        for rate in re.findall(r"L1D_load_hit_rate_pct: ([\d.]+)", result.stdout):
                            self.assertGreaterEqual(float(rate), 0)
                            self.assertLessEqual(float(rate), 100)

    def test_strict_hardware_status(self):
        result = run("--require-perf")
        self.assertIn(result.returncode, (0, 2), result.stderr)
        incomplete = "hardware_metrics: incomplete" in result.stdout
        self.assertEqual(result.returncode, 2 if incomplete else 0)

    def test_invalid_arguments(self):
        cases = [
            ("--size", "0"), ("--size", "31"), ("--size", "513"),
            ("--nodes", "-1"), ("--queries", "0"), ("--lists", "abc"),
            ("--nodes", "18446744073709551616"),
            ("--lists", "18446744073709551615"),
            ("--nodes", " 12"), ("--seed", "0"),
            ("--cpu", "-1"), ("--cpu", "999999"),
            ("--pattern", "hash"), ("--mode", "unknown"),
            ("--algorithm", "unknown"), ("--unknown", "1"), ("--size",),
        ]
        for args in cases:
            with self.subTest(args=args):
                self.assertEqual(run(*args).returncode, 1)

    def test_one_node_and_one_algorithm(self):
        for algorithm in ("baseline", "optimized"):
            result = run("--lists", "1", "--nodes", "1", "--algorithm", algorithm)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.count("verified=yes"), 1)

    def test_help(self):
        self.assertEqual(run("--help").returncode, 0)


if __name__ == "__main__":
    unittest.main()
