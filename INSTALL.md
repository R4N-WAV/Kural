# Kural (R4N) – install and use

Kural is a vocal harmonizer, VST3, 64-bit, for Windows 10 and 11. Made for FL Studio, Cubase and
Reaper; it should work in any VST3 host.

**This is a development build (0.2).** Four harmony voices work: Kural follows the pitch of your
vocal and sings up to four harmony lines in the key you choose. More voices (up to 16), presets,
MIDI control and quality modes come in later versions.

## Install with the setup wizard
1. Close your music app, then run **Kural-Setup-Windows.exe**.
2. Kural isn't code-signed yet, so Windows may show *Windows protected your PC*: click **More info** → **Run anyway**. Allow the admin prompt (the VST3 folder is a system folder).
3. Click through the wizard. It installs `Kural.vst3` into `C:\Program Files\Common Files\VST3` (you can pick another VST3 folder), replaces an older version cleanly and adds an uninstaller (**Settings → Apps → Kural**).
4. Rescan plugins in your host (below) and load **Kural** (listed under **R4N**) on a vocal track.

## Install by hand (portable zip)
1. Right-click the zip → **Properties** → tick **Unblock** → **OK**. Unzip it.
2. Copy the folder **`Kural.vst3`** into `C:\Program Files\Common Files\VST3`.
3. Rescan plugins in your host.

## Rescan plugins
- **FL Studio:** Options → Manage plugins → **Find installed plugins**. Then add Kural to a mixer insert on your vocal.
- **Cubase:** Studio → VST Plug-in Manager → rescan (or restart Cubase). Kural appears under Pitch Shift / R4N.
- **Reaper:** Options → Preferences → Plug-ins → VST → **Re-scan**. Add it as a track FX.

## Using this build
1. Put Kural on a vocal track (mono or stereo) and set **KEY** and the scale (Major, Natural minor, …)
   to the song. Harmonies count their intervals along that scale, so a "3rd above" is a major or a
   minor third as the key requires.
2. Play the track. The screen shows your vocal as a bright line and each harmony voice as a thinner
   line in its own colour; the notes the voices are singing appear at the top right.
3. Each **VOICE** strip: switch it on, pick an interval (or **Semitones** for a fixed distance),
   add octaves, and set **LEVEL**, **PAN**, **FORMANT** (up = smaller, brighter voice; down = bigger,
   darker) and **FINE** (detune in cents). Defaults: voice 1 a third above (left), voice 2 a third
   below (right).
4. Bottom row: **CONSONANTS** (how much of the "s", "t" and breaths the harmonies carry; they are
   never pitch-shifted), **GLIDE** (how quickly a voice moves to a new note), **HARMONY** (all voices),
   **DRY** (your original vocal), **OUTPUT**. **HARMONIES** switches all voices off and leaves the dry vocal.
5. **BYPASS** passes the input through untouched (also linked to your host's bypass).

Kural delays its output by about 31 ms at 48 kHz (the screen shows the exact figure) and tells your
host, which compensates automatically. Settings are saved with your project.

## Troubleshooting
- **Kural doesn't appear:** check that `C:\Program Files\Common Files\VST3\Kural.vst3` exists, that your host scans that folder, then rescan. In FL Studio, make sure the VST3 folder is in the plugin search paths.
- **The installer says Kural is in use:** your music app has it loaded. Close the app and click Back, then Next.
- **Blocked by Windows:** see step 2 above; for the zip, unblock it before unzipping.
- **The display stays empty:** Kural only draws when audio is playing through it. Check the INPUT meter moves.

## Uninstall
Settings → Apps → **Kural (VST3) by R4N** → Uninstall. This removes `Kural.vst3` from the VST3 folder.
