#!/usr/bin/env python3
"""Discrete power-law MLE with KS-selected xmin (no third-party dependencies).

Reports necessary M1 conditions, not proof of a power law. A positive result
still needs goodness-of-fit bootstrap and competing-distribution comparisons.
Fire CSVs use the absolute forest clock, including warmup.
"""
import argparse
import collections
import csv
import gzip
import json
import math
from pathlib import Path


def zeta(s, q):
    """Hurwitz zeta, Euler–Maclaurin with six correction terms."""
    terms = 32
    x = q + terms
    result = sum((q + k) ** -s for k in range(terms))
    result += x ** (1 - s) / (s - 1) + 0.5 * x ** -s
    rising = s
    # B_(2k)/(2k)! through B12.
    coefficients = (1/12, -1/720, 1/30240, -1/1209600,
                    1/47900160, -691/1307674368000)
    for k, coefficient in enumerate(coefficients, 1):
        if k > 1:
            rising *= (s + 2*k - 3) * (s + 2*k - 2)
        result += coefficient * rising * x ** (-s - 2*k + 1)
    return result


def minimize(function, low=1.00001, high=12.0):
    ratio = (math.sqrt(5) - 1) / 2
    a, b = high - ratio*(high-low), low + ratio*(high-low)
    fa, fb = function(a), function(b)
    for _ in range(70):
        if fa < fb:
            high, b, fb = b, a, fa
            a = high - ratio*(high-low)
            fa = function(a)
        else:
            low, a, fa = a, b, fb
            b = low + ratio*(high-low)
            fb = function(b)
    return (low + high) / 2


def fit(sizes, min_tail=100):
    counts = collections.Counter(sizes)
    values = sorted(counts)
    best = None
    for xmin in values:
        tail = [x for x in values if x >= xmin]
        n = sum(counts[x] for x in tail)
        if n < min_tail:
            break
        sum_log = sum(counts[x]*math.log(x) for x in tail)
        alpha = minimize(lambda a: n*math.log(zeta(a, xmin)) + a*sum_log)
        normalization = zeta(alpha, xmin)
        cumulative, ks = 0, 0.0
        for x in tail:
            ks = max(ks, abs(cumulative/n - (1-zeta(alpha, x)/normalization)))
            cumulative += counts[x]
            ks = max(ks, abs(cumulative/n - (1-zeta(alpha, x+1)/normalization)))
        result = dict(tau=alpha, xmin=xmin, tail_samples=n, ks=ks,
                      decades=math.log10(max(sizes)/xmin))
        if best is None or ks < best['ks']:
            best = result
    return best


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('events', nargs='+', type=Path)
    parser.add_argument('--warmup', type=int, default=3000)
    parser.add_argument('--out', type=Path)
    parser.add_argument('--check', action='store_true', help='check sample sufficiency and finite fit results')
    parser.add_argument('--legacy-exponent-check', action='store_true', help='evaluate the superseded design-v1 exponent hypothesis')
    args = parser.parse_args()
    results = []
    for path in args.events:
        opener = gzip.open if path.suffix == '.gz' else open
        with opener(path, 'rt') as stream:
            sizes = [int(row['size']) for row in csv.DictReader(stream)
                     if int(row['start_tick']) >= args.warmup]
        if any(size <= 0 for size in sizes):
            parser.error(f'{path}: nonpositive fire size')
        fitted = fit(sizes)
        result = dict(file=str(path), samples=len(sizes), maximum=max(sizes, default=0), fit=fitted)
        result['necessary_conditions_pass'] = bool(fitted and
            1.0 <= fitted['tau'] <= 1.4 and fitted['decades'] >= 2)
        results.append(result)
    report = dict(method='discrete MLE; Hurwitz zeta; KS-minimized xmin; at least 100 tail events',
                  results=results,
                  note='The original tau interval is a superseded hypothesis, not a correctness gate. Fit validity does not establish SOC; bootstrap/model comparisons remain required.')
    if len(results) == 2 and all(r['fit'] for r in results):
        report['tau_difference'] = abs(results[0]['fit']['tau']-results[1]['fit']['tau'])
        report['size_stability_pass'] = report['tau_difference'] <= .1
    text = json.dumps(report, indent=2)
    print(text)
    if args.out:
        args.out.write_text(text+'\n')
    if args.check and not all(r['samples']>=100 and r['fit'] and math.isfinite(r['fit']['tau']) and r['fit']['tau']>1 for r in results):
        raise SystemExit(1)
    if args.legacy_exponent_check and (not all(r['necessary_conditions_pass'] for r in results) or
                       not report.get('size_stability_pass', True)):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
