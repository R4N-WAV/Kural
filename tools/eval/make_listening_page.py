#!/usr/bin/env python3
"""Builds the Kural listening-test page (one self-contained HTML file, audio embedded as MP3 data URIs).

usage: make_listening_page.py <clips.json> <out.html>

clips.json:
{
  "sections": [
    {"title": "...", "note": "...", "clips": [{"label": "...", "group": "original|psola|pv|tap|harmony", "wav": "path"}]}
  ],
  "measurements": [["PSOLA", "+4", "99.1 %", ...], ...],      # optional table rows
  "measurementHeader": ["Algorithm", "Shift", ...]
}
"""
import base64, html, json, os, subprocess, sys, tempfile


def mp3_data_uri(wav, kbps=112):
    with tempfile.NamedTemporaryFile(suffix='.mp3', delete=False) as t:
        out = t.name
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', wav, '-codec:a', 'libmp3lame', '-b:a', f'{kbps}k', out], check=True)
    data = open(out, 'rb').read()
    os.remove(out)
    return 'data:audio/mpeg;base64,' + base64.b64encode(data).decode()


CSS = r"""
:root {
  /* layout: one reading column; each recording is a row of identical players, groups keep fixed colours */
  --bg: #eef3f1; --panel: #ffffff; --ink: #0d2430; --muted: #4d6670; --line: #cfdcd9;
  --psola: #2f8f7a; --pv: #4f6fb0; --tap: #a7782f; --orig: #0d2430; --harm: #8a5aa8;
  --display: "Catamaran", "Noto Sans Tamil", system-ui, sans-serif;
  --body: "Barlow Semi Condensed", "Arial Narrow", system-ui, sans-serif;
  --mono: "IBM Plex Mono", ui-monospace, monospace;
}
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) {
  --bg: #04121a; --panel: #0a2230; --ink: #e6ece4; --muted: #8fa3a3; --line: #16303e;
  --psola: #7cc8b4; --pv: #8fb4e8; --tap: #d9c08a; --orig: #e6ece4; --harm: #c79fd6; color-scheme: dark } }
:root[data-theme="dark"] {
  --bg: #04121a; --panel: #0a2230; --ink: #e6ece4; --muted: #8fa3a3; --line: #16303e;
  --psola: #7cc8b4; --pv: #8fb4e8; --tap: #d9c08a; --orig: #e6ece4; --harm: #c79fd6; color-scheme: dark }
body { background: var(--bg); color: var(--ink); font-family: var(--body); font-size: 17px; line-height: 1.5;
  padding-left: max(16px, env(safe-area-inset-left, 0px)); padding-right: max(16px, env(safe-area-inset-right, 0px)); padding-block: 28px 64px; }
main { max-width: 860px; margin: 0 auto; display: grid; gap: 28px; }
header { display: grid; gap: 6px; }
.lockup { display: flex; align-items: baseline; gap: 14px; flex-wrap: wrap; }
.tamil { font-family: var(--display); font-weight: 800; font-size: 44px; line-height: 1.1; }
.word { letter-spacing: .22em; font-weight: 600; font-size: 15px; color: var(--muted); }
h1 { font-family: var(--body); font-weight: 700; font-size: 26px; margin: 0; text-wrap: balance; }
h2 { font-size: 20px; margin: 0 0 4px; text-wrap: balance; }
p { margin: 0; max-width: 65ch; }
.muted { color: var(--muted); }
.controls { display: flex; gap: 12px; flex-wrap: wrap; align-items: center; }
button { font: inherit; font-weight: 600; font-size: 15px; border: 1px solid var(--line); background: var(--panel); color: var(--ink);
  border-radius: 999px; padding: 6px 14px; cursor: pointer; }
button[aria-pressed="true"] { border-color: var(--psola); }
button:focus-visible { outline: 2px solid var(--psola); outline-offset: 2px; }
section.rec { background: var(--panel); border: 1px solid var(--line); border-radius: 10px; padding: 18px; display: grid; gap: 12px; min-width: 0; }
.row { display: grid; grid-template-columns: 170px 1fr; gap: 12px; align-items: center; }
.tag { font-weight: 700; font-size: 14px; letter-spacing: .06em; text-transform: uppercase; display: flex; align-items: center; gap: 8px; }
.dot { width: 10px; height: 10px; border-radius: 50%; background: var(--orig); flex: none; }
.g-psola .dot { background: var(--psola); } .g-pv .dot { background: var(--pv); } .g-tap .dot { background: var(--tap); }
.g-harmony .dot { background: var(--harm); }
audio { width: 100%; min-width: 0; height: 36px; }
.blind .name { display: none; } .blind .letter { display: inline; } .letter { display: none; }
.blind .g-psola .dot, .blind .g-pv .dot, .blind .g-tap .dot { background: var(--muted); }
table { border-collapse: collapse; font-variant-numeric: tabular-nums; font-size: 15px; min-width: 640px; }
th, td { text-align: left; padding: 6px 10px; border-bottom: 1px solid var(--line); }
th { color: var(--muted); font-weight: 600; font-size: 13px; letter-spacing: .05em; text-transform: uppercase; }
.scroll { overflow-x: auto; }
.legend { display: flex; gap: 18px; flex-wrap: wrap; font-size: 15px; }
.legend span { display: inline-flex; align-items: center; gap: 6px; }
@media (max-width: 560px) { .row { grid-template-columns: 1fr; gap: 4px; } }
"""

