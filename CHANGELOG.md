# Changelog

Director is the suite release: it bundles Palette Director and Scene Director, which
keep their own versions and their own changelogs.

## 1.1.0

Suite = Palette Director 1.2.0 + Scene Director 2.0.0.

| Component | Version | Changelog |
|---|---|---|
| Palette Director | 1.2.0 | https://github.com/EnZhm/palette-director/blob/master/CHANGELOG.md |
| Scene Director | 2.0.0 | https://github.com/EnZhm/scene-director/blob/main/CHANGELOG.md |

What it adds:

- One Windows x64 zip with both plugins, `director-1.1.0-windows-x64.zip`; both folders
  are copied into `C:\ProgramData\obs-studio\plugins\`.
- Scene Director 2.0.0 (the suite's second component, renamed and rewritten): it follows
  Palette Director's beat clock and sends MIDI **Next** / **Previous** to a virtual MIDI
  port, so a Synesthesia playlist switches on the beat. The messages go out on a
  breakdown start (on the breakdown crossfade) and on the drop downbeat, and by hand from
  the new **Scene Director** dock or from the OBS hotkeys "Scene Director: Next scene" /
  "Previous scene". It is a major release of that component and drops what its 1.x
  releases did instead; the details are in its own changelog (linked above).
- Scene Director's dock shows the MIDI out link, the automatic-switch triggers, the
  cooldown and Scene lead, the Next and Prev message editors (Note or Control change,
  channel, number) and a log of every switch.
- Palette Director 1.2.0 is unchanged in this suite release.
- The **MIDI Director** dock (part of Scene Director) still maps MIDI notes and CCs from
  a controller to any OBS hotkey in the suite, and its hotkey list now includes the new
  Scene Director hotkeys.
- Windows-only packaging for both plugins.

## 1.0.0 - first release

Suite = Palette Director 1.2.0 + the suite's other component at version 1.1.0.

| Component | Version | Changelog |
|---|---|---|
| Palette Director | 1.2.0 | https://github.com/EnZhm/palette-director/blob/master/CHANGELOG.md |
| The second component | 1.1.0 | superseded: that component is Scene Director 2.0.0 today (https://github.com/EnZhm/scene-director/blob/main/CHANGELOG.md) |

What it added:

- One Windows x64 zip with both plugins, `director-1.0.0-windows-x64.zip`; both folders
  were copied into `C:\ProgramData\obs-studio\plugins\`.
- Palette Director: filter "Palette Lock", dock "Palette Director", dock "Music"
  (BPM, beat lights, section, level and bass meter, 64-beat graph, audio controls,
  Phrase is now / Drop now / Force breakdown / Tap tempo / Lock BPM / Nudge), and the
  read-only beat clock for other plugins.
- The second component: its source and dock at the time, plus the new dock
  "MIDI Director" (MIDI learn to OBS hotkeys). That component is Scene Director 2.0.0
  now, with a different feature set - see its changelog for what changed.
- Windows-only packaging for both plugins.
