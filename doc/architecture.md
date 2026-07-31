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
  by −/+ buttons in the bus bar). Architecturally there are always eight lanes
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
  | Grain | records a grain at block entry, loops it (`fxme::GrainLooper`/channel); seam crossfade fixed at 15 ms (the `grain_fade` param was removed as not useful) | `grain_`: 7 weights, `att`, `attcurve`, `rel`, `relcurve` |
  | Delay | feedback delay (`fxme::DelayLine`/channel), buffer persists across blocks; feedback reaches 0.999, a damping lowpass sits in the loop and the time glide is settable (1–50 ms), so `dur=mididur fb=0.99` is a Karplus-Strong style resonator | `dly_dur` (s), `dly_fb`, `dly_damp`, `dly_porta` (ms) |
  | Dist | tube saturation (`fxme::Saturator`/channel); `dist_gain` = ±24 dB makeup on the saturated signal, before the mix (loudness depends on input level — manual compensation) | `dist_model/drive/bias/sag/gain/mix` |
  | Filter | LP/HP sweep f0→f1 or formant vowel glide, ramp repeats at a drawn rate | `flt_`: 7 weights, `mode`, `q`, `f0`, `f1`, `v0`, `v1` |
  | Quant | lo-fi: bit crusher (`fxme::BitCrusher`) + sample & hold decimator (`fxme::Downsampler`/channel, hold phase restarts at block entry — but not on live-tweak re-enters — so passes reproduce) + wet/dry mix | `qnt_bits` (1–24), `qnt_down` (÷1–64), `qnt_mix` |
  | Ring | ring modulator: sine carrier glides exponentially f0→f1 over a drawn tempo-synced ramp, repeating for the block; amount 0–1 blends clean → full ±1 modulation (`x·(1−amp+amp·sin)`); carrier phase restarts at block entry (not on re-enters) so passes reproduce; one carrier feeds all channels | `ring_`: 7 weights, `f0`, `f1` (0.5 Hz–10 kHz), `amp` |
  | Rev | reverser: records drawn-duration slices and plays each backwards (while slice k records, slice k−1 plays reversed; the first slice of a block passes through); `fade` = 0–0.5 slice fraction faded at the seams; slice grid restarts at block entry (not on re-enters) so passes reproduce; pure sample copy, no interpolation | `rev_`: 7 weights, `fade` |
  | Freeze | spectral freeze (`fxme::SpectralFreezeMulti` — Mango's effect is only the APVTS adapter; WDL FFT): captures one 2048-sample window at block entry (passed through while recording, so blocks shorter than ~43 ms stay dry), then random-phase resynthesis of its magnitude spectrum — Hann/75% OLA, one iFFT per 512-sample hop (4 at the capture→wash switch) — sustains a static wash; phases keyed on (seed, lane, block, channel), the frame counter running across retriggers, so passes reproduce and the two channels decorrelate into a wide image. **Retrigger**: `frz_` weights (default straight 1/4) draw a grid on which the wash is re-captured from a rolling input history (seamless — `SpectralFreeze::retrigger` keeps the wash playing and swaps the spectrum, the OLA crossfading it); a boundary at/after the block end never fires, so a block ≤ the interval is a single capture (old sessions unchanged). The wash's mix fades to 0 over the block's last ~10 ms (`ctx.blockLengthSamples`) so the end returns to dry without a click. `frz_width` blends the two wet channels (L' = a·L + b·R, mirrored, a:b = (1/2+w/2):(1/2−w/2) normalised to a²+b²=1 — equal power over the sweep since the washes are incoherent: 1 wide, 0 mono) | `frz_mix`, `frz_width`, `frz_` 7 weights |
  | Aux | rhythmic aux send: the gater's draw/envelope/curves verbatim, but the shaped signal is *added* to the plugin's two aux stereo outputs (`aux_send1`/`aux_send2`) instead of being cut, while the main path is scaled by a flat `aux_pass` (1 = transparent, a send on top; 0 = the block leaves the main chain). The tap is the bus signal at the lane's position, before the bus volume/pan. Aux buffers arrive via `EffectBase::setAuxBuffers`, set by the engine once per processBlock; nullptr = that bus is disabled in the host and the send is dropped | `aux_`: 7 weights, `att`, `rel`, `attcurve`, `relcurve`, `send1`, `send2`, `pass` |
  | Pan | rhythmic panner: the gater's clock (same weighted draw), but each step lands on one of three positions -1/0/+1 rather than alternating two gains. `pan_mode` picks the sequence — `Cycle ->` (left, centre, right, period 3), `Cycle <-` (its mirror), `Cycle <->` (left, centre, right, centre: period 4, turning round rather than jumping across the image) or `Random` (drawn per step; the `1` coordinate keeps that stream clear of the block's duration draw at 0). `pan_glide` is the fraction of a step spent travelling to the new position, `pan_mix` the usual dry/wet as per-channel gain 1−mix+mix·g. Balance law, as the buses use; a mono main bus passes through untouched. `panStateAt()` in PannerEffect.h is shared with the block visual | `pan_`: 7 weights, `mode`, `glide`, `mix` |

