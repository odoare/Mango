# Mango: the block override language

*Quick reference. See the README for the rest of the plugin.*

Every lane has one set of knobs, but **each block can override them with a
line of text**. That is how one delay lane plays a different time in every
block, or a filter tracks the note you just played. Select a block and type
into the **block overrides** field at the bottom right.

## Syntax

```
key=value  key=value  key=value
```

Space-separated `key=value` pairs, up to 16 per block. Newlines count as
spaces (press **Ctrl+Return** for a newline, **Return** to commit).

- Each effect reads only the keys it knows and ignores the rest. The panel
  above the field lists the keys the selected lane understands.
- A key you set here overrides that knob **for this block only**.
- **All or nothing:** if any part of the line does not parse, *none* of it is
  applied and the field turns red. Your text is kept so you can fix it.

## Values

| Form | Meaning | Example |
|------|---------|---------|
| number | a plain value | `fb=0.6` |
| `mididur` | length of one cycle of the last MIDI note, in **seconds** | `dur=mididur` |
| `midifreq` | pitch of that note, in **Hz** (= 1/mididur) | `f0=midifreq` |
| voice digit | the same, on one note of the chord you are holding | `dur=mididur2`, `f0=midifreq1` |
| scaled | any magic word times or divided by a number | `mididur*2`, `mididur/4`, `2*midifreq3` |
| vowel letter | for `v0` / `v1` only | `v0=a` |

**MIDI tracking.** Without a digit both follow the last note-on, sampled when
the block starts: `dur=mididur` tunes a delay to the note, `f0=midifreq`
points a filter at it.

**Chords.** Add a digit **1 to 4** to address a note of the chord currently
held, counted **from the lowest up**: `mididur1` is the bottom note,
`mididur2` the next one, and so on (`midifreq1..4` likewise). Give each lane
a different voice and one chord tunes the whole rack:

```
lane 1   dur=mididur1 fb=0.97      \
lane 2   dur=mididur2 fb=0.97       )  a chord of tuned combs
lane 3   dur=mididur3 fb=0.97      /
```

Up to four notes are followed at once; play a fifth and the **oldest** is
dropped, so the chord always tracks what you played last. Ask for a voice you
are not holding (`mididur3` on a two-note chord) and you get the top note
instead of silence, and with nothing held at all you get the last note
played, exactly like plain `mididur`.

### The one unit trap

`dur` changes meaning depending on how you write it:

| Written | Means | At 120 bpm |
|---------|-------|------------|
| `dur=0.25` | a fraction of a **whole note**, follows host tempo | a quarter = 0.5 s |
| `dur=0.125` | eighth note | 0.25 s |
| `dur=mididur` | a time in **seconds**, ignores tempo | 1 cycle of the note |

Every other key is in the same unit as its knob.

## Keys

