#!/usr/bin/env python3
"""Builds Kural's real-vocal test set from openly licensed (CC BY 4.0) datasets.

Runs in GitHub Actions (the development container cannot reach Zenodo). Downloads the archives,
verifies their MD5 checksums, selects a balanced subset, converts it to 16-bit mono FLAC and writes
manifest.csv + ATTRIBUTION.md. The result is committed to the orphan branch `test-vocals` by
.github/workflows/fetch-test-vocals.yml, so `main` stays small.

usage: fetch_vocals.py <work dir> <out dir> [--survey]
  --survey: only list the archive contents (to adapt the selection rules)

Datasets (all Creative Commons Attribution 4.0 International):
  vocadito         40 solo singing excerpts, f0 annotations         zenodo.org/record/5578807
  Dagstuhl ChoirSet SATB singers, larynx-mic manual f0 annotations   zenodo.org/record/4618287
  VocalSet         20 singers, techniques (breathy, vibrato, fry...) zenodo.org/records/1442513
"""
import csv, hashlib, os, re, subprocess, sys, zipfile
from collections import Counter, defaultdict

DATASETS = {
    'vocadito': dict(
        url='https://zenodo.org/record/5578807/files/vocadito.zip?download=1',
        md5='dea40fd18f14d899643c4ba221b33a46', file='vocadito.zip',
        cite='Bittner, R. M., Pasalo, K., Bosch, J. J., Meseguer-Brocal, G., & Rubinstein, D. (2021). '
             'vocadito: A dataset of solo vocals with f0, note, and lyric annotations. arXiv:2110.05580. '
             'https://doi.org/10.5281/zenodo.5578807'),
    'dcs': dict(
        url='https://zenodo.org/record/4618287/files/DagstuhlChoirSet_V1.2.3.zip?download=1',
        md5='82b95faa634d0c9fc05c81e0868f0217', file='DagstuhlChoirSet_V1.2.3.zip',
        cite='Rosenzweig, S., Cuesta, H., Weiss, C., Scherbaum, F., Gómez, E., & Müller, M. (2020). '
             'Dagstuhl ChoirSet: A multitrack dataset for MIR research on choral singing. TISMIR 3(1), 98-110. '
             'https://doi.org/10.5281/zenodo.4618287'),
    'vocalset': dict(
        url='https://zenodo.org/records/1442513/files/VocalSet11.zip?download=1',
        md5='0c09396242f946e7111ad7d8fc649b81', file='VocalSet11.zip',
        cite='Wilkins, J., Seetharaman, P., Wahl, A., & Pardo, B. (2018). VocalSet: A singing voice dataset. '
             'ISMIR 2018. https://doi.org/10.5281/zenodo.1442513'),
}

MAX_SECONDS = 45.0          # longer recordings are cut (the f0 reference is cut to match)


def run(cmd):
    print('$', ' '.join(cmd), flush=True)
    subprocess.run(cmd, check=True)


def md5(path):
    h = hashlib.md5()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 22), b''):
            h.update(chunk)
    return h.hexdigest()


def download(work, key):
    d = DATASETS[key]
    path = os.path.join(work, d['file'])
    if not (os.path.exists(path) and md5(path) == d['md5']):
        run(['curl', '-L', '--fail', '--retry', '6', '--retry-delay', '20', '-sS', '-o', path, d['url']])
    got = md5(path)
    if got != d['md5']:
        sys.exit(f'{key}: MD5 mismatch ({got} != {d["md5"]})')
    print(f'{key}: {os.path.getsize(path) / 1e6:.0f} MB, MD5 ok', flush=True)
    return zipfile.ZipFile(path)


def survey(z, key, depth=4):
    names = [n for n in z.namelist() if not n.endswith('/')]
    print(f'\n===== {key}: {len(names)} files')
    ext = Counter(os.path.splitext(n)[1].lower() for n in names)
    print('extensions:', dict(ext))
    dirs = Counter('/'.join(n.split('/')[:depth]) for n in names)
    for d, c in sorted(dirs.items())[:200]:
        print(f'  {c:5d}  {d}')
    for n in names[:15]:
        print('  e.g.', n)


