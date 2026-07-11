# Mango

**FX-Mechanics modular sound glitcher / mangler** — a JUCE audio effect (VST3 / AU /
Standalone) with MIDI input.

Mango is a six-lane rubber step sequencer where every lane drives one effect. While
the playhead is inside a block on a lane, that lane's effect processes the audio;
lane order (top to bottom) is the processing order and can be changed with the
arrows on the left. All randomness is seeded and reproducible: the same session
with the same seed always glitches the same way, in real time and offline bounces
alike.

## Effects (selectable per lane)

| Effect  | What it does | Lane parameters |
|---------|--------------|-----------------|
| Gater   | Cuts the sound rhythmically: open(dur), closed(dur), ... starting open | duration probabilities, attack, release (0–25 % of dur) |
| Grain   | Records a grain at block start, loops it for the whole block | duration probabilities, seam fade |
| Delay   | Feedback delay (buffer persists across blocks) | time, feedback |
| Dist    | Tube-style saturation (Standard / Dynamic / Triode / Class AB) | model, drive, bias, sag, mix |
| Filter  | LP / HP sweep from start to end frequency, or a formant vowel glide, repeating at a drawn rhythmic rate | mode, Q, start/end freq, start/end vowel, ramp probabilities |
| Quant   | Bit-depth reduction | bits (1–24) |

**Duration probabilities**: effects that need a rhythmic duration (gate rate, grain
length, filter ramp) don't use a fixed value. You weight the probability of
1/4, 1/8, 1/16, 1/32 and of straight/triplet/dotted; at each block entry the
actual duration is drawn from those weights — e.g. with P(1/4)=1, P(1/8)=0.5,
P(1/16)=0.1 the chance of an uncut quarter is 1/1.6. The draw is a pure function
of (seed, lane, block, loop pass), so it repeats deterministically.

## Per-block override language

Select a block and type overrides into the *block overrides* field
(space-separated `key=value`, strict all-or-nothing parsing; an unparsable
string turns the field red and is kept for fixing):

```
dur=0.125 fb=0.6        eighth-note delay with more feedback
dur=mididur/2           half the period of the last MIDI note received
v0=a v1=u               formant glide from A to U
```

- `dur` as a plain number is a fraction of a whole note (0.25 = quarter, 0.125 =
  eighth) resolved against the host tempo; any expression with `mididur` is a
  time in seconds (`mididur`, `mididur*2`, `mididur/4`, `3*mididur`).
- Other keys are in the parameter's native unit:
  `fb att rel q f0 f1 v0 v1 bits drive bias sag mix model mode fade`
  (each effect reads the keys it understands).
- **`mididur`** is 1/f of the last MIDI note-on the plugin received, sampled when
  the block starts.

## Global controls

Dry/Wet, Seed (0–99999), the shared grid (step size 1/16…1/1, 1–64 steps).

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

- Lanes are fixed identities 0–5; reordering only permutes a display/processing
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