| Key | Controls | Range |
|-----|----------|-------|
| `dur` | rate / time (see above) | whole-note fraction, or seconds |
| `mix` | wet/dry of the effect | 0–1 (0 = transparent) |
| `att` `rel` | attack / release length | 0–1 = fraction of the shaped duration |
| `attcurve` `relcurve` | edge shape | 0 slow · 0.5 straight · 1 very fast |
| `fb` | delay feedback | 0–0.999 |
| `damp` | delay damping | 0–1 |
| `porta` | delay time glide | 1–50 (ms) |
| `model` | distortion model | 0 Standard · 1 Dynamic · 2 Triode · 3 Class AB |
| `drive` | distortion drive | 0–40 (dB) |
| `bias` | distortion bias | 0–0.5 |
| `sag` | supply sag | 0–1 |
| `gain` | distortion output gain | −24 to +24 (dB) |
| `mode` | filter mode | 0 Lowpass · 1 Highpass · 2 Formant |
| `mode` | panner mode | 0 `Cycle ->` · 1 `Cycle <-` · 2 `Cycle <->` · 3 Random |
| `q` | filter resonance | 0.3–12 |
| `f0` `f1` | start / end frequency | filter 20–20000, ring 0.5–10000 (Hz) |
| `v0` `v1` | start / end vowel | `a` `e` `i` `o` `u` |
| `bits` | bit depth | 1–24 |
| `down` | downsample factor | 1–64 |
| `amp` | ring modulation amount | 0–1 (this is the ring's mix) |
| `fade` | reverser seam fade | 0–0.5 |
| `width` | freeze stereo width | 0–1 (1 wide, 0 mono) |
| `aux1` `aux2` | aux send levels | 0–1 |
| `pass` | aux main passthrough | 0–1 (this is the aux send's mix) |
| `glide` | panner travel time | 0–1 = fraction of a step |
| `w4` `w8` `w16` `w32` | duration weights | 0–1 each |
| `wstr` `wtrip` `wdot` | straight / triplet / dotted weights | 0–1 each |

`mode` means different things on the filter and the panner (each lane only
sees its own).

## Which effect reads what

| Effect | Keys |
|--------|------|
| Gater | `dur att rel attcurve relcurve mix` + weights |
| Grain | `dur att rel attcurve relcurve mix` + weights |
| Delay | `dur fb damp porta mix` |
| Dist | `model drive bias sag gain mix` |
| Filter | `dur mode q f0 f1 v0 v1 mix` + weights |
| Quant | `bits down mix` |
| Ring | `dur f0 f1 amp` + weights |
| Rev | `dur fade mix` + weights |
| Freeze | `mix width` + weights |
| Aux | `dur att rel attcurve relcurve aux1 aux2 pass` + weights |
| Pan | `dur mode glide mix` + weights |

*weights* = `w4 w8 w16 w32 wstr wtrip wdot`

## Duration weights

Effects with a rhythmic rate do not take a fixed value: you weight how likely
each note length is, and the rate is **drawn** at the start of each block.
Setting `dur` pins it instead and skips the draw.

```
w4=1 w8=0 w16=0 w32=0        always a quarter
w16=1 w32=0.5                sixteenths, sometimes thirty-seconds
w8=1 wstr=1 wtrip=1          eighths, straight or triplet
```

Weights are relative, not percentages: `w4=1 w8=0.5` makes a quarter twice as
likely as an eighth. The draw depends only on the seed, the lane and the
block, so a pattern always plays back the same way and the block picture shows
exactly what you will hear.

## Recipes

```
dur=0.125 fb=0.6                 delay: eighth-note repeats, more feedback
dur=mididur fb=0.99 damp=0.4     delay: plucked string tuned by MIDI
f0=midifreq f1=midifreq*2        filter: sweep the note to its octave
v0=a v1=u                        filter: glide from "ah" to "oo"
mode=2 v0=i v1=o                 filter: force formant mode, I to O
w16=1 w32=1 mix=0.5              gater: fast, half depth
dur=mididur/2 amp=1              ring: carrier at twice the note pitch
f0=midifreq2 q=8                 filter: ring the second note of the chord
bits=3 down=8 mix=0.7            quant: crunchy, blended back
pass=0 aux1=1                    aux: send this block away from the main mix
mode=3 glide=0                   panner: random, hard jumps
w4=1 wdot=1                      freeze: re-capture every dotted quarter
```

## Gotchas

- **Red field = nothing applied.** One bad key disables the whole line, not
  just that pair.
- **Unknown keys are an error**, not ignored: a typo like `feedback=0.6`
  turns the line red. Keys an effect does not *use* are fine; keys that do
  not *exist* are not.
- `mix=0` makes the lane transparent, so the block does nothing. On the ring
  modulator that key is `amp`, on the aux send it is `pass`.
- `dur` has no effect on Freeze: its retrigger rate comes from the weights
  only.
- A `mididur` expression is in seconds and **ignores the host tempo**. That
  is the point, but it means `dur=mididur` will not stay in time with the
  grid unless the note happens to line up.
- Voice digits stop at **4**: `mididur5` is a typo, not a fifth voice, so it
  turns the line red.
- The voice digits read the notes held **when the block starts**. Releasing a
  note does not retune a block that is already sounding; the next block picks
  up the new chord.