def to_flac(z, member, out_path, seconds=None):
    tmp = out_path + '.src' + os.path.splitext(member)[1]
    with z.open(member) as src, open(tmp, 'wb') as dst:
        dst.write(src.read())
    cmd = ['ffmpeg', '-v', 'error', '-y', '-i', tmp, '-ac', '1', '-sample_fmt', 's16']
    if seconds:
        cmd += ['-t', f'{seconds:.3f}']
    cmd += ['-compression_level', '8', out_path]
    subprocess.run(cmd, check=True)
    os.remove(tmp)
    info = subprocess.run(['ffprobe', '-v', 'error', '-show_entries', 'stream=sample_rate:format=duration',
                           '-of', 'default=nw=1', out_path], capture_output=True, text=True, check=True).stdout
    sr = int(re.search(r'sample_rate=(\d+)', info).group(1))
    dur = float(re.search(r'duration=([\d.]+)', info).group(1))
    return sr, dur


def write_f0(rows, out_path, limit):
    """rows: (t, hz) pairs; writes time,hz with 0 for unvoiced, cut at `limit` seconds"""
    with open(out_path, 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['time_s', 'f0_hz'])
        for t, hz in rows:
            if t > limit:
                break
            w.writerow([f'{t:.5f}', f'{max(0.0, hz):.4f}'])


def read_f0_csv(z, member):
    rows = []
    with z.open(member) as f:
        for line in f.read().decode('utf-8', 'replace').splitlines():
            parts = [p.strip() for p in line.replace(';', ',').split(',')]
            try:
                t, hz = float(parts[0]), float(parts[1])
            except (ValueError, IndexError):
                continue          # header or blank
            rows.append((t, hz))
    return rows


# ---------------------------------------------------------------- selections
def select_vocadito(z, out, manifest):
    names = z.namelist()
    audio = sorted(n for n in names if re.search(r'Audio/.*\.wav$', n, re.I))
    f0s = {os.path.basename(n).split('_f0')[0]: n for n in names if re.search(r'F0/.*\.csv$', n, re.I)}
    print(f'vocadito: {len(audio)} audio, {len(f0s)} f0 files')
    for a in audio:
        stem = os.path.splitext(os.path.basename(a))[0]
        ref = f0s.get(stem)
        if ref is None:
            continue
        tid = f'vocadito_{stem}'
        sr, dur = to_flac(z, a, os.path.join(out, 'audio', tid + '.flac'), MAX_SECONDS)
        write_f0(read_f0_csv(z, ref), os.path.join(out, 'f0', tid + '.csv'), dur)
        manifest.append(dict(id=tid, dataset='vocadito', singer=stem, voice='', technique='solo song',
                             context='song', mic='various (crowd-sourced)', sr=sr, seconds=f'{dur:.2f}',
                             f0_ref='f0/' + tid + '.csv', f0_ref_kind='vocadito annotation', source=a))


def select_dcs(z, out, manifest, max_tracks=24):
    names = z.namelist()
    manual = [n for n in names if re.search(r'manual', n, re.I) and n.lower().endswith('.csv') and 'lrx' in n.lower()]
    wavs = [n for n in names if n.lower().endswith('.wav')]
    print(f'dcs: {len(manual)} manual LRX f0 files, {len(wavs)} wav files')
    # prefer quartet takes (less bleed from the other singers), one track per singer/piece
    manual.sort(key=lambda n: (0 if 'quartet' in n.lower() else 1, n))
    picked, seen = 0, set()
    for ref in manual:
        stem = os.path.splitext(os.path.basename(ref))[0]
        base = re.sub(r'_(LRX|HSM|DYN)$', '', re.sub(r'(?i)_?f0.*$', '', stem.replace('_manual', '')), flags=re.I)
        part = re.search(r'_([SATB]\d)', base)
        piece = re.search(r'DCS_([A-Z]+)_', base)
        key = (part.group(1) if part else base, piece.group(1) if piece else '')
        if key in seen:
            continue
        audio = None
        for mic in ('HSM', 'DYN', 'LRX'):
            cand = [w for w in wavs if os.path.basename(w).startswith(base) and re.search(mic, os.path.basename(w), re.I)]
            if cand:
                audio, used = cand[0], mic
                break
        if audio is None:
            print('  no audio for', ref)
            continue
        seen.add(key)
        tid = 'dcs_' + base.replace('DCS_', '')
        sr, dur = to_flac(z, audio, os.path.join(out, 'audio', tid + '.flac'), MAX_SECONDS)
        write_f0(read_f0_csv(z, ref), os.path.join(out, 'f0', tid + '.csv'), dur)
        voice = {'S': 'soprano', 'A': 'alto', 'T': 'tenor', 'B': 'bass'}.get(key[0][0], '')
        manifest.append(dict(id=tid, dataset='dcs', singer=key[0], voice=voice, technique='choral singing',
                             context='quartet' if 'quartet' in audio.lower() else 'full choir', mic=used,
                             sr=sr, seconds=f'{dur:.2f}', f0_ref='f0/' + tid + '.csv',
                             f0_ref_kind='manual, larynx microphone', source=audio))
        picked += 1
        if picked >= max_tracks:
            break