- **Per-effect mix**: every effect has a wet/dry `<fx>_mix` parameter
  (default 1, override key `mix`) — except the ring modulator, whose
  `amp` plays that role, and the aux send, whose `pass` does. Gater applies it as gain = 1−mix+mix·gate; the
  delay scales its added delayed signal; the others blend dry/wet.
  Filters/loopers keep running at full level so their state stays
  continuous — mix only blends the output.
- **Weighted random durations** (gate rate, grain length, filter ramp,
  ring glide, reverse slice, aux send rate, pan step, freeze retrigger): the
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
  `dur=0.125 fb=0.6`, `dur=mididur/2`, `f0=midifreq`, `v0=a v1=u`.
  `mididur` = period (s) of the last MIDI note-on; `midifreq` = its
  frequency in Hz (1/mididur), same `*N` `/N` `N*` forms. All lane
  parameters are reachable:
  `dur fb damp porta att rel attcurve relcurve q f0 f1 v0 v1 bits down
  drive bias sag gain mix width model mode fade amp aux1 aux2 pass
  glide w4 w8 w16 w32 wstr wtrip wdot`.
- **Globals**: dry/wet, seed (0–99999), step size, num steps, bus routing
  mode + per-bus volume/pan (see §3 buses), lane count
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

**Buses** (up to `numBuses` = 4): the visible rows group into contiguous
buses in display order — row 0 always starts bus 0, and every row whose
`l<i>_busstart` switch is on opens the next bus (switches beyond the
fourth bus, and switches on hidden rows, are ignored). Each bus runs its
rows serially, then applies its own output volume (`bus<n>_vol`, a plain
linear gain — 0 mutes the bus, it does **not** fall back to its input) and
pan (`bus<n>_pan`, −1..1 balance law, stereo only) — both bit-transparent at
defaults (vol 1, pan 0). `busmode` picks the topology
(`effectiveBusMode(mode, busCount)` in ParamIDs.h — shared by engine and
GUI; a mode needing more buses than exist falls back to parallel):

  - 0 parallel: every bus processes its own copy of the dry input;
    outputs are **summed** (not averaged) before the global dry/wet. One
    bus is bit-identical to the old serial chain; an idle bus passes a
    copy of the dry input (two all-idle buses output 2× — the
    parallel-rack convention).
  - 1 (needs ≥3 buses): buses 1+2 are parallel feeders; their summed
    outputs are the **input** of bus 3 (its wet reference too); bus 4, if
    present, stays parallel from the plugin input.
  - 2 (needs 4 buses): buses 1–3 parallel feeders → bus 4 processes their
    mix; the output is bus 4 alone.
  - 3 (needs 4 buses) **diamond**: bus 1 processes the input, its output
    feeds buses 2 and 3 in parallel, and *their* mix feeds bus 4, which
    alone reaches the output. Two chained stages, so it needs a second
    intermediate buffer (`stageBuffer` holds bus 1's output while
    `feedBuffer` accumulates 2+3).
  - 4 (needs ≥2 buses) **fan-out**: bus 1 is a common front end whose
    output feeds every remaining bus in parallel; those are summed and bus
    1 is not heard directly. The only mode that *degrades* rather than
    falling back — with three buses it fans out to two, with two it is a
    plain 1 → 2 series.

