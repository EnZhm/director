# Changelog

Director is the suite release: it bundles Palette Director and Clip Director, which
keep their own versions and their own changelogs.

## 1.0.0 - first release

Suite = Palette Director 1.2.0 + Clip Director 1.1.0.

| Component | Version | Changelog |
|---|---|---|
| Palette Director | 1.2.0 | https://github.com/EnZhm/palette-director/blob/master/CHANGELOG.md |
| Clip Director | 1.1.0 | https://github.com/EnZhm/clip-director/blob/main/CHANGELOG.md |

What it adds:

- One Windows x64 zip with both plugins, `director-1.0.0-windows-x64.zip`; both folders
  are copied into `C:\ProgramData\obs-studio\plugins\`.
- Palette Director: filter "Palette Lock", dock "Palette Director", dock "Music"
  (BPM, beat lights, section, level and bass meter, 64-beat graph, audio controls,
  Phrase is now / Drop now / Force breakdown / Tap tempo / Lock BPM / Nudge), and the
  read-only beat clock for other plugins.
- Clip Director: source "Clip Director", dock "Clip Director" with the new Synesthesia
  tab (OSC scene control), and the new dock "MIDI Director" (MIDI learn to OBS hotkeys).
- Windows-only packaging for both plugins.
