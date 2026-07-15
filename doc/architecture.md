# Mango — Architecture

*Reference for development sessions. Kept in sync with the code*

Mango is an FX-Mechanics JUCE audio plugin (VST3 / AU / Standalone, MIDI
input): a **modular sound glitcher/mangler** built around a multi-lane rubber
step sequencer. Each lane drives one effect; while the playhead is inside a
block on a lane, that lane's effect processes the audio. Lane order (top to
bottom) is the processing order. All randomness is seeded and reproducible.

Repo: `/home/doare/src/Mango` · JUCE expected as sibling `../JUCE` ·
[FxmeTools](https://github.com/odoare/FxmeTools) as submodule `lib/FxmeTools`
(JUCE module, namespace `fxme`). Sister projects used as references:
Spread (project template, TopBar origin), Neorix (mini-language + threading
patterns), FxmeFX (Tube saturation origin), Gloubiboulga (formant origin).

> **Workflow note:** the user commits and pushes himself — never run
> `git commit` / `git push` (see project memory). Shared, generally reusable
> functionality goes into the FxmeTools submodule with API docs; Mango-only
> logic stays in `Source/`.

---

## 1. Feature summary

- **Variable lane count, 1–8** (the `numlanes` parameter, default 4, driven
  by −/+ buttons in the globals). Architecturally there are always eight lanes
  (identities 0–7) with parameters/engines/state pre-built; `numlanes` sets
  how many **rows** are shown and processed. Hidden rows keep sequencing
  silently (like mute) so re-showing them is seamless; solo only counts
  among visible rows. Each lane has: a selectable effect type, mute (M) /
  solo (S), a bus-start switch (B — see the buses paragraph in §3),
  up/down reorder arrows, and a rubber sequencer strip on a **shared grid**
  (one step size 1/16…1/1 + step count 1–64 for the whole plugin, max 64
  steps). Lanes are coloured by the bus they belong to.
- **Effects** (one active instance per lane; every type pre-built per
  lane so switching is allocation-free and every type keeps its settings):

  | Type | DSP | Lane parameters (`l<i>_` prefix) |
  |---|---|---|
  | Gater | open(dur)/closed(dur) cycles, starts open | `gate_`: 7 duration weights, `att`, `rel`, `attcurve`, `relcurve` |
  | Grain | records a grain at block entry, loops it (`fxme::GrainLooper`/channel) | `grain_`: 7 weights, `fade`, `att`, `attcurve`, `rel`, `relcurve` |
  | Delay | feedback delay (`fxme::DelayLine`/channel), buffer persists across blocks; feedback reaches 0.999, a damping lowpass sits in the loop and the time glide is settable (1–50 ms), so `dur=mididur fb=0.99` is a Karplus-Strong style resonator | `dly_dur` (s), `dly_fb`, `dly_damp`, `dly_porta` (ms) |
  | Dist | tube saturation (`fxme::Saturator`/channel) | `dist_model/drive/bias/sag/mix` |
  | Filter | LP/HP sweep f0→f1 or formant vowel glide, ramp repeats at a drawn rate | `flt_`: 7 weights, `mode`, `q`, `f0`, `f1`, `v0`, `v1` |
  | Quant | lo-fi: bit crusher (`fxme::BitCrusher`) + sample & hold decimator (`fxme::Downsampler`/channel, hold phase restarts at block entry — but not on live-tweak re-enters — so passes reproduce) + wet/dry mix | `qnt_bits` (1–24), `qnt_down` (÷1–64), `qnt_mix` |
  | Ring | ring modulator: sine carrier glides exponentially f0→f1 over a drawn tempo-synced ramp, repeating for the block; amount 0–1 blends clean → full ±1 modulation (`x·(1−amp+amp·sin)`); carrier phase restarts at block entry (not on re-enters) so passes reproduce; one carrier feeds all channels | `ring_`: 7 weights, `f0`, `f1` (0.5 Hz–10 kHz), `amp` |
  | Rev | reverser: records drawn-duration slices and plays each backwards (while slice k records, slice k−1 plays reversed; the first slice of a block passes through); `fade` = 0–0.5 slice fraction faded at the seams; slice grid restarts at block entry (not on re-enters) so passes reproduce; pure sample copy, no interpolation | `rev_`: 7 weights, `fade` |
  | Freeze | spectral freeze (`fxme::SpectralFreeze`/channel, WDL FFT): captures one 2048-sample window at block entry (passed through while recording, so blocks shorter than ~43 ms stay dry), then random-phase resynthesis of its magnitude spectrum — Hann/75% OLA, one iFFT per 512-sample hop (4 at the capture→wash switch) — sustains a static wash; phases keyed on (seed, lane, block, channel) with the frame counter restarting at entry, so passes reproduce and the two channels decorrelate into a wide image | `frz_mix` |

- **Weighted random durations** (gate rate, grain length, filter ramp,
  ring glide, reverse slice): the
  user weights P(1/4..1/32) × P(straight/triplet/dotted); the actual duration
  is drawn at block entry. **Draws are a pure function of (seed, lane
  identity, blockId, drawIndex)** — never of time or loop pass — so every
  pattern pass plays exactly what the block visuals display; the seed
  re-rolls everything. (A per-pass `loopIndex` existed early on and was
  removed: the user expects loop passes to repeat identically.)
- **Attack/release envelopes** (gater open phase; each individual grain
  repetition — never the block): lengths are 0–1 fractions of the shaped
  duration; if att+rel > 1 they share it proportionally
  (`normaliseAttackRelease`, e.g. 1 + 0.5 → 2/3 + 1/3). Curves 0–1: 0 slow,
  0.5 linear, 1 very fast (fast release ≈ exponential decay), mapped to
  exponents by `attackGammaFor`/`releaseGammaFor` (gamma = 6^±(1−2c)).
- **Per-block override mini-language** (Neorix-style): block strings like
  `dur=0.125 fb=0.6`, `dur=mididur/2`, `v0=a v1=u`. `mididur` = period (s)
  of the last MIDI note-on. All lane parameters are reachable:
  `dur fb damp porta att rel attcurve relcurve q f0 f1 v0 v1 bits down
  drive bias sag mix model mode fade amp w4 w8 w16 w32 wstr wtrip wdot`.
- **Globals**: dry/wet, seed (0–99999), step size, num steps, lane count
  (−/+ buttons).
- **Live updates**: any parameter change refreshes the sounding block in
  place immediately (same draw, new values, phase preserved) — no transport
  restart needed.

## 2. Source layout

```
CMakeLists.txt                Spread-derived; juce_add_module(lib/FxmeTools/FxmeTools)
                              (no fxmetools_attach: Mango does not use WDL/FirFilter);
                              PLUGIN_CODE MNGO, COMPANY FX-Mechanics, MANUFACTURER FXME
Source/
  PluginProcessor.{h,cpp}     APVTS, MIDI->mididur, grid listener, state save/load
  PluginEditor.{h,cpp}        layout, selection flow, panel switching, ChangeListener
  ParamIDs.h                  mng::pid — globals + lanePrefix(i)="l<i>_", laneType(i)
  Theme.h                     mng::theme — colours, laneColour(i), styleKnob/styleCombo
  Dsp/
    MangoEngine.{h,cpp}       the core (see §3)
    EffectBase.h              effect interface, BlockContext, override helpers,
                              kCurveRange/attackGammaFor/releaseGammaFor,
                              normaliseAttackRelease
    EffectTypes.h             EffectType enum + display names (extension point)
    DurationWeights.h         the 7 weight params + resolveTable(ctx, weights)
    OverrideParser.h          the mini-language (JUCE-free; see §5)
    Effects/*.h               the EffectBase implementations (header-only)
  Components/
    LaneRackComponent.h       ruler + lane rows (LaneHeader + LockedRubber) + block painter
    BlockGraphics.h           per-effect visuals drawn inside blocks (see §7)
    EffectPanel.h             generic (lane × type) control panel — 48 pre-built
    BlockTextPanel.h          override-string TextEditor (red outline on parse error)
  Assets/logo686.png          FX-Mechanics logo (BinaryData)
Tests/
  UnitTests.cpp               JUCE-free pieces (parser, detrand, weights, FxmeTools DSP)
  RenderTest.cpp              offline processBlock renders + GUI snapshot mode (see §9)
doc/architecture.md           this file
```

## 3. MangoEngine (Source/Dsp/MangoEngine.{h,cpp})

The core. Owns:

- `std::array<Lane, numLanes(8)> lanes` — **indexed by lane identity**. Each `Lane`:
  `fxme::StringSequencer seq` + `fxme::SequencerEngine engine`
  (`setEnterEmptyBlocks(true)` — Mango blocks fire even with empty strings) +
  all pre-built `EffectBase` instances (one per type) + raw param pointers
  (type/mute/solo) + `active`/`activeBlockId`.
- `order` — the display/processing permutation (row → identity). Reordering
  only permutes this; parameters, sequences, draws stay with the identity.
- the parsed-override map, one `juce::CriticalSection seqLock`, a dry buffer
  and a bus working buffer.

**Buses** (up to `numBuses` = 4, parallel): the visible rows group into
contiguous buses in display order — row 0 always starts bus 0, and every
row whose `l<i>_busstart` switch is on opens the next bus (switches beyond
the fourth bus, and switches on hidden rows, are ignored). Each bus
processes its own copy of the dry input through its rows serially, and the
bus outputs are **summed** (not averaged) before the global dry/wet: one
bus is bit-identical to the old serial chain, and an idle bus passes a
copy of the dry input (so two all-idle buses output 2× the input — the
parallel-rack convention). `busMapByLane()` (message thread, locks
briefly) gives lane→bus for the GUI, which colours every lane by its bus
(`theme::busColour`, 4 colours) — headers, blocks and panels re-accent
live when the map changes (rack timer → `onBusMapChanged`).

**processBlock flow** (`process()`): read transport → publish bpm → on first
call after `prepare()` start the engines (deferred so the first draws use the
real host bpm, not a stale default) → on ppq jump (|ppq − accumulated| >
1e-3) call `engine->relocate(ppq)` on every lane — relocate (FxmeTools)
always exits + re-enters even into the same block, so loop wraps restart
phases cleanly → sync effect types → per-lane param-version check (see
below) → compute the row→bus map → advance in **≤32-sample sub-blocks**:
per sub-block, first every lane's `engine->advance(deltaBeats, seq)`
(enter/exit callbacks fire here), then per bus: copy the dry chunk into
the bus buffer, run its active & audible rows serially on it, add it into
the (cleared) output chunk → free-run at host bpm (120 fallback) when the
transport is stopped → global dry/wet mix → publish GUI atomics (playhead
step, active block, bpm).

**Lane count + mute/solo**: audible = row < `visibleLaneCount()` && !mute &&
(no solo among visible rows || solo). Bypassed/hidden lanes still advance
and fire enter/exit (sequencing state stays deterministic); they just skip
`process()`.

**Live parameter reactivity**: the processor listens to *every* APVTS
parameter; `parameterChanged` (any thread) routes: `stepsize/numsteps` →
AsyncUpdater → `setGrid` (message thread); `seed` → bump all lane versions;
`l<i>_*` → bump lane i's version (`noteLaneParamsChanged`). In `process()`,
a changed version re-enters the lane's active block with
`BlockContext::isReEnter = true`: same draw inputs, new parameter values.
Effects preserve running phase on re-enter (gater keeps `pos`, grain only
re-triggers if the resolved duration changed, filter keeps the ramp).

**Threading contract** (documented in the header): one lock guards the lane
sequencers, the order and the override map. The audio thread holds it for
the whole DSP section; every message-thread critical section must be tiny
and allocation-free — `rebuildOverrides()` parses *outside* the lock and
swaps the map inside. GUI sequencer edits go through `LockedRubber`
(SequencerRubber subclass taking the lock around mouse/key handlers).

**BlockContext** handed to `onBlockEnter`: lane identity, blockId, seed,
sampleRate, bpm, mididurSeconds (sampled at entry), `overrides` pointer
(nullptr = none/parse error), `isReEnter`.

## 4. Parameters & state

- ~570 APVTS parameters, all pre-declared (IDs frozen): 5 globals + per lane
  `type`, `mute`, `solo`, `busstart` + every effect type's set, ids
  `l<i>_<fx>_<name>`
  (FxmeFX `addParameters(prefix)` pattern — each effect class has static
  `addParameters` + member `bindParameters`).
- **Not** parameters: block data. `getStateInformation` appends a `MangoSeq`
  child to the APVTS tree: `laneOrder` + per lane `<Block id start len text/>`.
  Restore uses `StringSequencer::addBlockWithId` (FxmeTools addition) so
  **block ids survive sessions** — required because ids seed the draws.
  Grid params are applied before blocks on load. `sendChangeMessage()` tells
  the editor to reload.

## 5. Override mini-language (Source/Dsp/OverrideParser.h)

JUCE-free, strict, all-or-nothing: space-separated `key=expr`;
`expr := number | mididur | mididur*N | mididur/N | N*mididur`; vowel keys
also accept `a e i o u`. `Expr` is a flat POD (Const | MididurScaled) the
audio thread evaluates without allocation. Up to 16 assignments/block.

**Units convention**: a plain-number `dur` is a fraction of a whole note
(0.125 = eighth) resolved against the tempo (`overrideDurSeconds`); any
`mididur` expression is a time in seconds. Other keys are in the parameter's
native unit. Effects read only the keys they understand; a `dur` override
bypasses the weighted draw; weight keys (`w4`…`wdot`) override the table
before the draw (`resolveTable`).

**Flow** (Neorix pattern): parse on the message thread on every content
commit / state load → fresh `unordered_map<(lane<<32|blockId), ParsedOverrides>`
swapped in under the lock. Parse error → entry omitted, raw string kept,
TextEditor outline red. `blockHasParseError()` = non-empty content with no
map entry.

## 6. Determinism

`u = fxme::detrand::u01 (seed, laneIdentity, blockId, drawIndex)` (stateless
splitmix64 hashing, FxmeTools `dsp/DeterministicRandom.h`), then
`WeightedDurationTable::drawBeats(u)` (`midi/NoteDuration.h`; weights =
base×mod normalised, all-zero → straight quarter). drawIndex is 0 for the
single per-entry draw (reserved for future multi-draw effects). Consequences:
same session + seed → bit-identical renders; loop passes identical; block
visuals can show the *exact* draw (they recompute the same hash on the
message thread).

## 7. GUI (fixed 1000×650)

- `fxme::TopBar` (54 px, logo from BinaryData) · `fxme::FxmeLookAndFeel` ·
  `fxme::FxmeSlider` knobs (right-click value entry; styleKnob = dark disc +
  per-control accent).
- **LaneRackComponent**: step ruler + the visible rows in `order`; each row =
  LaneHeader (accent swatch, ▲▼ arrows acting on the *row*, type combo,
  M/S MiniToggles — custom-painted, stock LnF text doesn't fit 20 px) +
  LockedRubber. Rubbers call `setMinPixelsPerStep(1)` so the whole pattern
  always fits the width — the lanes, ruler and playhead share one mapping at
  any step count (the FxmeTools default of 20 px/step would scroll). A 30 Hz
  timer feeds playhead/active-block atomics; every 3rd tick repaints the
  strips so **block visuals follow knob changes** even when stopped.
- **Block visuals** (BlockGraphics.h, drawn by the rack's BlockPainter, all
  in beats so tempo-invariant): gater = exact envelope curve (pass-0 draw +
  overrides + shared curve helpers); grain = the same envelope mirrored
  around the centre (filled lens per repetition); delay = decaying vertical
  lines spaced by the delay time (uses published bpm); dist = tanh-squashed
  sine; quant = staircase sine; filter = repeating ramp in a distinct dark
  colour. Painters parse the block's own override string. Override text is
  drawn at 12.5 px with a dark backing.
- **Rubber gestures** (fxme::SequencerRubber): drag empty space = create;
  body click = select (click again = deselect, resolved on mouse-up); body
  drag = **move block** (`StringSequencer::moveBlock`, walls against
  neighbours); edge drag = resize; **alt-click = delete** (mouse-only
  alternative to the Delete key, added because hosted Linux windows lose the
  keyboard-focus race); right-click = clear content; Delete key = delete.
- **Right column**: global knobs + step-size combo / the selected lane's
  EffectPanel (48 pre-built, visibility-switched; weight mini-knobs) /
  BlockTextPanel (3-line multiline field: Return commits and leaves the
  field, Ctrl/Cmd+Return inserts a newline, focus loss commits too).
- **Selection flow**: rubber `onBlockSelected(lane, id)` → editor deselects
  other rubbers, shows `panels[lane][currentType]`, loads block text. A 10 Hz
  editor timer follows lane-type changes; processor ChangeBroadcaster resets
  everything after state loads.
- `fxme::TextEntryFocusFixer` (declared last in the editor): Linux focus
  battles — every click starts a bounded 10 Hz OS-focus claim (~0.8 s for
  plain clicks, ~2 s refreshed while a caret is active), Return commits &
  leaves single-line fields, Escape reverts.

## 8. FxmeTools additions made for Mango (lib/FxmeTools, branch `mango-additions`)

All header-only, `namespace fxme`, JUCE-free in dsp/midi. Registered in the
umbrella `FxmeTools/FxmeTools.h` (module v0.0.3):

- `dsp/Waveshapers.h` — tube curves lifted from FxmeFX (triode, halfWave,
  classAB, tanhDrive). FxmeFX to be refactored to include it (not done).
- `dsp/Saturator.h` — curves + sag rail + DC blocker (per channel).
- `dsp/FormantFilter.h` — 2 formants on `fxme::Biquad` bandpass; vowels
  A/E/I/O/U (F1/F2: 800/1200, 400/2200, 300/2500, 500/1000, 300/800; bw
  80/100 Hz); `setVowelBlend(from, to, t)` at control rate.
- `dsp/BitCrusher.h`, `dsp/Downsampler.h` (per-channel sample & hold
  decimator, fractional factors, ÷1 bit-transparent), `dsp/DelayLine.h`
  (interp., settable time smoothing 0.5–500 ms, fb ≤ 0.999, damping
  lowpass in the feedback path),
- `dsp/SpectralFreeze.h` — FFT freeze on the WDL real FFT (WDL is
  FxmeTools' existing submodule; like FirFilter it is NOT in the module
  umbrella — include it explicitly and compile `WDL/fft.c` into the
  target, which Mango's CMake does for the plugin and both test apps).
  Capture window + random-phase OLA resynthesis; deterministic phases via
  `setIdentity`; `prepare()` self-calibrates the FFT round-trip gain, so
  it is immune to the library's scaling convention.
  `dsp/ArEnvelope.h` (unused by Mango currently), `dsp/DeterministicRandom.h`,
  `midi/NoteDuration.h`.
- `dsp/GrainLooper.h` (pre-existing) gained: `setAttack(frac, gamma)` /
  `setRelease(frac, gamma)` per-instance envelopes on top of the seam fades,
  and **the first (recording) grain shapes its live pass-through with the
  same `instanceEnv()`** — without this the first grain of every block
  played unshaped (user-reported bug). First seam is sequential, not
  overlapped (documented one-off dip).
- `midi/StringSequencer.h` gained `addBlockWithId` (id-stable restore) and
  `moveBlock` (whole-block move with walls).
- `midi/SequencerEngine.h` gained `setEnterEmptyBlocks`, `relocate()` (always
  exit+re-enter on transport jumps), and a range-based exit check (blocks
  moved/resized from under the playhead exit at the next step).
- `components/SequencerRubber.h` gained `setMinPixelsPerStep`, body-drag
  move, alt-click delete.
- `components/TopBar.h` (generalised from Spread, logo passed in),
  `components/TextEntryFocusFixer.h` (new; see §7).

## 9. Testing (`-DMANGO_BUILD_TESTS=ON`)

- **MangoTests** (`Tests/UnitTests.cpp`, no JUCE): parser (incl. weight keys,
  errors, all-or-nothing), detrand distribution/weighted choice, duration
  table (incl. the spec's 1/1.6 example), ArEnvelope, BitCrusher, DelayLine,
  Saturator bounds, GrainLooper attack/release incl. the first-grain window,
  StringSequencer addBlockWithId/moveBlock, SequencerEngine empty-block and
  moved-block-exit behaviour.
- **MangoRenderTest** (`Tests/RenderTest.cpp`): an independent console app
  compiling the plugin sources (linking the plugin lib would duplicate JUCE
  symbols) that renders through the real `processBlock`: gate timing at the
  120 free-run, `dur=0.125` override, bad-string rejection, bit-exact
  same-seed renders / different-seed divergence, state round-trip (structure
  + audio), host-tempo sync via a FakePlayHead (the engines start on the
  first process() for this reason), live weight change mid-block, release
  curve RMS values (0.20/0.41/0.61 analytic match), att/rel proportional
  sharing (peak at 2/3), mute/solo, pass repeatability (recomputes the
  expected draw), loop-jump re-entry (dotted quarter exposes stale phase).
  `MangoRenderTest <path.png>` instead dumps GUI snapshots (editor + gater +
  filter panels; pumps the message loop so the async grid update applies).
- Verify cycle: `cmake --build build -j && ./build/MangoTests &&
  ./build/MangoRenderTest_artefacts/Release/MangoRenderTest`; snapshots for
  anything visual; standalone smoke test (`timeout 5 ...` exit 124 = ran).

## 10. Conventions & invariants

- Lane **identity** (0–7) vs **row** (display position): parameters, ids,
  draws, colours attach to identity; only `order` and layout use rows.
- Draw inputs must never gain a time-dependent component (see §6).
- No allocation/parsing under `seqLock`; audio thread never touches strings.
- Effects read live knob values from APVTS atomics inside `process()`,
  except values fixed by a block override (resolved at entry, live-skipped).
- The block visuals and the DSP must share helpers (`normaliseAttackRelease`,
  `attackGammaFor/releaseGammaFor`, `resolveTable`, the u01 call shape) so
  the picture can never drift from the sound.
- New effect type checklist: subclass `EffectBase` (+ static
  `addParameters(params, lanePrefix, nameP)` + `bindParameters`), extend
  `EffectTypes.h`, the factory + bind switch in `MangoEngine.cpp`, a case in
  `EffectPanel`'s constructor, a visual in the rack's `paintEffectVisual`,
  and (if it draws durations) reuse `DurationWeights`.
- Language: new keys extend `OvKey` + `keyNames` (parser is order-matched
  arrays) and are documented in README.
- GUI reads of sequencer step size / num steps without the lock are accepted
  benign races (transient one-frame glitches at worst).

## 11. Known caveats / deferred

- Delay tail hard-cuts at block exit (glitch-appropriate; buffer persists so
  re-entry continues it).
- Tempo changes mid-block keep entry-time sample counts until the next entry.
- Enter/exit quantised to ≤32-sample sub-blocks (~0.7 ms; effects keep
  sample-accurate internal timing).
- `gate_att`/`gate_rel` ranges changed 0–0.25 → 0–1 after the first builds;
  no state-version migration exists (old saved values reinterpret on the new
  scale). Add a version bump if sessions from before that change matter.
- 64-step cap (StringSequencer clamp), grid shrink truncates blocks.
- FxmeFX still carries its private copies of the tube curves.
- If keyboard input still dies in a specific DAW after the focus-fixer
  battles, it's host keyboard routing (REAPER: "Send all keyboard input to
  plugin").
