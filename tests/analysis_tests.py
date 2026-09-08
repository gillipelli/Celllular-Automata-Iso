import importlib.util
import math
from pathlib import Path
import random
import unittest

spec = importlib.util.spec_from_file_location('forest', Path(__file__).resolve().parents[1]/'python/analysis/forest.py')
forest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(forest)


class AnalysisTests(unittest.TestCase):
    def test_zeta_reference(self):
        self.assertAlmostEqual(forest.zeta(2., 1.), math.pi**2/6, places=13)
        self.assertAlmostEqual(forest.zeta(4., 1.), math.pi**4/90, places=13)
        for s in (1.1, 1.6, 2.5, 5.0):
            self.assertAlmostEqual(forest.zeta(s, 10)-forest.zeta(s, 11), 10**-s, places=13)

    def test_empty_data(self):
        self.assertIsNone(forest.fit([]))

    def test_recovers_synthetic_tail(self):
        rng = random.Random(73)
        sizes = [int(9.5*rng.paretovariate(1.2)+.5) for _ in range(3000)]
        fit = forest.fit(sizes, min_tail=1000)
        self.assertIsNotNone(fit)
        self.assertLess(abs(fit['tau']-2.2), .12)


if __name__ == '__main__':
    unittest.main()
