# Mango

**FX-Mechanics modular sound glitcher / mangler** — a JUCE audio effect (VST3 / AU /
Standalone) with MIDI input.

Mango is a rubber step sequencer of up to eight lanes (default 4) where every
lane drives one effect. While
the playhead is inside a block on a lane, that lane's effect processes the audio;
lane order (top to bottom) is the processing order and can be changed with the
arrows on the left. Lanes group into up to **four buses**: the **B** switch on
a lane header starts a new bus at that row (row 1 always starts bus 1). Each
bus runs its lanes in series and has its own **wet** and **pan** controls in
the bus bar under the rack, which also shows the routing diagram and cycles
between three **routing modes**: all buses parallel (each processes its own
copy of the input, outputs summed), bus 3 processing the mixed outputs of
buses 1+2 (bus 4 parallel), or bus 4 processing the mixed outputs of buses
1–3. A mode that needs more buses than exist falls back to parallel. Lane
colours show the bus a lane belongs to; within a bus, each following lane is
shaded a little lighter. All randomness is seeded and reproducible: the same
session with the same seed always glitches the same way, in real time and
offline bounces alike.

## Effects (selectable per lane)

| Effect  | What it does | Lane parameters |
|---------|--------------|-----------------|
| Gater   | Cuts the sound rhythmically: open(dur), closed(dur), ... starting open | duration probabilities, attack/release lengths + curves |
| Grain   | Records a grain at block start, loops it for the whole block (fixed 15 ms seam crossfade) | duration probabilities, per-repetition attack/release lengths + curves |

Attack/release lengths are fractions (0–1) of the shaped duration (the gate's
open phase, or one grain repetition — never the whole block). If their sum
exceeds 1 they share the duration proportionally to their values: att=1,
rel=0.5 acts as att=2/3, rel=1/3. The curves set the edge shapes: 0 slow,
0.5 linear, 1 very fast (a fast release ≈ exponential decay).
| Delay   | Feedback delay (buffer persists across blocks); damping mellows the repeats, portamento sets the time-glide (1–50 ms). With `dur=mididur fb=0.99` it is a Karplus-Strong style resonator tuned by MIDI | time, feedback, damping, portamento |
| Dist    | Tube-style saturation (Standard / Dynamic / Triode / Class AB). Saturation loudness depends on the input level, so the output gain (±24 dB, on the saturated signal) is the manual makeup | model, drive, bias, sag, out gain, mix |
| Filter  | LP / HP sweep from start to end frequency, or a formant vowel glide, repeating at a drawn rhythmic rate | mode, Q, start/end freq, start/end vowel, ramp probabilities |
| Quant   | Lo-fi: bit-depth reduction + sample-rate reduction (raw sample & hold — the aliasing is the point) | bits (1–24), downsample (÷1–64), mix |
| Ring    | Ring modulator: a sine carrier glides from a start to an end frequency over a drawn tempo-synced ramp, repeating for the block. Amount 0 = clean, 1 = full ring modulation (low frequencies give tremolo) | start/end freq (0.5 Hz–10 kHz), amount, glide probabilities |
| Rev     | Reverser: chops the audio into drawn-duration slices and plays each backwards (the first slice of a block passes through — there is nothing to reverse yet) | slice probabilities, seam fade |
| Freeze  | Spectral freeze (FFT): captures ~43 ms at block start (passed through while recording), then sustains its spectrum as a static, non-periodic wash for the rest of the block. Width sets how similar L and R are (1 = fully decorrelated/wide, 0 = mono; equal-power, so the level stays constant) | mix, width |

**Mix**: every effect has a wet/dry mix knob (override key `mix`) — except the
ring modulator, whose `amount` plays that role. At 0 the lane is transparent;
for the delay, mix scales the delayed signal added to the dry.

**Duration probabilities**: effects that need a rhythmic duration (gate rate, grain
length, filter ramp, ring glide, reverse slice) don't use a fixed value. You weight the probability of
1/4, 1/8, 1/16, 1/32 and of straight/triplet/dotted; the actual duration is
drawn from those weights — e.g. with P(1/4)=1, P(1/8)=0.5, P(1/16)=0.1 the
chance of an uncut quarter is 1/1.6. The draw is a pure function of
(seed, lane, block): every pattern pass plays exactly the sequence the block
visuals display, and changing the seed re-rolls every block.

## Per-block override language

Select a block and type overrides into the *block overrides* field
(space-separated `key=value`, strict all-or-nothing parsing; an unparsable
string turns the field red and is kept for fixing):

```
dur=0.125 fb=0.6        eighth-note delay with more feedback
dur=mididur/2           half the period of the last MIDI note received
dur=mididur fb=0.99 damp=0.4    plucked-string resonance on the last note
f0=midifreq f1=midifreq*2       sweep from the note's fundamental to its octave
v0=a v1=u               formant glide from A to U
```

