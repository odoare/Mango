# Mango 0.2.0

Denser control panels, per-block randomness in more effects, and a block
language that can now speak in plain seconds.

**Compatibility:** sessions, presets and automation from 0.1.3 load
unchanged. No existing parameter was renamed, renumbered or reordered - every
new knob defaults to the old fixed behaviour (Std = 0, Fade at its previous
constant 15 ms). Saved state now also records an internal format version
(currently 1), ahead of any future save-format change.

## New

**FxmeNumberBox controls, everywhere.** Every effect panel, and the bus
Volume/Pan knobs at the bottom of the mixer, now use a compact readout
control in place of a knob: name and value as text, drag up/down to adjust,
right-click to type a value directly - built for panels that had outgrown
knobs as more parameters were added.

**Per-block standard deviation.** Gater, Grain, Delay, Quantizer, RingMod,
Freeze and AuxSend each gain a Std knob alongside every mean control they
already had (attack/release/curve, delay time/feedback/damping, bits/
downsample, ring frequencies, freeze width). At 0 a block still draws the
exact mean, as before; above 0 every block draws its own value around it, so
several lanes running the same effect don't move in lockstep.

**Grain Fade knob.** The seam crossfade between repetitions, previously a
fixed 15 ms, is now a knob: short keeps repeats tight, long smooths a grain
that isn't looping cleanly on its own.

**Minilanguage: absolute seconds and note-value shortcuts.** A block
override can now write `dur=1.5s` for a literal time in seconds, independent
of both tempo and `mididur`. `s4 s8 s16 s32 s64` (straight), `t4..t64`
(triplet) and `d4..d64` (dotted) are shortcuts for the fractions the
duration-weight grid already draws from, and take the same `*N`/`/N` scaling
`mididur` does - e.g. `dur=t8*2`.

**Tooltips** across the transport, block editor and lane controls.

## Fixed

**Grain's loop landed sharp of the requested pitch/duration.** The fixed
30 ms crossfade was eating into the recorded grain, so a loop meant to be
exactly `dur` long (tuned to a MIDI note, say) came out measurably shorter -
audible as slightly sharp. Grain now trims its crossfade so the loop always
lands on the exact period requested.

**Bipolar knobs whose centre isn't 0.** Att Curve and Rel Curve (Gater,
Grain, AuxSend) are a 0..1 range whose neutral setting is the midpoint, 0.5 -
they were drawing their bipolar fill from 0 instead, which read as already
off-centre at rest.

**Buttons stole keyboard focus on click.** Clicking a lane button or the
Configs toggle could pull focus out of whatever text field you were editing.

## Changed

**Mix rides with the block-keys text.** On every panel, Mix (Amount on
RingMod; the three aux sends on AuxSend) now sits beside the "block keys"
reference text at the bottom instead of taking a slot in the knob grid -
freeing the grid to size every control the same way regardless of how many
fit in a row, instead of stretching the last few to fill the panel width.

**A consistent accent colour** now tints every inherited look-and-feel
element (combo box menus and the like), not just the controls Mango styles
directly.

# Mango 0.1.1

A workflow release: editing blocks is much faster, the block language
understands chords, and two annoyances are fixed.

**Compatibility:** sessions, presets and automation from 0.1.0 load
unchanged. No existing parameter was renamed, renumbered or reordered.

## New

**Chords in the block language.** `mididur` and `midifreq` now take a voice
digit 1–4 addressing the notes you are holding, counted from the lowest up:
`dur=mididur2`, `f0=midifreq1*2`. Point each lane at a different voice and one
chord tunes the whole rack. Four notes are followed at once (play a fifth and
the oldest is dropped); without a digit both keep meaning the last note
played, as before.

**Copy and move blocks.** All of these work within a lane and across lanes:

| Gesture | Action |
|---|---|
| Ctrl-drag a block | Drop a copy of it, text included |
| Ctrl-D | Copy the selected block into the steps right after it, and select the copy — press again to lay down a run |
| Ctrl-Shift-drag a block | Copy just its override text onto the block you drop it on |
| Drag a block's body to another lane | Move it there |

A whole block may only land on a lane running the same effect (its override
text would mean nothing on a different one); an override string can go to any
lane. The drop outline turns red whenever the drop would be refused, and
refusing costs nothing: the block stays exactly where it was. On macOS use
Cmd where this says Ctrl.

**1/64 note lengths.** Every effect with a rhythmic rate gains a P(1/64)
weight, and `w64` works as a block override. It defaults to zero, so nothing
you already made changes.

**Block override indicator.** When a selected block overrides lane knobs, the
effect panel now names them: `block overrides: dur w16 (knobs inactive)`. Those
knobs really are inactive for that block, which was previously invisible and
looked like the knobs were broken.

## Fixed

**Help popups and dropdown menus vanishing on Windows.** They blinked twice and
disappeared, making the in-plugin help unusable (reported in FL Studio on
Windows 10). Caused by the plugin's own keyboard-focus handling closing them;
Linux and macOS were not affected.

**Shortening the pattern destroyed blocks.** Going 32 → 16 steps permanently
deleted everything past step 16. Blocks past the end are now kept and hidden,
an amber arrow at the right edge shows they are there, and lengthening the
pattern brings them back unchanged, override text and all.

**Block edges were hard to grab.** The resize zone was a fixed 7 px; it now
scales with the step width, and is capped so that short blocks always keep a
draggable middle.

## Changed

**Clearing a block's override text is now Alt-right-click** (it was a plain
right-click, one stray click away from losing a line of text). Alt is now the
"destroy" modifier for both: Alt + left button deletes the block, Alt + right
button deletes its text.