**Aux outputs**: the processor declares two extra stereo output buses,
"Aux 1" and "Aux 2", **disabled by default** so a host (or the standalone
app) wiring only a stereo pair is unaffected; `isBusesLayoutSupported`
accepts each as disabled-or-stereo. `processBlock` works on
`getBusBuffer(buffer, false, 0)` — the engine is prepared with
`getMainBusNumOutputChannels()`, never the total, which would count the aux
channels and oversize every working buffer — and hands the engine
non-owning views of the enabled aux buses (`setDataToReferTo`, not
assignment: copying an `AudioBuffer` allocates). Aux buses are additive
destinations, so they are cleared every block; the engine passes them to
each lane's *current* effect via `EffectBase::setAuxBuffers` after the type
sync, and only `AuxSendEffect` uses them.

`busMapByLane()` / `busCount()` (message thread, lock briefly) feed the
GUI: every lane is coloured by its bus (`theme::busColour`, 4 colours),
shaded progressively towards white by the lane's position inside the bus
(`busColour(bus, depth)`) — headers, blocks and panels re-accent live when
the map or the shading depths change (rack timer → `refreshBusCache` →
`onBusMapChanged`). The **BusBar** (Components/BusBar.h, under the rack)
draws the active buses as numbered coloured boxes with feed lines per the
effective mode, cycles `busmode` with a button, and holds the per-bus
volume/pan knob pairs (visible for active buses only).

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
sampleRate, bpm, mididurSeconds (sampled at entry), `blockLengthSamples`
(the block's span at the current tempo, for effects that fade at the edge or
clock inside it — the freeze uses it), `overrides` pointer (nullptr =
none/parse error), `isReEnter`.

## 4. Parameters & state

- ~630 APVTS parameters, all pre-declared (IDs frozen): 16 globals
  (incl. `busmode`, `bus<n>_vol/pan`, `config`, `configsync`) + per lane
  `type`, `mute`, `solo`, `busstart` + every effect type's set, ids
  `l<i>_<fx>_<name>`
  (FxmeFX `addParameters(prefix)` pattern — each effect class has static
  `addParameters` + member `bindParameters`).
- **Not** parameters: block data, the config bank and the editor's view
  state. `getStateInformation`
  appends a `MangoSeq` child to the APVTS tree (`laneOrder` + per lane
  `<Block id start len text/>`); the `MangoBank` child lives permanently in
  `apvts.state`, so sessions *and* presets carry the whole bank.
  Restore uses `StringSequencer::addBlockWithId` (FxmeTools addition) so
  **block ids survive sessions** — required because ids seed the draws.
  Grid params are applied before blocks on load. `sendChangeMessage()` tells
  the editor to reload.
- **Editor view state** (`MangoAudioProcessor::ViewState`): the selected
  block (lane identity + id) and which right-column panel is open. It lives
  on the processor as a plain member — *not* in `apvts.state` — and is
  appended as a `MangoView` child at `getStateInformation` time, exactly
  like MangoSeq. That placement is the whole point: the processor outlives
  the editor, so closing and reopening the window restores the view, the
  session carries it, and presets (which serialise `apvts.state`) never
  touch it — loading a sound must not rearrange someone's GUI. The editor
  writes it from `selectBlock` / `showConfigs` / `showPresets` and applies
  it in `restoreView()` (constructor + every ChangeBroadcaster callback),
  which drops a selection whose block no longer exists (`blockExists`, plus
  a visible-row check) after a grid change, config recall or preset load.
  The two panels are mutually exclusive in both directions, so the stored
  pair can never disagree.

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

## 7. GUI (fixed 1050×650; right column 320, bus bar under the rack)

**Colour code** (Theme.h): the identity is the fruit — a yellow → green →
red ramp, `theme::mangoAt(t)` being its single source. It appears as a
gradient where something spans the whole identity (the 2 px separation line
under the top bar via `paintMangoRamp`; the config bank's eight slots, slot
*n* sampling `mangoAt(n/7)`), and as three discrete accents that divide the
global controls by role rather than by position:

  - **yellow** (`mangoYellow`) — the mix side: dry/wet, seed, and the preset
    bar / toggle / browser.
  - **green** (`mangoGreen`) — the structure: steps knob, step-size box, and
    the bus bar's lane −/+ and routing-mode buttons.
  - **red** (`mangoRed`) — the config bank: the Configs button, the panel
    frame, its sync box and include-parameters toggle.

`paintBackground` runs the same ramp at near-black (saturation ×0.45,
brightness 0.085–0.155) diagonally: red bottom-left, green through the
middle, warm amber top-right. `theme::busColour` deliberately stays *off*
the ramp — bus colours identify content, and four hues drawn from a
three-colour ramp would separate neither from each other nor from the
chrome.

