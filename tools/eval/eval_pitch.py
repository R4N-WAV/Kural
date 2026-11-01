#!/usr/bin/env python3
"""Measures Kural's pitch detector on the real-vocal test set (branch `test-vocals`).

usage: eval_pitch.py <test-vocals dir> <KuralLab binary> <work dir> [report.md]

For tracks with a reference f0 (vocadito annotations, Dagstuhl ChoirSet manual larynx-mic f0):
  voicing recall      share of reference-voiced frames Kural calls voiced
  false alarm         share of reference-unvoiced frames Kural calls voiced
  RPA (50 c)          raw pitch accuracy: reference-voiced frames where Kural's f0 is within 50 cents
  octave errors       reference-voiced frames where Kural is within 50 cents of ±1 octave
  median error        cents, on frames within 50 cents
For tracks without a reference (VocalSet): voiced share and isolated octave outliers (a voiced frame more
than 6 semitones from the median of its ±50 ms voiced neighbours), per technique.

Reference annotations are themselves estimates (manually corrected), so tiny errors are not meaningful;
RPA and octave errors are the measures to watch.
"""
import csv, os, subprocess, sys
from collections import defaultdict
import numpy as np


def load_frames(path):
    d = np.genfromtxt(path, delimiter=',', names=True)
    return d['time_s'], d['f0_hz'], d['voicing'].astype(int), d['confidence']


def load_ref(path):
    d = np.genfromtxt(path, delimiter=',', names=True)
    return d['time_s'], d['f0_hz']


def cents(a, b):
    return 1200.0 * np.log2(a / b)


def to_wav(flac, wav):
    if not os.path.exists(wav):
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', flac, '-ac', '1', '-c:a', 'pcm_f32le', wav], check=True)


def score_ref(t, f0, voi, rt, rf):
    # reference value at each Kural frame time (nearest reference frame within 10 ms)
    idx = np.clip(np.searchsorted(rt, t), 1, len(rt) - 1)
    left = rt[idx - 1]; right = rt[idx]
    near = np.where(np.abs(t - left) <= np.abs(right - t), idx - 1, idx)
    ok = np.abs(rt[near] - t) <= 0.010
    ref = np.where(ok, rf[near], np.nan)
    valid = ~np.isnan(ref)
    rv = valid & (ref > 0)
    ru = valid & (ref <= 0)
    kv = voi == 3
    has = f0 > 0
    with np.errstate(divide='ignore', invalid='ignore'):
        c = np.where(rv & has, cents(np.where(has, f0, 1), np.where(rv, ref, 1)), np.nan)
    good = rv & has & (np.abs(c) <= 50)
    octv = rv & has & (np.abs(np.abs(c) - 1200) <= 50)
    return dict(ref_voiced=int(rv.sum()), ref_unvoiced=int(ru.sum()),
                recall=float((rv & kv).sum()), fa=float((ru & kv).sum()),
                rpa=float(good.sum()), octave=float(octv.sum()),
                abs_err=list(np.abs(c[good])))


def outliers(t, f0, voi):
    v = (voi == 3) & (f0 > 0)
    tv, mv = t[v], 69 + 12 * np.log2(f0[v] / 440.0)
    n_out = 0
    for i in range(len(tv)):
        nb = (np.abs(tv - tv[i]) <= 0.05)
        if nb.sum() >= 5 and abs(mv[i] - np.median(mv[nb])) > 6:
            n_out += 1
    return int(v.sum()), n_out


def main():
    root, lab, work = sys.argv[1], sys.argv[2], sys.argv[3]
    report = sys.argv[4] if len(sys.argv) > 4 else None
    os.makedirs(os.path.join(work, 'wav'), exist_ok=True)
    os.makedirs(os.path.join(work, 'frames'), exist_ok=True)
    rows = list(csv.DictReader(open(os.path.join(root, 'manifest.csv'))))
    agg = defaultdict(lambda: defaultdict(float))
    errs = defaultdict(list)
    per_track = []
    nores = defaultdict(lambda: [0, 0, 0])   # technique -> voiced frames, outliers, all frames
    for r in rows:
        wav = os.path.join(work, 'wav', r['id'] + '.wav')
        to_wav(os.path.join(root, 'audio', r['id'] + '.flac'), wav)
        fr = os.path.join(work, 'frames', r['id'] + '.csv')
        subprocess.run([lab, 'analyse', wav, fr], check=True)
        t, f0, voi, conf = load_frames(fr)
        if r['f0_ref']:
            rt, rf = load_ref(os.path.join(root, r['f0_ref']))
            s = score_ref(t, f0, voi, rt, rf)
            group = r['dataset'] + ('/' + r['voice'] if r['voice'] else '')
            for g in sorted({r['dataset'], group, 'all'}):
                for k in ('ref_voiced', 'ref_unvoiced', 'recall', 'fa', 'rpa', 'octave'):
                    agg[g][k] += s[k]
                agg[g]['tracks'] += 1
                errs[g].extend(s['abs_err'])
            per_track.append((r['id'], s))
        else:
            nv, no = outliers(t, f0, voi)
            for key in (r['technique'], r['voice'] + ' ' + r['technique'], 'all VocalSet'):
                nores[key][0] += nv; nores[key][1] += no; nores[key][2] += len(t)

    lines = ['# Kural pitch detector on real vocals', '',
             '## Against reference f0', '',
             '| Group | Tracks | Ref-voiced frames | Voicing recall | False alarm | RPA (50 c) | Octave errors | Median error |',
             '|---|---|---|---|---|---|---|---|']
    order = sorted(agg.keys(), key=lambda g: (g != 'all', '/' in g, g))
    for g in order:
        a = agg[g]
        rv, ru = max(a['ref_voiced'], 1), max(a['ref_unvoiced'], 1)
        med = np.median(errs[g]) if errs[g] else float('nan')
        lines.append(f"| {g} | {int(a['tracks'])} | {int(a['ref_voiced'])} | {100 * a['recall'] / rv:.1f} % | "
                     f"{100 * a['fa'] / ru:.1f} % | {100 * a['rpa'] / rv:.1f} % | {100 * a['octave'] / rv:.2f} % | {med:.1f} c |")
    lines += ['', '## Without reference (VocalSet): stability per technique', '',
              '| Technique | Voiced share | Voiced frames | Isolated octave outliers |', '|---|---|---|---|']
    for k in sorted(nores.keys(), key=lambda k: (k != 'all VocalSet', ' ' in k, k)):
        nv, no, n = nores[k]
        lines.append(f'| {k} | {100 * nv / max(n, 1):.1f} % | {nv} | {100 * no / max(nv, 1):.2f} % ({no}) |')
    worst = sorted(per_track, key=lambda x: x[1]['rpa'] / max(x[1]['ref_voiced'], 1))[:8]
    lines += ['', '## Lowest RPA tracks', '', '| Track | RPA | Recall | Octave |', '|---|---|---|---|']
    for tid, s in worst:
        rv = max(s['ref_voiced'], 1)
        lines.append(f"| {tid} | {100 * s['rpa'] / rv:.1f} % | {100 * s['recall'] / rv:.1f} % | {100 * s['octave'] / rv:.1f} % |")
    text = '\n'.join(lines) + '\n'
    print(text)
    if report:
        open(report, 'w').write(text)


if __name__ == '__main__':
    main()
