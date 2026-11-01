#!/usr/bin/env python3
"""Compares pitch-shifting algorithms on real vocals (test-vocals branch) with objective measures.

usage: eval_shift.py <test-vocals dir> <KuralLab binary> <work dir> [report.md] [--tracks id,id,...]

For each track × algorithm × interval, with formants preserved (formant shift 0):
  on pitch      share of input-voiced frames whose output pitch is within 50 cents of input × ratio
  pitch error   median |cents| on those frames
  formant shift mean |dB| difference between the input and output spectral envelopes, 300 Hz – 5 kHz
                (cepstral envelopes, liftered below both pitch periods; level difference removed).
                Small = formants kept in place; a plain resampling shifter moves them with the pitch.
  periodicity   output periodicity (detector confidence) relative to the input's: below 1 means
                roughness, phasiness or doubled pulses the input did not have
  level         output RMS vs input RMS on voiced frames (dB)
  CPU           % of one core for one voice (analysis included)
These are indicators, not a verdict: the listening test decides (see VALIDATION.md).
"""
import csv, os, re, subprocess, sys
from collections import defaultdict
import numpy as np
from scipy.io import wavfile

ALGOS = ['psola', 'pv', 'tap']
SHIFTS = [-12, -7, -5, 3, 4, 7, 12]
DEFAULT_TRACKS = 12


def run(cmd):
    return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout


def read(path):
    sr, x = wavfile.read(path)
    return sr, x.astype(np.float64)


def frames(lab, wav, csvpath):
    run([lab, 'analyse', wav, csvpath])
    d = np.genfromtxt(csvpath, delimiter=',', names=True)
    return d['time_s'], d['f0_hz'], d['voicing'].astype(int), d['confidence']


def envelope_db(seg, sr, period_samples):
    n = len(seg)
    w = np.hanning(n)
    spec = np.abs(np.fft.rfft(seg * w)) + 1e-9
    logm = np.log(spec)
    cep = np.fft.irfft(logm, n)
    qc = max(8, int(0.5 * period_samples))
    lift = np.zeros(n)
    lift[:qc] = 1; lift[-qc + 1:] = 1
    env = np.fft.rfft(cep * lift, n).real
    return 20 / np.log(10) * env


def best_offset(x, y, lat, maxp):
    # PSOLA output sits up to one period after its nominal latency: find the best alignment
    seg = slice(int(len(x) * 0.2), int(len(x) * 0.8))
    xs = x[seg]
    best, bo = -1e9, lat
    for off in range(lat - 8, lat + int(maxp) + 8, 4):
        ys = y[seg.start + off: seg.stop + off]
        if len(ys) < len(xs):
            break
        c = np.dot(np.abs(xs), np.abs(ys))       # envelope alignment works for shifted signals too
        if c > best:
            best, bo = c, off
    return bo