**Sequencer config bank** (8 slots): a config is a *snapshot*, stored in the
state tree (`MangoBank` child of `apvts.state`), **not** in parameters — the
only parameters are the `config` selector (1–8, automatable) and
`configsync` (Immediate / Next bar / Next pattern). Eight copies of the
~630 parameters would have meant ~5,000 host-visible parameters; generic
"lane N param M" slots would have cost the host-facing naming and broken
every existing session. What a config contains is decided by
`mng::configParamKind(id)` in ParamIDs.h, split by what a parameter
*belongs to* rather than by whether it is a "sound" control:

  - **Always — structure**: grid (`stepsize`/`numsteps`), `numlanes`, lane
    order, per-lane `type` and `busstart`, `busmode`, **`bus<n>_vol` and
    `bus<n>_pan`**, `seed`, plus the blocks (a `MangoSeq` child, the same
    format the session uses). Blocks are meaningless without their grid and
    seed; and a bus's *identity* is defined by the config (`busstart`
    decides which lanes are in it), so its level and pan must travel with
    it or a recall leaves you with a routing you never balanced.
  - **Optional — voicing** (`includeParams` flag on the bank, recorded per
    stored slot, **default off**): the per-lane effect parameter sets, and
    only those. Per-block override strings cover the same ground the other
    way round — typed rather than knob-captured, per block rather than per
    lane, and already carried inside every config since they live in the
    blocks.
  - **Never — performance**: mute, solo, global dry/wet, and the selector
    itself.