VS_TECH = ['straight', 'vibrato', 'breathy', 'belt', 'vocal_fry', 'spoken', 'fast_forte', 'lip_trill', 'messa']
VS_CONTEXT_ORDER = ['excerpts', 'arpeggios', 'long_tones', 'scales']


def select_vocalset(z, out, manifest):
    wavs = [n for n in z.namelist() if n.lower().endswith('.wav') and '__macosx' not in n.lower()]
    # path like .../FULL/<singer>/<context>/<technique>/<file>.wav
    by = defaultdict(list)
    for w in wavs:
        parts = w.split('/')
        singer = next((p for p in parts if re.fullmatch(r'[mf]\d+', p)), None)
        if singer is None:
            continue
        low = w.lower()
        tech = next((t for t in VS_TECH if f'/{t}' in low or f'_{t}' in low), None)
        if tech is None:
            continue
        ctx = next((c for c in VS_CONTEXT_ORDER if f'/{c}/' in low), 'other')
        by[(singer, tech)].append((VS_CONTEXT_ORDER.index(ctx) if ctx in VS_CONTEXT_ORDER else 9,
                                   0 if re.search(r'_a\.wav$|_a_|/a/', low) else 1, w, ctx))
    print(f'vocalset: {len(wavs)} wav files, {len(by)} singer x technique groups')
    for (singer, tech), cands in sorted(by.items()):
        cands.sort()
        _, _, w, ctx = cands[0]
        tid = f'vocalset_{singer}_{tech}_{ctx}'
        sr, dur = to_flac(z, w, os.path.join(out, 'audio', tid + '.flac'), MAX_SECONDS)
        manifest.append(dict(id=tid, dataset='vocalset', singer=singer,
                             voice='male' if singer.startswith('m') else 'female', technique=tech.replace('_', ' '),
                             context=ctx, mic='studio', sr=sr, seconds=f'{dur:.2f}', f0_ref='', f0_ref_kind='',
                             source=w))


def attribution(out, manifest):
    used = sorted({m['dataset'] for m in manifest})
    with open(os.path.join(out, 'ATTRIBUTION.md'), 'w') as f:
        f.write('# Kural test vocals – attribution\n\n')
        f.write('This branch holds a subset of the following datasets, each licensed under the Creative Commons '
                'Attribution 4.0 International licence (https://creativecommons.org/licenses/by/4.0/).\n\n')
        f.write('**Changes made:** a subset of files was selected; audio was converted to 16-bit mono FLAC and '
                f'cut to at most {MAX_SECONDS:.0f} s; f0 reference annotations were converted to `time_s,f0_hz` '
                'CSV (0 = unvoiced) and cut to the same length. Nothing else was altered.\n\n')
        for k in used:
            f.write(f'- **{k}** — {DATASETS[k]["cite"]}\n')
        f.write('\nThese recordings are used only to test Kural. They are not part of the plugin and are not '
                'distributed with it.\n')
    with open(os.path.join(out, 'manifest.csv'), 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(manifest[0].keys()))
        w.writeheader()
        w.writerows(manifest)


def main():
    work, out = sys.argv[1], sys.argv[2]
    only_survey = '--survey' in sys.argv
    os.makedirs(work, exist_ok=True)
    os.makedirs(os.path.join(out, 'audio'), exist_ok=True)
    os.makedirs(os.path.join(out, 'f0'), exist_ok=True)
    manifest = []
    for key, sel in (('vocadito', select_vocadito), ('dcs', select_dcs), ('vocalset', select_vocalset)):
        z = download(work, key)
        survey(z, key)
        if not only_survey:
            n0 = len(manifest)
            sel(z, out, manifest)
            print(f'{key}: selected {len(manifest) - n0} tracks', flush=True)
    if not only_survey:
        if not manifest:
            sys.exit('nothing selected')
        attribution(out, manifest)
        total = sum(os.path.getsize(os.path.join(dp, f)) for dp, _, fs in os.walk(out) for f in fs)
        print(f'\nTOTAL: {len(manifest)} tracks, {total / 1e6:.1f} MB')
        print(Counter(m['dataset'] for m in manifest))


if __name__ == '__main__':
    main()