def evaluate(track, sr_in, x, t_in, f0_in, v_in, conf_in, y, t_out, f0_out, v_out, conf_out, ratio, offset_s, sr):
    vin = (v_in == 3) & (f0_in > 0)
    # output frame for input time t is at t + offset
    on, errs, periods_in, periods_out = 0, [], [], []
    env_d, n_env = [], 0
    for ti, fi in zip(t_in[vin], f0_in[vin]):
        j = np.searchsorted(t_out, ti + offset_s)
        if j <= 0 or j >= len(t_out):
            continue
        j = j if abs(t_out[j] - ti - offset_s) < abs(t_out[j - 1] - ti - offset_s) else j - 1
        if abs(t_out[j] - ti - offset_s) > 0.006:
            continue
        if f0_out[j] > 0:
            c = 1200 * np.log2(f0_out[j] / (fi * ratio))
            if abs(c) <= 50:
                on += 1; errs.append(abs(c))
    total = int(vin.sum())
    # envelopes on every 4th voiced frame
    N = int(2 ** np.ceil(np.log2(0.046 * sr)))
    for k, (ti, fi) in enumerate(zip(t_in[vin], f0_in[vin])):
        if k % 4:
            continue
        a = int(ti * sr) - N // 2
        b = int((ti + offset_s) * sr) - N // 2
        if a < 0 or b < 0 or a + N > len(x) or b + N > len(y):
            continue
        xs, ys = x[a:a + N], y[b:b + N]
        if np.sqrt(np.mean(ys ** 2)) < 1e-4:
            continue
        p = min(sr / fi, sr / (fi * ratio))
        ex, ey = envelope_db(xs, sr, p), envelope_db(ys, sr, p)
        f = np.fft.rfftfreq(N, 1 / sr)
        band = (f >= 300) & (f <= 5000)
        d = ey[band] - ex[band]
        env_d.append(np.mean(np.abs(d - np.mean(d))))
    # periodicity and level
    vout = (v_out >= 2) & (f0_out > 0)
    per = (np.mean(conf_out[vout]) if vout.any() else 0.0) / max(np.mean(conf_in[vin]) if vin.any() else 1.0, 1e-6)
    lv = []
    for ti in t_in[vin][::8]:
        a = int(ti * sr) - 1024; b = int((ti + offset_s) * sr) - 1024
        if a >= 0 and b >= 0 and a + 2048 <= len(x) and b + 2048 <= len(y):
            lv.append(20 * np.log10((np.sqrt(np.mean(y[b:b + 2048] ** 2)) + 1e-9) / (np.sqrt(np.mean(x[a:a + 2048] ** 2)) + 1e-9)))
    return dict(total=total, on=on, errs=errs, env=env_d, per=per, level=lv)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    root, lab, work = args[0], args[1], args[2]
    report = args[3] if len(args) > 3 else None
    tracks_arg = next((a.split('=', 1)[1] for a in sys.argv if a.startswith('--tracks=')), None)
    os.makedirs(os.path.join(work, 'wav'), exist_ok=True)
    os.makedirs(os.path.join(work, 'shift'), exist_ok=True)
    rows = {r['id']: r for r in csv.DictReader(open(os.path.join(root, 'manifest.csv')))}
    if tracks_arg:
        ids = tracks_arg.split(',')
    else:
        # a spread: low and high voices, solo and choir, breathy and vibrato
        want = [('dcs', 'bass'), ('dcs', 'tenor'), ('dcs', 'alto'), ('dcs', 'soprano'),
                ('vocalset', 'male straight'), ('vocalset', 'female straight'), ('vocalset', 'male vibrato'),
                ('vocalset', 'female vibrato'), ('vocalset', 'male breathy'), ('vocalset', 'female belt')]
        ids = []
        for ds, key in want:
            for r in rows.values():
                label = r['voice'] if ds == 'dcs' else r['voice'] + ' ' + r['technique']
                if r['dataset'] == ds and label == key and r['id'] not in ids:
                    ids.append(r['id']); break
        ids += [r['id'] for r in rows.values() if r['dataset'] == 'vocadito'][:max(0, DEFAULT_TRACKS - len(ids))]
    res = defaultdict(lambda: dict(total=0, on=0, errs=[], env=[], per=[], level=[], cpu=[], lat=0.0))
    for tid in ids:
        r = rows[tid]
        wav = os.path.join(work, 'wav', tid + '.wav')
        if not os.path.exists(wav):
            subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', os.path.join(root, 'audio', tid + '.flac'),
                            '-ac', '1', '-c:a', 'pcm_f32le', wav], check=True)
        sr, x = read(wav)
        t_in, f0_in, v_in, c_in = frames(lab, wav, os.path.join(work, 'shift', tid + '_in.csv'))
        maxp = sr / 65.0
        for algo in ALGOS:
            for s in SHIFTS:
                out = os.path.join(work, 'shift', f'{tid}_{algo}_{s:+d}.wav')
                info = run([lab, 'shift', algo, str(s), '0', '0.3', wav, out])
                lat = int(re.search(r'latency_samples=(\d+)', info).group(1))
                proc = float(re.search(r'process_s=([\d.]+)', info).group(1))
                aud = float(re.search(r'audio_s=([\d.]+)', info).group(1))
                _, y = read(out)
                off = best_offset(x, y, lat, maxp)
                t_o, f0_o, v_o, c_o = frames(lab, out, out[:-4] + '.csv')
                e = evaluate(tid, sr, x, t_in, f0_in, v_in, c_in, y, t_o, f0_o, v_o, c_o, 2 ** (s / 12), off / sr, sr)
                for key in ((algo, s), (algo, 'all')):
                    R = res[key]
                    R['total'] += e['total']; R['on'] += e['on']; R['errs'] += e['errs']; R['env'] += e['env']
                    R['per'].append(e['per']); R['level'] += e['level']; R['cpu'].append(100 * proc / aud)
                    R['lat'] = 1000 * lat / sr
            print('done', tid, flush=True)
    lines = ['# Pitch shifter comparison on real vocals', '',
             f'Tracks ({len(ids)}): ' + ', '.join(ids), '',
             '| Algorithm | Shift | On pitch | Pitch error | Formant shift | Periodicity | Level | CPU (1 voice) | Latency |',
             '|---|---|---|---|---|---|---|---|---|']
    for algo in ALGOS:
        for s in SHIFTS + ['all']:
            R = res[(algo, s)]
            if not R['total']:
                continue
            lines.append(f"| {algo} | {s if s == 'all' else f'{s:+d}'} | {100 * R['on'] / R['total']:.1f} % | "
                         f"{np.median(R['errs']) if R['errs'] else float('nan'):.1f} c | {np.mean(R['env']):.2f} dB | "
                         f"{np.mean(R['per']):.2f} | {np.median(R['level']):+.1f} dB | {np.mean(R['cpu']):.2f} % | {R['lat']:.1f} ms |")
    text = '\n'.join(lines) + '\n'
    print(text)
    if report:
        open(report, 'w').write(text)


if __name__ == '__main__':
    main()