- `dur` as a plain number is a fraction of a whole note (0.25 = quarter, 0.125 =
  eighth) resolved against the host tempo; any expression with `mididur` is a
  time in seconds (`mididur`, `mididur*2`, `mididur/4`, `3*mididur`).
- Other keys are in the parameter's native unit:
  `fb damp porta att rel attcurve relcurve q f0 f1 v0 v1 bits down drive bias sag gain mix width model mode fade amp`
  and the duration probability weights `w4 w8 w16 w32 wstr wtrip wdot`
  (each effect reads the keys it understands).
- **`mididur`** is 1/f of the last MIDI note-on the plugin received, sampled when
  the block starts; **`midifreq`** is its frequency in Hz (1/mididur), for the
  frequency keys (`f0=midifreq`, `midifreq*2`, `midifreq/2`, `2*midifreq`).

## Global controls

Dry/Wet, Seed (0–99999), the shared grid (step size 1/16…1/1, 1–64 steps),
and the lane count (1–8, default 4 — hidden lanes keep their blocks and
settings and simply stop processing). The bus bar under the rack holds the
lane-count − / + chooser above the routing-mode button, the routing diagram
and the per-bus wet/pan knobs (shown for active buses only).

**Sequencer configs**: the **Configs** button swaps the lane panel for a bank
of 8 configuration slots — one row to load, one to store. A config is a
*pattern*, not a sound: it holds the blocks and their override strings, the
grid, the lane count/order/effect types, the whole bus routing (grouping,
mode, per-bus wet and pan) and the seed. Tick *include effect parameters* —
off by default — to store the effect knob values as well; slots that carry
them are underlined, so a recall is never a surprise. Mute, solo and the
global dry/wet are never stored, so a recall can't clobber your live mix.
Slots show their state (filled = active, outlined = stored, dim = empty,
dot = edited since loaded); alt-click a store slot to clear it, and *undo
store* takes back the last overwrite. Recalls can be
immediate or quantised to the next bar or pattern. Only the slot **selector**
is a plugin parameter, so the bank is fully automatable from the host without
multiplying the parameter count.

**Presets**: the top bar hosts a compact preset strip (previous / name /
next) and a toggle that expands the full browser over the right column —
factory presets plus user presets saved as XML under the platform user-data
folder (`.../Mango/Presets`). Presets carry the complete state, sequencer
blocks and override strings included.
Each lane header also has **M**ute and **S**olo toggles (muted / non-soloed
lanes keep sequencing — draws stay deterministic — but stop processing audio)
and the **B**us-start switch. Note that an idle bus passes its copy of the dry
input, so several buses with no sounding blocks sum to more than unity — the
usual parallel-rack behaviour; use the dry/wet or your lanes' gating to taste.

Parameter changes apply to the sounding blocks immediately: the active block
is refreshed in place (same random draw, new values), keeping the gate/ramp
phase running. Host loop jumps re-enter blocks so every pass starts clean and
plays the same drawn sequence.

## Building

JUCE is expected as a sibling directory (`../JUCE`); FxmeTools is a git
submodule:

```sh
git clone --recursive <this repo>
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Tests (console, no audio device needed):

```sh
cmake -B build -DMANGO_BUILD_TESTS=ON
cmake --build build --target MangoTests MangoRenderTest -j
ctest --test-dir build
```

`MangoTests` covers the JUCE-free pieces (override parser, deterministic RNG,
weighted durations, FxmeTools DSP); `MangoRenderTest` renders audio through the
real processor and checks gate timing, host-tempo sync, seed determinism
(bit-exact), the override language and the state round-trip. Run
`MangoRenderTest <path.png>` to dump GUI snapshots instead.

## Architecture notes

Full architecture reference (engine, threading, determinism, language, GUI,
FxmeTools additions, testing, invariants): [doc/architecture.md](doc/architecture.md).

- Lanes are fixed identities 0–7; reordering only permutes a display/processing
  order, so parameters, sequences and random draws stay with their lane.
- Sequencer model/engine/GUI come from [FxmeTools](https://github.com/odoare/FxmeTools)
  (`StringSequencer`, `SequencerEngine`, `SequencerRubber`), as do the DSP
  building blocks added for Mango (`Saturator`, `FormantFilter`, `BitCrusher`,
  `DelayLine`, `GrainLooper`, `ArEnvelope`, `DeterministicRandom`,
  `NoteDuration`) and the shared FX-Mechanics `TopBar`.
- One lock guards the sequencers + override cache; the audio thread holds it
  during processing, and everything the message thread does under it is tiny
  and allocation-free (strings are parsed outside, the cache map is swapped in).
- New effect types: subclass `mng::EffectBase`, add parameters through the
  static `addParameters(params, lanePrefix, name)` hook, register the type in
  `Source/Dsp/EffectTypes.h` and the factory in `MangoEngine.cpp`.

---
Author: Olivier Doaré — FX-Mechanics · LGPL-3.0-or-later
