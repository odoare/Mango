# Mango — FX-Mechanics house-style audit (2026-08-20)

Audited commit: `2ddbf11` ("Update tests"), branch `main`, clean working tree
except the FxmeTools submodule pointer (see the commit plan).

FxmeTools: committed pointer `70e5cda` (2026-08-19), local checkout is 6
commits ahead at `c677ae4` (2026-08-20), which is clean (no uncommitted edits
inside the submodule). Both commits are on the far side of the core/shell
split, so the split findings below apply regardless of which of the two is
built against.

This audit is **static**: grep and read only, no build was run. See the
coverage caveat at the end for what that means for the API-rename checks in
particular.

---

## Silent bugs

None found. Every item on the shipped-silent-bug checklist (macOS
universal-binary settings, `AU_MAIN_TYPE`/MIDI input, the CI "verify" step's
ability to fail, the core/shell CMake wiring, the auxiliary-target trap, the
README's macOS and MIDI-routing sections) was checked against the actual
source and is already correct. Detail under "Already correct" below.

## Retrofit

Mechanical, low-risk items from the FxmeLookAndFeel retrofit and JUCE 8
deprecation sweep.

- [x] **R1. The shared look-and-feel never gets an accent colour, so every
      combo box's drop-down menu (and any future tooltip) is neutral grey.**
      [PluginEditor.h:62](../Source/PluginEditor.h#L62) declares the single
      `fxme::FxmeLookAndFeel lnf` for the whole editor, set once in
      [PluginEditor.cpp:21](../Source/PluginEditor.cpp#L21)
      (`setLookAndFeel (&lnf)`). Every `juce::ComboBox` in the project
      (`stepSizeBox`, `ConfigPanel::syncBox`, `EffectPanel::Combo::box`,
      `LaneRackComponent::typeBox`) inherits `lnf` through the component
      chain — correct, and no per-combo `setLookAndFeel()` call is needed
      (see "Already correct"). But `lnf.setAccentColour(...)` is never
      called anywhere, and a menu is its own window that cannot see the box
      that opened it, so every drop-down in the plugin opens with the
      default neutral-grey tick/hairline instead of the panel's own accent.
      **Fix:** one line in the `PluginEditor` constructor, e.g.
      `lnf.setAccentColour (theme::mangoYellow);` right after
      `setLookAndFeel (&lnf);` (pick whichever accent should represent the
      shared chrome — the mango-ramp yellow is the identity colour used for
      the preset chrome already).
      **safe to apply** — **done**: `lnf.setAccentColour (theme::mangoYellow);`
      added at [PluginEditor.cpp:22](../Source/PluginEditor.cpp#L22), right
      after `setLookAndFeel (&lnf);`. Unbuilt — confirm the drop-down tint by
      hand.

- [x] **R2. `setTooltip()` is called but no `juce::TooltipWindow` exists, so
      no tooltip in the plugin is ever visible.**
      [PluginEditor.cpp:45](../Source/PluginEditor.cpp#L45)
      (`presetToggle.setTooltip(...)`) and
      [PluginEditor.cpp:59](../Source/PluginEditor.cpp#L59)
      (`configToggle.setTooltip(...)`) are the only two `setTooltip()` calls
      in the project, and grepping the whole `Source/` tree for
      `TooltipWindow` finds nothing. `FxmeLookAndFeel::drawTooltip` is ready
      (dark panel, accent hairline once R1 is fixed) but nothing hosts it.
      **Fix:** add a `juce::TooltipWindow tooltipWindow { nullptr, 700 };`
      member to `PluginEditor.h` (after the child components) and
      `tooltipWindow.setLookAndFeel (&lnf);` in the constructor (a
      `nullptr`-parented TooltipWindow lives on the desktop and uses the
      *default* look-and-feel otherwise).
      **decision** — this makes tooltips start appearing everywhere in the
      plugin, which is a product/UX choice, not a pure cleanup. **Done**, by
      the user's explicit choice: `juce::TooltipWindow tooltipWindow { nullptr,
      700 };` added to [PluginEditor.h](../Source/PluginEditor.h#L101-L103)
      (after `splash`), `tooltipWindow.setLookAndFeel (&lnf);` added at
      [PluginEditor.cpp:23](../Source/PluginEditor.cpp#L23). Destruction order
      is safe as-is: `tooltipWindow` is declared after `lnf`, so it is
      destroyed first, well before `lnf` goes away. Unbuilt — worth a look for
      any tooltip whose wording assumed it would never be seen.

- [x] **R3. The distortion effect's output-gain knob is bipolar but is never
      told so, so its value arc fills from -24 dB instead of growing from
      0 dB.** The `dist_gain` parameter
      ([DistortionEffect.h:44-46](../Source/Dsp/Effects/DistortionEffect.h#L44-L46))
      has range -24…+24 dB, default 0.0 — the only knob-bound bipolar range
      in the project besides the bus-pan knob (which *is* handled
      correctly, see "Already correct"). It is wired through
      `EffectPanel::addKnob`
      ([EffectPanel.h:74](../Source/Components/EffectPanel.h#L74),
      `addKnob (prefix + "dist_gain", "Out dB")`), whose implementation
      ([EffectPanel.h:442-451](../Source/Components/EffectPanel.h#L442-L451))
      always calls `theme::styleKnob(*k.slider, label, accent)` with no
      bipolar flag. Verified against
      [FxmeLookAndFeel.h's `originProportion()`](../lib/FxmeTools/FxmeTools/lookandfeels/FxmeLookAndFeel.h#L145-L157):
      with neither `"centralValue"` nor `"drawFromCentre"` set, the origin
      is hardcoded to `0.0f` — the range minimum, i.e. -24 dB — so the arc
      always fills as if this were a unipolar 0…+48 control.
      **Fix:** add a `bool bipolar = false` parameter to `addKnob()`, pass
      it through to `theme::styleKnob (*k.slider, label, accent, bipolar)`
      (the fourth parameter already exists and does exactly this, see
      [Theme.h:121-133](../Source/Theme.h#L121-L133)), and pass `true` only
      at the `dist_gain` call site.
      **safe to apply** — **done, but the original scoping was too narrow**:
      the grep for `NormalisableRange<float> (-` (a literal negative lower
      bound) missed a whole other class of bipolar control — a 0..1 knob
      whose *meaningful* centre is 0.5 rather than the range minimum, which
      doesn't show up as a negative bound. The user caught this on the Att
      Curve / Rel Curve knobs; grepping for `curve` across `Dsp/Effects/`
      found it applies to six knobs across three effects (`gate_attcurve`/
      `gate_relcurve`, `grain_attcurve`/`grain_relcurve`,
      `aux_attcurve`/`aux_relcurve` — all range 0..1, default 0.5, "0.5 is
      the linear ramp" per each effect's own doc comment), not just the one
      `dist_gain` knob.

      That also exposed a real bug in the mechanical fix as first written:
      `Theme::styleKnob`'s `bipolar` branch hardcoded
      `s.setCentralValue (0.0)`, which is correct for a ± range like
      `dist_gain` or bus pan but wrong for a 0..1 range centred at 0.5 —
      passing `bipolar=true` alone would have anchored the curve knobs at
      the *bottom* of their range, not the middle, silently reintroducing
      the same bug on a different set of controls. Fixed by replacing the
      hardcoded 0.0 with a `double centreValue = 0.0` parameter on
      `styleKnob` ([Theme.h:118-133](../Source/Theme.h#L118-L133), default
      preserves the `dist_gain`/pan behaviour unchanged) and on
      `EffectPanel::addKnob`
      ([EffectPanel.h:442-452](../Source/Components/EffectPanel.h#L442-L452)).
      All six curve call sites now pass `(nullptr, true, 0.5)`
      ([EffectPanel.h:46-47, 54, 56, 120-121](../Source/Components/EffectPanel.h#L46-L47)).
      `dist_gain` is untouched (its default `centreValue` of 0.0 is already
      right). Unbuilt — confirm the "Out dB" knob fills from 12 o'clock, and
      that all three curve-knob pairs now show empty at their 0.5 default
      rather than half-full.

- [x] **R4. Two toggle buttons can steal keyboard focus away from an
      in-progress block-text edit, unlike every other utility button in the
      project.** `configToggle`
      ([PluginEditor.h:93](../Source/PluginEditor.h#L93),
      wired at [PluginEditor.cpp:57-60](../Source/PluginEditor.cpp#L57-L60))
      and the per-lane M/S/B toggles built by `setupToggle`
      ([LaneRackComponent.h:306-311](../Source/Components/LaneRackComponent.h#L306-L311))
      never call `setMouseClickGrabsKeyboardFocus (false)`. Every comparable
      button elsewhere in the project does: `presetToggle`
      ([PluginEditor.cpp:40](../Source/PluginEditor.cpp#L40)), the lane
      up/down arrows ([LaneRackComponent.h:243](../Source/Components/LaneRackComponent.h#L243)),
      `ConfigPanel`'s load/store/undo buttons, and `BusBar`'s lane count and
      mode buttons. Clicking Mute/Solo/the bus switch, or the Configs
      toggle, while `BlockTextPanel`'s override editor has the caret will
      steal OS keyboard focus and fire its `onFocusLost` commit — probably
      harmless in effect (the edit is committed, not lost) but inconsistent
      with the rest of the codebase and will eat the next keystroke/shortcut
      aimed at the editor.
      **Fix:** add `setMouseClickGrabsKeyboardFocus (false)` inside
      `setupToggle()` (covers mute/solo/bus in one place) and one line next
      to `configToggle.setTooltip(...)` in the `PluginEditor` constructor.
      **safe to apply** — **done**:
      `configToggle.setMouseClickGrabsKeyboardFocus (false);` added at
      [PluginEditor.cpp:62](../Source/PluginEditor.cpp#L62), and
      `b.setMouseClickGrabsKeyboardFocus (false);` added inside
      `setupToggle()` at
      [LaneRackComponent.h:310](../Source/Components/LaneRackComponent.h#L310),
      covering mute/solo/bus in the one place they're built. Unbuilt —
      confirm clicking Mute/Solo/Bus/Configs mid-edit no longer steals the
      caret.

## House style

- [x] **H1. Saved state carries no version marker.**
      [PluginProcessor.cpp:684-719](../Source/PluginProcessor.cpp#L684-L719)
      (`getStateInformation`/`setStateInformation`) write and read the raw
      APVTS `ValueTree` (plus the `MangoSeq`/`MangoView` side-trees) with no
      `version` property anywhere. Nothing is broken today, but a version
      marker cannot be added retroactively to sessions already saved by a
      released build — every day this stays unfixed is another day of
      un-versioned sessions in the wild.
      **Fix:** `state.setProperty ("version", 1, nullptr);` before
      `state.writeToStream (mos)` in `getStateInformation`, and read it back
      in `setStateInformation` (`tree.getProperty ("version", 1)`) so a
      future format change has something to branch on. Purely additive —
      old and new savers/loaders round-trip identically either way.
      **safe to apply** — **done**:
      `state.setProperty ("version", 1, nullptr);` added at
      [PluginProcessor.cpp:687](../Source/PluginProcessor.cpp#L687), right
      after `apvts.copyState()`. `setStateInformation` doesn't read it back
      into a branch yet — there's only one version so there's nothing to
      migrate from — but a comment at
      [PluginProcessor.cpp:701-702](../Source/PluginProcessor.cpp#L701-L702)
      marks where a future version bump reads `tree.getProperty ("version",
      1)` and branches. Unbuilt — a saved session/preset from before this
      change loads fine either way (missing property just isn't there,
      nothing reads it yet); worth a round-trip save/load to confirm.

## Already correct

Coverage record — these were checked against the actual source, not assumed
from the checklist.

**Project shape.** `FXME` manufacturer code, single `juce_add_plugin` target
(`Mango`, `PLUGIN_CODE MNGO`), FxmeTools at the standard `lib/FxmeTools`
layout (not nested, not relocated). Single-plugin repo, so Step 3's
registration-completeness check (a target present in one list and missing
from another) does not apply by construction — one root `CMakeLists.txt`,
one `.github/workflows/release.yml`, one README table, all naming the same
plugin.

**Core/shell split.** Fully wired via the Trap-1 three-line fix
([CMakeLists.txt:36-42](../CMakeLists.txt#L36-L42): `add_subdirectory(...core FxmeCore)`,
`juce_add_module(...)`, `target_link_libraries(FxmeTools INTERFACE FxmeCore)`),
deliberately not through `fxmetools_attach()` (Mango only needs `fft.c` from
WDL, documented at [CMakeLists.txt:26-28](../CMakeLists.txt#L26-L28)). Both
auxiliary targets are correctly handled: `MangoTests`
([CMakeLists.txt:148](../CMakeLists.txt#L148)) links `FxmeCore` explicitly
(it links no JUCE at all), `MangoRenderTest` needs nothing extra because it
links the `FxmeTools` module, which carries `FxmeCore` transitively. This
matches `lib/FxmeTools/doc/api-changes.md`'s own record of Mango as "done"
(0 undefined symbols, both test suites passing before the last sync).

No renamed-API usage: grepped for `shapeChoices`/`syncRateChoices`/
`syncDivisionChoices`, `getScaleTypeNames`, `MidiTools::`, `getSortedSet`/
`getRawNotes`/`getDegrees` across `Source/` and `Tests/` — zero hits. Mango
doesn't use `Lfo` or `MidiTools` at all. `fxme::Random`'s reseeding change
(the one behavioural, non-source-breaking item in the split) doesn't apply
either: Mango's randomness runs entirely through `fxme::detrand::u01`, a
pure function of `(seed, laneIndex, blockId, drawIndex)`, confirmed
unaffected by `api-changes.md`'s own note on this ("Mango draws through
`fxme::detrand::u01`... which moved to core untouched"). The one
behavioural DSP change in the six unpushed FxmeTools commits — the
Downsampler phase fix — is *not* pending: it landed in commit `70e5cda`,
which is the commit already pinned in Mango's parent repo.

**macOS / CI.** The `if(APPLE)` block sits before `project()`
([CMakeLists.txt:15-20](../CMakeLists.txt#L15-L20)), deployment target is
10.13, and the release workflow passes both settings again on the configure
line ([release.yml:121-124](../.github/workflows/release.yml#L121-L124)).
`AU_MAIN_TYPE` is `kAudioUnitType_MusicEffect` with `NEEDS_MIDI_INPUT TRUE`
([CMakeLists.txt:56-66](../CMakeLists.txt#L56-L66)) — the historic
Mango 0.1.0–0.1.2 bug is fixed and stays fixed, with a comment recording
why. The "Verify universal binaries" CI step exits non-zero on a missing
slice or missing bundle and checks all three bundle types, not just the
VST3 ([release.yml:132-155](../.github/workflows/release.yml#L132-L155)).

**README.** Has both required sections: "macOS: one extra step" with the
`xattr -dr com.apple.quarantine` lines for all three bundle types, and
"Sending MIDI to Mango" with a per-host routing table (REAPER, Live, Bitwig/
Studio One/Cubase, Logic).

**Controls / look-and-feel.** The look-and-feel is set once on the editor
([PluginEditor.cpp:21](../Source/PluginEditor.cpp#L21)), so every descendant
— including all four `ComboBox`es — gets it by inheritance; the generic
"every ComboBox needs its own `setLookAndFeel()`" checklist item does not
apply to this project (verified: `lnf` is the only `FxmeLookAndFeel`
instance anywhere in `Source/`, confirming the editor-level pattern rather
than the per-widget one). Bipolar rotary knobs use `setCentralValue()`
correctly (`BusBar`'s pan knob, [BusBar.h:68](../Source/Components/BusBar.h#L68));
no `LinearHorizontal`/`LinearBarVertical` sliders exist in the project, so
the linear-bipolar bug class does not apply either. No bare `juce::Slider`
+ `TextBox` + `Label`, and no bare `juce::ToggleButton` + hand-attached
`ButtonAttachment`: every toggle uses the house `fxme::AccentToggle`
component (a `juce::TextButton` subclass), including the three
APVTS-bound lane toggles that wire their own `ButtonAttachment` rather than
going through `fxme::FxmeButton` — a legitimate variant already used
consistently, not the bare-toggle anti-pattern the checklist warns about.
`fxme::InfoButton` is present on every panel (`BlockTextPanel`, `BusBar`,
`ConfigPanel`, `EffectPanel`). `fxme::TextEntryFocusFixer` is present
([PluginEditor.h:109](../Source/PluginEditor.h#L109)), and the one
timer/refresh path that calls `setText()` on a live `TextEditor`
(`BlockTextPanel::setBlock`,
[BlockTextPanel.h:108-110](../Source/Components/BlockTextPanel.h#L108-L110))
correctly guards it with `hasKeyboardFocus (true)`.

**Presets.** `fxme::PresetManager` wired with both factory (`BinaryData`)
and user presets ([PluginProcessor.h:70-76](../Source/PluginProcessor.h#L70-L76)),
`fxme::PresetBarComponent` + `fxme::PresetComponent` (via `PresetOverlay`)
each call `setAccentColour()` on themselves — correct, because each owns its
own private `FxmeLookAndFeel` internally (`components/PresetBarComponent.h`,
`components/PresetComponent.h`) rather than sharing the editor's `lnf`, so
this is unrelated to the R1 finding above.

**JUCE 8 deprecations.** Every `juce::Font` construction found in `Source/`
(23 call sites across 8 files) already uses
`juce::Font (juce::FontOptions (...))`. No `createWriterFor` usage anywhere.
Nothing left to migrate.

**Realtime safety.** `processBlock`
([PluginProcessor.cpp:123-177](../Source/PluginProcessor.cpp#L123-L177))
opens with `ScopedNoDenormals`, uses `setDataToReferTo` (not a copying
assignment) for the aux-bus views, and allocates nothing. Sampled every
effect's `process()` method (11 files under `Dsp/Effects/`) plus
`MangoEngine::process` — no allocation, no locking, no file/console I/O in
any of them; parameter setup (`push_back` on the parameter vector) is
confined to `addParameters()`, called only from `createParameters()` at
construction. The one `juce::CriticalSection` in the project (`seqLock`,
guarding the lane sequencers/display order/parsed-override map) is held for
the whole DSP section of `MangoEngine::process` by explicit, documented
design ([MangoEngine.h:31-37](../Source/Dsp/MangoEngine.h#L31-L37)) — this
is the same "hold it around a minimal, pre-allocated read/advance" pattern
`juce-best-practices` documents as the house pattern for exactly this case
(Neorix's `seqLock_`), and the matching message-thread critical sections
(`setBlockContent`/`blockContent`,
[PluginProcessor.cpp:321-337](../Source/PluginProcessor.cpp#L321-L337)) are
correspondingly tiny, with parsing (`rebuildOverrides`) done outside the
lock as the contract requires.

**Layering against FxmeTools.** Nothing reimplemented that already exists:
`FilterEnvEffect` uses `fxme::Biquad` + `fxme::FormantFilter`,
`QuantizerEffect` uses `fxme::BitCrusher` + `fxme::Downsampler`,
`DelayEffect` uses `fxme::DelayLine`, `DistortionEffect` uses
`fxme::Saturator`, `GrainDupEffect` uses `fxme::GrainLooper`,
`FreezeEffect` uses `fxme::SpectralFreeze`, `MangoEngine` uses
`fxme::StringSequencer` + `fxme::SequencerEngine`, `MeterStrip` uses
`fxme::VuMeter`/`VuMeterComponent`, and `DurationWeights` wraps
`fxme::WeightedDurationTable`. One near-miss was checked and ruled out:
`GaterEffect` and `AuxSendEffect` hand-roll a curve-shaped (gamma-exponent)
attack/release ramp rather than using `fxme::ArEnvelope` — read
`lib/FxmeTools/core/FxmeTools/dsp/ArEnvelope.h` directly and confirmed it is
a strictly linear trapezoid with no curve shaping, so this is not a
duplicate of an existing kernel, it's a feature `ArEnvelope` doesn't have.
`RingModEffect`'s carrier oscillator was checked against `fxme::
SignalGenerator` for the same reason and is also not a duplicate (
`SignalGenerator` is a fixed test-signal source; `RingModEffect` needs an
exponentially-glided sweep, which is different code driven by the same
per-block-weights machinery as every other effect here). Nothing in
`Source/` looked like a candidate for promotion *into* FxmeTools: everything
generic Mango needs (DSP kernels, the sequencer, meters) is already pulled
from the submodule, and what's left in `Source/Dsp/` (the mini-language
parser, `HeldNotes`, `DurationWeights`, the per-effect classes) is
genuinely tied to Mango's own override language and parameter layout.

**State / self-containment.** No audio loaded by file path anywhere in the
project — `fxme::EmbeddedAudio` doesn't apply (Mango processes the live
signal, it doesn't load samples or IRs).

---

## Coverage caveat

This audit did not build the project. In particular:

- [ ] The core/shell CMake wiring (see "Already correct") is a claim about
      structure, verified by reading the CMakeLists, not by configuring or
      building. A `cmake` *configure* (no build) would catch a typo a grep
      cannot.
- The renamed-API sweep (no `Lfo`/`MidiTools` hits) is a claim about what
  `Source/` currently calls; it says nothing about warnings a real compiler
  would emit. If useful, paste the last build's warning output here for a
  second pass — in particular around `-Wdeprecated-declarations` and any
  FxmeTools header that changed shape between the pinned `70e5cda` and the
  checked-out `c677ae4`.
- Per `lib/FxmeTools/doc/api-changes.md`, Mango was last built and both test
  suites (`ctest` 2/2, 26,769 checks) run clean against the checked-out
  FxmeTools commit `c677ae4` (2026-08-20) — i.e. *ahead* of what this parent
  repo currently has pinned (`70e5cda`). That is stronger evidence than a
  static audit alone, but it predates the R1–R4/H1 findings above, which
  are new to this pass.

## Commit plan

- [x] R1 committed (`7e15a09`, "Accent colour for look and feel").
- [x] R2 committed (`5333f79`, "Implement tooltips") — applied by the user's
      explicit choice, tooltips are now live everywhere `setTooltip()` is
      called.
- [x] R3 committed (`03a98fb`, "Mangane bipolar knobs") — includes the
      widened fix (six curve knobs, not just `dist_gain`) and the
      `centreValue` correction.
- [x] R4 committed (`d655d9d`, "Mouse click on buttonss don't grab keyboard
      focus").
- [ ] H1 is applied in the working tree
      ([PluginProcessor.cpp](../Source/PluginProcessor.cpp)) but not yet
      committed.
- [ ] Build `Mango_VST3` and `Standalone` with `-j2` and confirm all five
      by hand — none of R1–R4/H1 are covered by `MangoTests`/
      `MangoRenderTest`.
- [ ] Commit the FxmeTools submodule bump: `lib/FxmeTools` is still a clean
      checkout 6 commits ahead of the pinned pointer (`70e5cda` →
      `c677ae4`), unrelated to and unaffected by the R1–R4/H1 work above.
      Not a source edit, just `git add lib/FxmeTools && git commit` in the
      parent repo once you're satisfied — the CMake and test-suite work for
      this bump was already committed before this audit (`d4ce8e6`,
      `2ddbf11`).
