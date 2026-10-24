# Porting features from `10inch-1280x800` into `main`

Decision (2026-10-24): `main` stays the published default. Generic features from the 10-inch line are ported deliberately; the 1280x800 layout is **not** merged wholesale.

## Structural constraints

- `main` has **no Play screen** (`mActiveNav` 0–6 only; Play is 7 on the 10-inch line). Any UI gated on Play cannot be ported without first adding the Play screen to `main`.
- `main` hardcodes **1024×600**; the 10-inch line uses 1280×800 and `HardwareDisplay`. Layout/rotation/geometry commits are 10-inch-only.
- `main` is an ancestor of `10inch-1280x800` after the 2026-10-24 merge, so both histories are intact.

## Classification of the 37 non-merge commits

| Class | Commits | Action |
|---|---|---|
| **10-inch only** | `ebbde6e`, `058feeb`, `f311069`, `44fbe19`, `180d86c`, `7be4a7e`, `c476b5d`, `6a478d8` | Skip |
| **Already on main** | `37671d7` (= `4a57049`) | Skip |
| **Generic** | `d2bcc29`, `e786cb8`, `3ea0622`, `4f4e00a`, `af5ec32`, `c3938eb`, `24b6156`, `897a7dc`, `cd034e2`, `06d81a2`, `16211a4`, `afd98d6`, `72e3b4a`, `005ab67`, `16743dc`, `9fce542`, `631e6cc`, `30809eb`, `88239c5` | Cherry-pick / port |
| **Mixed** | `8bf731b`, `7055067`, `47f06d5`, `7fd774d`, `42ef31a`, `6aeb038`, `9356659`, `5e82705`, `cad2a44` | Partial port (see below) |

## Recommended order

### Phase A — audio plumbing (foundation)

1. `e786cb8` — pause capture, remove renderInput lock contention (skip the 1024-buffer hunk).
2. `3ea0622` — USB audio auto-detect, settle delay.
3. `4f4e00a` — remove audio logging, direct device selection (partial).
4. `af5ec32` — capture strictly on demand.
5. `c3938eb` — MIDI before audio, device-removal recovery.
6. `24b6156` — S16_LE hardware output.
7. `897a7dc` — active input device auto-select (main keeps its mic/line-in dropdowns as fallback).
8. `cd034e2` — log detected audio devices at startup.

### Phase B — UI/engine fixes

9. `d2bcc29` — idle label formatting + 60 Hz pacing.
10. `06d81a2` — sampler end-marker scaling, self-triggering scrub.
11. `16211a4` — save/load flag contamination + keyboard confirmation.
12. `7055067` — **partial**: Settings drum-row note labels/options only; skip Play pad code.

### Phase C — per-voice modulation (engine only)

13. `42ef31a` — **partial**: `AudioEngine` + engine `setVoiceMod`/`setModRouting`; skip Play voice-link UI.
14. `6aeb038` — **partial**: engine fixes and routing guards; skip Play hunks.
15. `9356659` — **partial**: parameter names/options + per-voice Morph/Fold/Drive; skip Play pad hunk.

> `main` has no Play screen, so the user-facing voice-link toggle cannot be ported yet. The engine APIs are still worth landing for MIDI pad modulation.

### Phase D — drums and envelopes

16. `5e82705` — **partial**: `allNotesOff(int)`, `Adsr.h` time-based envelopes, `AnalogDrumEngine.h` 808 rework, sampler playhead/audition/updater. Skip split pads and `HardwareDisplay`.
17. `afd98d6` — analog drum decay/silence fix.
18. `72e3b4a` — 808-style decay ranges + audition buttons.
19. `005ab67` — envelope cutoffs across SoundFont/FM/Drum.

### Phase E — sampler

20. `cad2a44` — **partial**: slice-scrub engine/MIDI; skip Play pad hunks.

### Phase F — hardware / power / idle

21. `8bf731b` — **partial**: brightness/CPU temp/reboot/shutdown UI; replace `HardwareDisplay` calls with main's `setBacklightPower` + sysfs/system calls.
22. `47f06d5` — **partial**: screen timeout/sleep/wake; skip Settings Tab 1 layout.
23. `7fd774d` — **partial**: engine idle/silence gating; skip all UIManager layout hunks.

### Phase G — melody (last)

24. `16743dc` — Melody In transcriber (new `MelodyTracker.*`; adapt to 1024×600).
25. `9fce542` — capture on modal open, stereo mix, wider range.
26. `631e6cc` — SAMPLE source mode.
27. `30809eb` — gain, YIN sensitivity, temporal resolution.
28. `88239c5` — detection side panel (HP/LP, confidence, speed, tolerance).

## Method

- Work on `feature/port-from-10inch` (branched from `main`).
- Never `git cherry-pick` a MIXED commit directly. Use `git cherry-pick -n`, then `git restore --staged --worktree <paths>` for the 10-inch-only files.
- For new files (e.g. `MelodyTracker.*`), `git checkout <hash> -- path` is clean.
- Build and smoke-test on the 1024×600 device after each phase before moving on.

## Expected conflict hotspots

| File | Risk | Reason |
|---|---|---|
| `src/main.cpp` | High | 10-inch uses `HardwareDisplay`; main uses SDL window + `PeepEvents` |
| `src/ui/UIManager.cpp` | High | ~9k-line layout divergence; Play screen absent |
| `src/AudioEngine.cpp/.h` | Medium-high | Missing per-voice command enum and `allNotesOff(int)` |
| `src/engines/*.h` | Medium-high | Per-voice APIs and `Adsr.h` rework |

## Do not port yet

- 20/20 split pads, Play screen, `HardwareDisplay`, rotation/touch calibration.
- Any 1280×800 geometry or Settings Tab 1 redistribution.
- Melody tracker before Phase G.
