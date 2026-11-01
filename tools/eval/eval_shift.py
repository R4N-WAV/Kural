#!/usr/bin/env python3
"""Compares pitch-shifting algorithms on real vocals (test-vocals branch) with objective measures.

usage: eval_shift.py <test-vocals dir> <KuralLab binary> <work dir> [report.md] [--tracks=id,..] [--algos=psola,..] [--shifts=4,-5,..] [--tag=x]

For each track × algorithm × interval, with formants preserved (formant shift 0):
  on pitch      share of input-voiced frames whose output pitch is within 50 cents of input × ratio
  pitch error   median |cents| on those frames
  formant shift mean |dB| difference between the input and output spectral envelopes, 300 Hz – 5 kHz,
                where the input envelope is within 40 dB of its peak (cepstral envelopes, liftered below
                both pitch periods; level difference removed).
                Small = formants kept in place; a plain resampling shifter moves them with the pitch.
  ghost of lead energy left at the original pitch's harmonics relative to the new pitch's harmonics
                (dB, median): period repetition puts it there and it sounds buzzy/robotic; lower = cleaner
  ghost excess  the same measure minus the input's own energy at the same places between its harmonics
                (breath, jitter and vibrato put some there in any voice): 0 dB = no added ghost
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
SHIFTS = [0, -12, -7, -5, 3, 4, 7, 12]      # 0 = unshifted: shows the floor of each measure
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


def inharmonic_db(seg, sr, f0):
    """energy between the harmonics of f0 relative to energy on them (dB), 80 Hz - 3 kHz.
    Higher = rougher: sidebands, buzz or noise that the harmonics do not account for."""
    n = len(seg)
    p = np.abs(np.fft.rfft(seg * np.hanning(n))) ** 2
    f = np.fft.rfftfreq(n, 1 / sr)
    band = (f >= 80) & (f <= 3000)
    k = np.round(f / f0)
    on = band & (k >= 1) & (np.abs(f - k * f0) <= 0.2 * f0)
    off = band & ~on
    return 10 * np.log10((p[off].sum() + 1e-20) / (p[on].sum() + 1e-20))


def ghost_db(seg, sr, f_in, f_out):
    """'ghost' of the original pitch in a shifted voice: energy at the input's harmonics (where they
    are not close to an output harmonic) relative to energy at the output's harmonics, 80 Hz - 3 kHz
    (dB). Period repetition / dropping in PSOLA modulates at |f_out - f_in| and puts energy exactly
    there; heard as a buzzy, robotic edge. Lower is cleaner."""
    n = len(seg)
    p = np.abs(np.fft.rfft(seg * np.hanning(n))) ** 2
    f = np.fft.rfftfreq(n, 1 / sr)
    band = (f >= 80) & (f <= 3000)
    ko = np.round(f / f_out)
    on_out = band & (ko >= 1) & (np.abs(f - ko * f_out) <= 0.06 * f_out)
    ki = np.round(f / f_in)
    near_out = np.abs(f - ko * f_out) <= 0.25 * f_out
    on_in = band & (ki >= 1) & (np.abs(f - ki * f_in) <= 0.06 * f_in) & ~near_out
    if not on_in.any():
        return None
    return 10 * np.log10((p[on_in].mean() + 1e-20) / (p[on_out].mean() + 1e-20))


def best_offset(x, y, lat, maxp, sr):
    # PSOLA and the phase vocoder output exactly at their reported latency. The rotating-tap baseline
    # wanders around ~W/2, so it is aligned by its level envelope (10 ms RMS), which works for shifted
    # signals too
    def env(v):
        k = int(0.01 * sr)
        return np.sqrt(np.convolve(v ** 2, np.ones(k) / k, mode='same'))
    ex, ey = env(x), env(y)
    seg = slice(int(len(x) * 0.2), int(len(x) * 0.8))
    xs = ex[seg] - ex[seg].mean()
    best, bo = -1e9, lat
    for off in range(max(0, lat - int(maxp)), lat + int(maxp), 8):
        ys = ey[seg.start + off: seg.stop + off]
        if len(ys) < len(xs):
            break
        c = np.dot(xs, ys - ys.mean())
        if c > best:
            best, bo = c, off
    return bo


def evaluate(track, sr_in, x, t_in, f0_in, v_in, conf_in, y, t_out, f0_out, v_out, conf_out, ratio, offset_s, sr):
    vin = (v_in == 3) & (f0_in > 0)
    # output frame for input time t is at t + offset
    on, errs, periods_in, periods_out = 0, [], [], []
    env_d, n_env, rough_d, excess_d = [], 0, [], []
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
        if fi * ratio >= 90 and abs(ratio - 1) > 0.02:
            # 10 periods of the output / of the input, so window leakage is the same relative to the harmonics
            no, ni = int(10 * sr / (fi * ratio)), int(10 * sr / fi)
            co, ci = b + N // 2, a + N // 2
            yg = y[max(0, co - no // 2): co - no // 2 + no]
            xg = x[max(0, ci - ni // 2): ci - ni // 2 + ni]
            g = ghost_db(yg, sr, fi, fi * ratio) if len(yg) == no else None
            g0 = ghost_db(xg, sr, fi / ratio, fi) if len(xg) == ni else None   # the input's own energy at the same places
            if g is not None and g0 is not None:
                rough_d.append(g); excess_d.append(g - g0)
        ex, ey = envelope_db(xs, sr, p), envelope_db(ys, sr, p)
        f = np.fft.rfftfreq(N, 1 / sr)
        # compare only where the input envelope carries energy (within 40 dB of its peak in the band):
        # near-silent bins make large log differences out of nothing
        band = (f >= 300) & (f <= 5000)
        band &= ex >= np.max(ex[band]) - 40.0
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
    return dict(total=total, on=on, errs=errs, env=env_d, per=per, level=lv, rough=rough_d, excess=excess_d)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    root, lab, work = args[0], args[1], args[2]
    report = args[3] if len(args) > 3 else None
    tracks_arg = next((a.split('=', 1)[1] for a in sys.argv if a.startswith('--tracks=')), None)
    global ALGOS, SHIFTS
    ALGOS = next((a.split('=', 1)[1].split(',') for a in sys.argv if a.startswith('--algos=')), ALGOS)
    SHIFTS = next(([int(v) for v in a.split('=', 1)[1].split(',')] for a in sys.argv if a.startswith('--shifts=')), SHIFTS)
    tag = next((a.split('=', 1)[1] for a in sys.argv if a.startswith('--tag=')), '')
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
    res = defaultdict(lambda: dict(total=0, on=0, errs=[], env=[], per=[], level=[], cpu=[], lat=0.0, rough=[], excess=[]))
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
                out = os.path.join(work, 'shift', f'{tid}_{algo}{tag}_{s:+d}.wav')
                info = run([lab, 'shift', algo, str(s), '0', '0.3', wav, out])
                lat = int(re.search(r'latency_samples=(\d+)', info).group(1))
                proc = float(re.search(r'process_s=([\d.]+)', info).group(1))
                aud = float(re.search(r'audio_s=([\d.]+)', info).group(1))
                _, y = read(out)
                off = lat if algo in ('psola', 'pv') else best_offset(x, y, lat, maxp, sr)
                t_o, f0_o, v_o, c_o = frames(lab, out, out[:-4] + '.csv')
                e = evaluate(tid, sr, x, t_in, f0_in, v_in, c_in, y, t_o, f0_o, v_o, c_o, 2 ** (s / 12), off / sr, sr)
                for key in ((algo, s), (algo, 'all')) if s != 0 else ((algo, s),):
                    R = res[key]
                    R['total'] += e['total']; R['on'] += e['on']; R['errs'] += e['errs']; R['env'] += e['env']; R['rough'] += e['rough']; R['excess'] += e['excess']
                    R['per'].append(e['per']); R['level'] += e['level']; R['cpu'].append(100 * proc / aud)
                    R['lat'] = 1000 * lat / sr
            print('done', tid, flush=True)
    lines = ['# Pitch shifter comparison on real vocals', '',
             f'Tracks ({len(ids)}): ' + ', '.join(ids), '',
             '| Algorithm | Shift | On pitch | Pitch error | Formant shift | Ghost of lead | Ghost excess | Periodicity | Level | CPU (1 voice) | Latency |',
             '|---|---|---|---|---|---|---|---|---|---|---|']
    for algo in ALGOS:
        for s in SHIFTS + ['all']:
            R = res[(algo, s)]
            if not R['total']:
                continue
            lines.append(f"| {algo} | {s if s == 'all' else ('0 (floor)' if s == 0 else f'{s:+d}')} | {100 * R['on'] / R['total']:.1f} % | "
                         f"{np.median(R['errs']) if R['errs'] else float('nan'):.1f} c | {np.mean(R['env']):.2f} dB | {np.median(R['rough']) if R['rough'] else float('nan'):+.1f} dB | "
                         f"{np.median(R['excess']) if R['excess'] else float('nan'):+.1f} dB | "
                         f"{np.mean(R['per']):.2f} | {np.median(R['level']):+.1f} dB | {np.mean(R['cpu']):.2f} % | {R['lat']:.1f} ms |")
    text = '\n'.join(lines) + '\n'
    print(text)
    if report:
        open(report, 'w').write(text)


if __name__ == '__main__':
    main()
