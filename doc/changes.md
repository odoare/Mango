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
