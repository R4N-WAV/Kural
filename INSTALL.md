# Kural (R4N) – install and use

Kural is a vocal harmonizer, VST3, 64-bit, for Windows 10 and 11. Made for FL Studio, Cubase and
Reaper; it should work in any VST3 host.

**This is a development build.** It tracks the pitch of your vocal live and shows it on Kural's
display. The harmony voices are not in this build yet.

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
- Put Kural on a vocal track and play the track (or sing through it). The display shows the pitch of the voice as a line moving right to left: the note name and how many cents above or below it, plus how sure the analysis is. Breaths and consonants leave gaps; that is correct.
- **Output**: level after Kural (−24 to +12 dB, double-click for 0 dB).
- **Bypass**: passes the input through untouched, crossfaded so it never clicks (also linked to your host's bypass).
- Kural adds no delay (latency 0 samples) in this build. Settings are saved with your project.

## Troubleshooting
- **Kural doesn't appear:** check that `C:\Program Files\Common Files\VST3\Kural.vst3` exists, that your host scans that folder, then rescan. In FL Studio, make sure the VST3 folder is in the plugin search paths.
- **The installer says Kural is in use:** your music app has it loaded. Close the app and click Back, then Next.
- **Blocked by Windows:** see step 2 above; for the zip, unblock it before unzipping.
- **The display stays empty:** Kural only draws when audio is playing through it. Check the INPUT meter moves.

## Uninstall
Settings → Apps → **Kural (VST3) by R4N** → Uninstall. This removes `Kural.vst3` from the VST3 folder.