Recall flow: GUI clicks and host automation both go through the `config`
parameter (one source of truth for the active-slot indicator) →
`parameterChanged` queues `pendingRecall` → `handleAsyncUpdate` (message
thread) applies immediately, or `armRecall` starts a 60 Hz timer that waits
for the engine's `barWrapCount()` / `patternWrapCount()` atomic to advance
(bumped per sub-block in `process()`), landing the switch within ~16 ms of
the boundary. Re-clicking the active slot forces a reload ("revert to
stored"); an empty slot is a no-op and never clobbers the live setup.
**Both state-restore paths (`setStateInformation` and the preset
`onAfterLoad` hook) must cancel `pendingRecall`** — `replaceState` moves the
selector, which would otherwise overwrite the just-restored live setup with
its own stored config. `configIsModified()` compares an FNV-1a signature of
the config-relevant parameters + blocks against the one taken at the last
load/store. Store keeps one level of undo (the overwritten slot's subtree).
GUI: Components/ConfigPanel.h, in the right column's panel area (mutually
exclusive with the preset overlay).

**Presets** (`fxme::PresetManager`, AmbiRR2 pattern): the processor owns
the manager (user dir `.../Mango/Presets`, factory bank = BinaryData
`*_xml` resources — the `Source/Assets/*.xml` files, embedded by the CMake
asset glob alongside the images; the manager keeps only those whose root
tag is the APVTS state type `Parameters`, so images in the same blob are
ignored). Because presets are the plain
APVTS state and Mango's blocks are side state, the processor sets the
manager's `onBeforeSave` / `onAfterLoad` hooks (a Mango-driven FxmeTools
addition) to merge `MangoSeq` in before saving and rebuild the sequencers
(+ grid + overrides + editor notify) after loading — without them presets
would silently lose all blocks. GUI: `fxme::PresetBarComponent` + toggle
parked in the top bar via `fxme::TopBar::setRightControls` (promoted from
AmbiRR2's local TopBar), full `fxme::PresetComponent` in a PresetOverlay
covering the right column when toggled. Caveat: block edits alone don't
mark the preset dirty (the dirty tracker watches apvts.state, and blocks
live outside it until save time).

**Level meters**: four stereo taps — input, main output, aux 1, aux 2 —
metered with `fxme::VuMeter` (RMS over a 0.1 s window, atomics read by the
GUI). The processor feeds the input tap *before* `engine.process`, which
works in place, and the rest after; an aux bus the host has disabled is fed
a `silence` buffer so its bars fall away rather than freezing at their last
level. Components/MeterStrip.h lays out eight `fxme::VuMeterComponent` bars
(−60..0 dBFS, unity mark at −6) with their labels and polls at 20 Hz. All
bars share one colour: on a meter red means clipping, so aux bars sampling
the red end of the identity ramp would read as a fault.

**Splash** (`fxme::SplashOverlay`, covering the whole window): fades in the
`Assets/Splash.png` artwork over a dimmed backdrop, holds 2 s, fades out;
a click dismisses it early and it swallows mouse events while up. The
component holds no policy about when to appear — the editor shows it from
`fxme::TopBar::onLogoClicked` (the company logo and the decoration artwork
are the hit areas, recorded as they paint) and once at startup, gated by
`MangoAudioProcessor::claimSplash()`. That flag lives on the **processor**,
not the editor: the editor is destroyed and rebuilt on every window
open/close, so an editor-side flag would replay the splash each time. It is
also *not* serialised — a reloaded session is a new run and gets its splash.

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
  colour; aux send = the gate envelope filled (scaled by the larger send)
  under a dashed line at the passthrough level; panner = the stepped pan
  position with its glide ramps, from the effect's own panStateAt. Painters parse the block's own override string. Override text is
  drawn at 12.5 px with a dark backing.
- **Rubber gestures** (fxme::SequencerRubber): drag empty space = create;
  body click = select (click again = deselect, resolved on mouse-up); body
  drag = **move block** (`StringSequencer::moveBlock`, walls against
  neighbours); edge drag = resize; **alt-click = delete** (mouse-only
  alternative to the Delete key, added because hosted Linux windows lose the
  keyboard-focus race); right-click = clear content; Delete key = delete.
**Help** (`fxme::InfoButton`, `theme::styleInfo` for the shared palette):
four callouts, each placed immediately after the title it explains rather
than at a panel edge — so a title's width is measured with
`GlyphArrangement::getStringWidthInt` and the button laid out after it.
EffectPanel carries per-effect help (`helpFor(EffectType)`, kept next to the
control list so the two are edited together, and re-accented with the lane
in `setAccent`); BusBar's sits bottom-left and covers lanes, order, mute/solo
and the whole bus system; ConfigPanel's explains the bank; BlockTextPanel's
explains the override language. New effect types must add a `helpFor` case.

- **Right column**: global knobs + step-size combo / the selected lane's
  EffectPanel (48 pre-built, visibility-switched; weight mini-knobs) /
  BlockTextPanel (3-line multiline field: Return commits and leaves the
  field, Ctrl/Cmd+Return inserts a newline, focus loss commits too).
- **Override notice**: `EffectPanel::setBlockOverrides()` names the keys the
  selected block pins *and* this effect reads, in the panel's accent above
  the key reference. Without it an overridden knob just looks broken: the
  factory presets carry `dur=mididur` on delay blocks and `w16=1` on filter
  blocks, which was reported as "the Time knob does nothing" and "the
  probability knobs stopped reacting". The editor feeds it from
  `refreshPanelOverrides()` (selection, panel switch, and text commit); key
  matching is whole-token, since `att`/`attcurve`, `amp`/`damp` and
  `mode`/`model` are substrings of each other.
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
  it is immune to the library's scaling convention. The same header's
  `SpectralFreezeMulti` is the complete multichannel freeze (per-channel
  phase streams, stereo width, wet/dry mix — the low 8 bits of the
  identity tag are the channel index): a standalone freeze plugin only
  needs to add parameters around it, and Mango's FreezeEffect is exactly
  that adapter. `retrigger()` (added for Mango's rhythmic freeze) re-captures
  from a rolling per-instance input history and swaps the magnitude spectrum
  in place while the wash keeps playing — the 4-frame overlap-add crossfades
  old→new, so it is click- and gap-free, and the running frame counter keeps
  it deterministic.
  `dsp/ArEnvelope.h` (unused by Mango currently), `dsp/DeterministicRandom.h`,
  `midi/NoteDuration.h`.
- `dsp/GrainLooper.h` (pre-existing) gained: `setAttack(frac, gamma)` /
  `setRelease(frac, gamma)` per-instance envelopes on top of the seam fades,
  and **the first (recording) grain shapes its live pass-through with the
  same `instanceEnv()`** — without this the first grain of every block
  played unshaped (user-reported bug). First seam is sequential, not
  overlapped (documented one-off dip).
- `presets/PresetManager.{h,cpp}` (pre-existing) gained the optional
  `onBeforeSave` / `onAfterLoad` side-state hooks (both run with dirty
  tracking suppressed): processors that keep non-parameter data outside
  apvts.state (Mango's MangoSeq) merge it in before a preset save and
  rebuild from it after a preset load.
- `components/SplashOverlay.h` — cover-the-window splash / about screen:
  dimmed backdrop, artwork centred (scaled down, never enlarged), timed
  fade in / hold / fade out, click to dismiss, `onDismissed` callback.
  Display only; the owner decides when it appears.
- `components/AccentToggle.h` — the latching on/off button (rounded body
  that lights in its accent colour, bold centred text, legible down to
  ~18 px squares; custom-painted because the stock LookAndFeel's text
  indents leave no room at that size). Mango uses it for the lane
  headers' M/S/B letters, the Configs button and the config panel's
  include-parameters toggle.
- `components/TopBar.h` gained `setRightControls` (promoted from AmbiRR2's
  local TopBar): parks externally-owned controls left of the version
  string, keeping the blurb clear. Now takes an
  `initializer_list<{Component*, width}>` laid out left to right — Mango
  needs three slots (meter strip, preset bar, toggle) — with the original
  bar+button overload kept for existing callers. It also gained
  `setDecoration(juce::Image)`: artwork centred in whatever space is left
  between the blurb and those controls, scaled to the bar height. The blurb
  is measured and takes only the width it needs, so the gap — and the image
  centred in it — adapts to the description's length; `onlyReduceInSize`
  keeps a narrow gap from being overflowed. Mango passes
  `Assets/Mango.png`, the pixel-art fruit on a yellow -> green -> red
  waveform — the identity ramp again. It also gained `onLogoClicked`, fired
  when either image is clicked (hit areas recorded as they paint, so they
  follow the layout), with a pointing-hand cursor over them.
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
  sharing (peak at 2/3), mute/solo, aux sends (both aux buses enabled via enableAllBuses: gated
  send levels vs the flat main passthrough, and the effect degrading to a
  plain level control when the buses are disabled), the panner (cycle
  orders including the period-4 ping-pong, the left/right mirror identity,
  and the Random mode's audible balance matched step by step against
  panStateAt), pass repeatability (recomputes the
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
- **Choice parameters are append-only.** The `EffectType` enum /
  `effectTypeNames()` and every `AudioParameterChoice` (`busmode`, `pan_mode`,
  `flt_mode`, `dist_model`, `configsync`, …) may gain entries **only at the
  end**; never reorder or remove. APVTS serialises a choice as its raw index,
  so reordering silently reinterprets every saved value — a preset's
  `l<i>_type=2` stops meaning Delay. This is the invariant that keeps old
  presets loading (see §12.3 for the full argument).
- New effect type checklist: subclass `EffectBase` (+ static
  `addParameters(params, lanePrefix, nameP)` + `bindParameters`), extend
  `EffectTypes.h` (**append**), the factory + bind switch in
  `MangoEngine.cpp`, a case in `EffectPanel`'s constructor, a visual in the
  rack's `paintEffectVisual`, and (if it draws durations) reuse
  `DurationWeights`. Also add a `helpFor` case in EffectPanel — the info
  callout is part of the effect, not an afterthought.
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

## 12. Planned: the Player lane (design + forward-compatibility)

*Not implemented. This section is the design agreed for a future version and,
above all, a record that the **current** release can accept it without
breaking presets/sessions saved before it. Read §12.3 before touching state
code — it lists the invariants the release must keep so this stays true.*

### 12.1 Feature

A tenth-plus effect type, **Player**, plays audio from a shared **loop pool**
instead of processing the incoming signal. The pool is a small set of audio
loops the user drops in; each loop has **markers** on its waveform that name
onsets to start playback from. Markers are set/moved by hand or placed
automatically by transient detection.

- **New pane** (like the Configs / Presets panes): a waveform plot of the
  selected loop with its markers, add/move/delete of markers by hand, a
  "detect transients" button, and loop import. Opens from a button in the
  **gap between the step-size box and the Configs button** in the top-right
  controls (room is already there). Mutually exclusive with the config and
  preset panes, and stored in `ViewState` alongside them.
- **Per-lane (Player) parameters**, drawn/retriggered exactly like the other
  rhythmic effects: which **marker** to start from, the **playback rate**,
  and the 7 duration-probability weights (1/4…1/32 × straight/triplet/dotted)
  that set the **retrigger** rate inside each block. Each is overridable in
  the mini-language (new `OvKey`s, e.g. `marker`, `rate`, reusing the
  existing weight keys `w4…wdot`).

### 12.2 Where each piece lives

- **The audio pool is side state, not parameters** — same category as
  `MangoSeq` and `MangoBank`. A new `MangoPool` child of `apvts.state` holds
  the marker lists (and per-loop metadata); the audio bytes go through
  **`fxme::EmbeddedAudio`** (FLAC + Base64 under the state's `EmbeddedAudio`
  child, keyed by a slot id per loop). Because `EmbeddedAudio` and the state
  tree round-trip through both `get/setStateInformation` **and**
  `PresetManager`, **presets embed the audio automatically** — exactly what
  the user asked for, and the same mechanism other FX-Mechanics plugins use
  for impulse responses. No file paths are stored, so a preset is
  self-contained across machines.
- **Marker onsets and rate are lane parameters** (`l<i>_player_*`), so they
  automate and sit in configs like every other lane parameter. The *pool
  itself* (loops + markers) is shared across lanes and lives in `MangoPool`;
  a lane parameter only indexes into it.
- **Config bank**: `configParamKind` must classify the new `l<i>_player_*`
  ids (marker/rate → voicing/Optional; the weights → voicing/Optional, as the
  other effects' do). The pool is structure-ish but, like the blocks, is
  better carried whole: a config already carries a `MangoSeq`, so it can carry
  a `MangoPool` copy the same way if configs should recall loops. (Decide at
  implementation time; not carrying it — configs share the live pool — is
  also defensible and simpler.)
- **Realtime**: playback is a per-lane voice reading a shared, immutable
  decoded buffer. The decode (message thread) publishes an immutable
  `shared_ptr<const AudioBuffer>` the audio thread reads without locking —
  the pool is swapped, never mutated in place, so the seqLock is not extended
  to cover sample data.

### 12.3 Forward-compatibility verdict — **the current release is safe**

Verified against the code as it ships. Adding the Player later will load
every preset/session saved by the release **provided the rules below hold**.

**Why it works — APVTS stores the *denormalised* value.** The load-bearing
fact (checked in JUCE `AudioProcessorValueTreeState.cpp`: state writes
`unnormalisedValue`, restore calls `setDenormalisedValue`): the `l<i>_type`
choice is saved as its raw **index** (`value="2"`), not as a normalised
0–1 fraction. So appending `Player` as a new enum value **does not remap**
existing types — a stored `2` is still Delay whether the enum has 11 entries
or 12. (Contrast VST3 *automation*, which is normalised: an automated
`l<i>_type` lane in a DAW arrangement *would* remap. That is automation, not
preset/session state, and is called out as the one caveat.)

**Why new parameters are safe.** APVTS matches parameters by string id, so
`l<i>_player_*` ids simply don't exist in an old preset and load at their
defaults; their position in `createParameters()` is irrelevant. New side-state
children (`MangoPool`, more `EmbeddedAudio` entries) are absent in old
presets, and every reader here already tolerates an absent child
(`viewFromTree`, `sequencersFromTree`, the `MangoBank` lookups all no-op on a
missing/invalid tree).

**Rules the release — and the Player patch — must keep for this to hold:**

1. **The effect enum is append-only.** Add `Player` (and any future type) at
   the **end** of `EffectType` / `effectTypeNames()`; never reorder or remove.
   Reordering silently reinterprets every stored `l<i>_type`. This is now the
   single most important state invariant and belongs in §10.
2. **Never renumber existing choice parameters** (`busmode`, `pan_mode`,
   `flt_mode`, `dist_model`, `configsync`, …) for the same reason — append
   only. `busmode` already grew 3 → 5 this way safely.
3. **Absent state children stay optional.** Keep reading them defensively; a
   new `MangoPool` reader must no-op when the child is missing.
4. **Embed audio, never reference paths** — use `fxme::EmbeddedAudio` so
   presets stay self-contained.
5. A **new `OvKey` is appended** to the enum + `keyNames` (already the
   documented rule); old blocks simply never use it.

**No state-version tag is needed** and none exists: the format is additive and
tolerant (unknown/extra ignored, absent defaulted). A version tag would only
be worth adding if a future change had to *reinterpret* existing data (e.g.
the `gate_att` range change in §11) — appending the Player does not.

**One-line answer to the release question:** yes — as long as the effect enum
and every choice parameter stay append-only, the shipping architecture will
take the Player lane and still load presets/sessions made before it.

## 13. Backlog: sequencer editing & UX

*Mostly from a user's workflow critique (2026-07). Their framing was "number
of clicks, mouse distance, time to complete a task, visual responsiveness,
visual recognition" — a fair lens, and worth keeping while working through
this list. Ordered by value/effort, not by who asked.*

Note **where** each item lands: `SequencerRubber` / `StringSequencer` live in
**FxmeTools** and are shared with other plugins, so changes there need the
same care as any module change (and benefit every project).

### 13.1 Confirmed defects (fix first)

- **Grid shrink destroys blocks.** `StringSequencer::setNumSteps` *erases*
  blocks whose `startStep >= numSteps` and clamps the rest, so 32 → 16 → 32
  loses everything past step 16 permanently. Blocks should survive out of
  range: keep them in the model, hide/skip them while out of range, and clip
  only at save time (or not at all). Touches `setNumSteps`, the engine's
  block iteration, and `sequencersToTree`. **FxmeTools.**
- **Edge-grab zone is 7 px** (`SequencerRubber::kEdgeGrab`), too fine at
  small step widths. Scale it with the step width (e.g.
  `jlimit(6, 14, stepWidth/4)`), and show a resize cursor on hover so the
  zone is discoverable. **FxmeTools.**

### 13.2 Editing workflow

- **Paint mode**: hold and drag across the lane to lay down a run of
  fixed-length blocks, instead of one drag per block. The single most
  requested time-saver for dense patterns. **FxmeTools.**
- **Copy / paste / duplicate blocks** (and across lanes). Already on the
  wish list before the critique.
- **Multi-select** (rubber-band or shift-click) + operations on the
  selection: move, delete, resize, and *drag up/down to change a parameter*
  on all selected blocks at once.
- **Move should swap, not wall.** `StringSequencer::moveBlock` currently
  stops against neighbours; dragging past one should displace/swap it, and a
  block should be able to travel the whole lane. **FxmeTools.**
- **Shrink / expand selected blocks** by a step, from keys or buttons.
- **Randomise fill**: one lane or all lanes, with a density control. Fits
  Mango's seeded-randomness identity; must stay deterministic (draw from the
  seed, not from `rand()`), so it belongs with the `detrand` machinery.

### 13.3 Parameter entry

- **Inline block editor**: a small popup under the clicked block with that
  block's most-used parameters, instead of only the right-hand panel. This is
  the biggest single reduction in mouse travel and eye movement. The right
  panel stays as the full view.
- **Duration weights as a grid.** The current model is an *outer product* —
  `baseWeights[4] × modWeights[3]` — so "straight 1/4 **and** dotted 1/8" is
  not expressible: turning on dotted turns it on for every note value. A
  4×3 (or wider) grid of per-cell toggles would be strictly more expressive
  and easier to read than seven knobs. Drag-across-to-toggle is the cheap
  interaction win.
  **State cost:** this replaces 7 floats per effect with 12+ per effect, so
  it is a parameter-layout change — new ids alongside the old, old ones kept
  and mapped, or a documented migration. Not a free change; see §10's
  append-only rule.
- **Wider note range**: 1/1 and 1/2 at the slow end, 1/64 at the fast end
  (currently 1/4…1/32). Cheap on its own, but interacts with the grid item
  above — decide them together.

### 13.4 Visual responsiveness

- **Hover states** on blocks, lane headers and the bus diagram (the critique
  singled this out, and it is largely missing today).
- **Cursor feedback**: resize cursor on block edges, move cursor on bodies,
  paint cursor in paint mode.

### 13.5 Considered and declined

- **"The per-block text language is a side feature; give manual block setup
  instead."** Declined: the override language *is* the manual per-block
  setup, and it is central to what Mango is for. It stays and will grow
  (see §12: the Player lane adds keys to it). The real problem the critique
  points at is **discoverability**, which is being addressed instead: the
  override notice in the effect panel (§7), the printable reference
  (`doc/minilanguage.md`), and presets that do not depend on overrides.
  A GUI editor for block parameters (§13.3) is complementary to the text,
  not a replacement for it.

### 13.6 Already implemented (recurring questions)

Kept here so answers to future reports are consistent:

- **Delete a block**: alt-click, or select and press Delete. (Right-click
  clears the block's *text*.)
- **Fix one block by hand instead of hunting with the seed**: pin it with a
  block override, e.g. `dur=0.125`.
- **Move / resize a block**: drag its body / its edges.
- **Per-block parameter values**: the override language, one line per block.