JS = r"""
const root = document.querySelector('main');
const blindBtn = document.getElementById('blind');
blindBtn.addEventListener('click', () => {
  const on = root.classList.toggle('blind');
  blindBtn.setAttribute('aria-pressed', on ? 'true' : 'false');
  blindBtn.textContent = on ? 'Show names' : 'Hide names (blind)';
  if (on) shuffle();
});
function shuffle() {
  document.querySelectorAll('section.rec').forEach(sec => {
    const rows = [...sec.querySelectorAll('.row.shuffle')];
    for (let i = rows.length - 1; i > 0; i--) { const j = Math.floor(Math.random() * (i + 1)); [rows[i], rows[j]] = [rows[j], rows[i]]; }
    rows.forEach((r, k) => { sec.appendChild(r); const l = r.querySelector('.letter'); if (l) l.textContent = 'Version ' + 'ABCDEFG'[k]; });
  });
}
// one player at a time
document.addEventListener('play', e => { document.querySelectorAll('audio').forEach(a => { if (a !== e.target) a.pause(); }); }, true);
"""

NAMES = {'original': 'Original', 'psola': 'Kural PSOLA', 'pv': 'Phase vocoder', 'tap': 'ATELIER tap', 'harmony': 'Kural harmonies'}


def main():
    spec = json.load(open(sys.argv[1]))
    out = sys.argv[2]
    parts = ['<title>Kural Listening Test</title>',
             '<meta name="claude:viewport-fit-sides" content="cover">',
             '<link rel="preconnect" href="https://fonts.googleapis.com">',
             '<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow+Semi+Condensed:wght@500;600;700&family=Catamaran:wght@800&display=swap">',
             f'<style>{CSS}</style>', '<main>',
             '<header><div class="lockup"><span class="tamil" lang="ta">குரல்</span><span class="word">KURAL · BY R4N</span></div>',
             f'<h1>{html.escape(spec.get("title", "Pitch shifter listening test"))}</h1>',
             f'<p class="muted">{html.escape(spec.get("intro", ""))}</p></header>',
             '<div class="controls"><button id="blind" type="button" aria-pressed="false">Hide names (blind)</button>'
             '<div class="legend"><span><i class="dot" style="background:var(--psola)"></i>Kural PSOLA</span>'
             '<span><i class="dot" style="background:var(--pv)"></i>Phase vocoder</span>'
             '<span><i class="dot" style="background:var(--tap)"></i>ATELIER tap (baseline)</span>'
             '<span><i class="dot" style="background:var(--harm)"></i>Full harmonies</span></div></div>']
    for sec in spec['sections']:
        parts.append('<section class="rec">')
        parts.append(f'<div><h2>{html.escape(sec["title"])}</h2><p class="muted">{html.escape(sec.get("note", ""))}</p></div>')
        for c in sec['clips']:
            g = c['group']
            shuffle = ' shuffle' if g in ('psola', 'pv', 'tap') else ''
            label = html.escape(c.get('label', NAMES.get(g, g)))
            parts.append(f'<div class="row g-{g}{shuffle}"><div class="tag"><i class="dot"></i>'
                         f'<span class="name">{label}</span><span class="letter"></span></div>'
                         f'<audio controls preload="none" src="{mp3_data_uri(c["wav"])}"></audio></div>')
        parts.append('</section>')
    if spec.get('measurements'):
        parts.append('<section class="rec"><div><h2>Measurements on the full test set</h2>'
                     f'<p class="muted">{html.escape(spec.get("measurementNote", ""))}</p></div><div class="scroll"><table><thead><tr>')
        parts += [f'<th>{html.escape(h)}</th>' for h in spec['measurementHeader']]
        parts.append('</tr></thead><tbody>')
        for row in spec['measurements']:
            parts.append('<tr>' + ''.join(f'<td>{html.escape(str(v))}</td>' for v in row) + '</tr>')
        parts.append('</tbody></table></div></section>')
    parts.append(f'<p class="muted">{html.escape(spec.get("credits", ""))}</p>')
    parts.append('</main>')
    parts.append(f'<script>{JS}</script>')
    open(out, 'w').write('\n'.join(parts))
    print(f'wrote {out}: {os.path.getsize(out) / 1e6:.1f} MB')


if __name__ == '__main__':
    main()
