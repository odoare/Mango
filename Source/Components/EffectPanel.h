/*
  ------------------------------------------------------------------------------
    EffectPanel.h

    The right-column control panel for one (lane, effect type) pair: knobs,
    choice boxes and — for the duration-randomised effects — the 5+3
    probability-weight mini knobs. All controls attach to the lane's
    prefixed parameters, so the lanes × types panels are pre-built and
    simply visibility-switched when the selection or a lane's type changes.

    Author: Olivier Doaré, github.com/odoare
    (c) 2026 Olivier Doaré
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#pragma once

#include <JuceHeader.h>
#include <deque>
#include "../Theme.h"
#include "../ParamIDs.h"
#include "../Dsp/EffectTypes.h"
#include "../Dsp/DurationWeights.h"

namespace mng
{

class EffectPanel : public juce::Component
{
public:
    EffectPanel (juce::AudioProcessorValueTreeState& state, int laneIdx,
                 EffectType effectType, juce::Colour accentColour)
        : apvts (state), laneIndex (laneIdx), type (effectType), accent (accentColour)
    {
        title = "Lane " + juce::String (laneIndex + 1) + "  -  "
              + effectTypeNames()[(int) type];
        keywords = overrideKeysFor (type);

        const auto prefix = pid::lanePrefix (laneIndex);
        switch (type)
        {
            case EffectType::Gater:   // paired layout - see addKnob()/layoutKnobPairs(); Mix rides with the block-keys text
                addKnob (prefix + "gate_att", "Attack", nullptr, false, 0.0, true);
                addKnob (prefix + "gate_attstd", "Att Std", nullptr, false, 0.0, true);
                addKnob (prefix + "gate_rel", "Release", nullptr, false, 0.0, true);
                addKnob (prefix + "gate_relstd", "Rel Std", nullptr, false, 0.0, true);
                addKnob (prefix + "gate_attcurve", "Att Curve", nullptr, true, 0.5, true);
                addKnob (prefix + "gate_attcurvestd", "AttCv Std", nullptr, false, 0.0, true);
                addKnob (prefix + "gate_relcurve", "Rel Curve", nullptr, true, 0.5, true);
                addKnob (prefix + "gate_relcurvestd", "RelCv Std", nullptr, false, 0.0, true);
                addKnob (prefix + "gate_mix", "Mix", &primaryKnobs, false, 0.0, true);
                addWeights (prefix + "gate_", true);
                break;

            case EffectType::Grain:   // paired layout - see layoutKnobPairs(); Mix rides with the block-keys text
                addKnob (prefix + "grain_att", "Attack", nullptr, false, 0.0, true);
                addKnob (prefix + "grain_attstd", "Att Std", nullptr, false, 0.0, true);
                addKnob (prefix + "grain_rel", "Release", nullptr, false, 0.0, true);
                addKnob (prefix + "grain_relstd", "Rel Std", nullptr, false, 0.0, true);
                addKnob (prefix + "grain_attcurve", "Att Curve", nullptr, true, 0.5, true);
                addKnob (prefix + "grain_attcurvestd", "AttCv Std", nullptr, false, 0.0, true);
                addKnob (prefix + "grain_relcurve", "Rel Curve", nullptr, true, 0.5, true);
                addKnob (prefix + "grain_relcurvestd", "RelCv Std", nullptr, false, 0.0, true);
                addKnob (prefix + "grain_fade", "Fade", nullptr, false, 0.0, true);
                addKnob (prefix + "grain_mix", "Mix", &primaryKnobs, false, 0.0, true);
                addWeights (prefix + "grain_", true);
                break;

            case EffectType::Delay:   // paired layout - see layoutKnobPairs(); Mix rides with the block-keys text
                addKnob (prefix + "dly_dur", "Time", nullptr, false, 0.0, true);
                addKnob (prefix + "dly_durstd", "Time Std", nullptr, false, 0.0, true);
                addKnob (prefix + "dly_fb", "Feedback", nullptr, false, 0.0, true);
                addKnob (prefix + "dly_fbstd", "Fb Std", nullptr, false, 0.0, true);
                addKnob (prefix + "dly_damp", "Damping", nullptr, false, 0.0, true);
                addKnob (prefix + "dly_dampstd", "Damp Std", nullptr, false, 0.0, true);
                addKnob (prefix + "dly_porta", "Porta ms", nullptr, false, 0.0, true);
                addKnob (prefix + "dly_mix", "Mix", &primaryKnobs, false, 0.0, true);
                break;

            case EffectType::Distortion:
                addCombo (prefix + "dist_model", "Model");
                addKnob (prefix + "dist_drive", "Drive", nullptr, false, 0.0, true);
                addKnob (prefix + "dist_bias", "Bias", nullptr, false, 0.0, true);
                addKnob (prefix + "dist_sag", "Sag", nullptr, false, 0.0, true);
                // -24..+24 dB, centred on 0:
                addKnob (prefix + "dist_gain", "Out dB", nullptr, true, 0.0, true);
                addKnob (prefix + "dist_mix", "Mix", &primaryKnobs, false, 0.0, true);
                break;

            case EffectType::FilterEnv:
                addCombo (prefix + "flt_mode", "Mode");
                addCombo (prefix + "flt_v0", "Vowels");
                addCombo (prefix + "flt_v1", "to");
                addKnob (prefix + "flt_q", "Q", nullptr, false, 0.0, true);
                addKnob (prefix + "flt_f0", "Start Hz", nullptr, false, 0.0, true);
                addKnob (prefix + "flt_f1", "End Hz", nullptr, false, 0.0, true);
                addKnob (prefix + "flt_mix", "Mix", &primaryKnobs, false, 0.0, true);
                addWeights (prefix + "flt_", true);
                break;

            case EffectType::Quantizer:   // paired layout - see layoutKnobPairs(); Mix rides with the block-keys text
                addKnob (prefix + "qnt_bits", "Bits", nullptr, false, 0.0, true);
                addKnob (prefix + "qnt_bitsstd", "Bits Std", nullptr, false, 0.0, true);
                addKnob (prefix + "qnt_down", "Downsmp", nullptr, false, 0.0, true);
                addKnob (prefix + "qnt_downstd", "Down Std", nullptr, false, 0.0, true);
                addKnob (prefix + "qnt_mix", "Mix", &primaryKnobs, false, 0.0, true);
                break;

            case EffectType::RingMod:   // paired layout - see layoutKnobPairs(); Amount rides with the block-keys text
                addKnob (prefix + "ring_f0", "Start Hz", nullptr, false, 0.0, true);
                addKnob (prefix + "ring_f0std", "Start Std", nullptr, false, 0.0, true);
                addKnob (prefix + "ring_f1", "End Hz", nullptr, false, 0.0, true);
                addKnob (prefix + "ring_f1std", "End Std", nullptr, false, 0.0, true);
                addKnob (prefix + "ring_amp", "Amount", &primaryKnobs, false, 0.0, true);
                addWeights (prefix + "ring_", true);
                break;

            case EffectType::Reverser:
                addKnob (prefix + "rev_fade", "Fade", nullptr, false, 0.0, true);
                addKnob (prefix + "rev_mix", "Mix", &primaryKnobs, false, 0.0, true);
                addWeights (prefix + "rev_", true);
                break;

            case EffectType::Freeze:   // paired layout - see layoutKnobPairs(); Mix rides with the block-keys text
                addKnob (prefix + "frz_width", "Width", nullptr, false, 0.0, true);
                addKnob (prefix + "frz_widthstd", "Width Std", nullptr, false, 0.0, true);
                addKnob (prefix + "frz_mix", "Mix", &primaryKnobs, false, 0.0, true);
                addWeights (prefix + "frz_", true);   // retrigger grid
                break;

            case EffectType::AuxSend:   // paired layout - see layoutKnobPairs(); sends/Pass ride with the block-keys text
                addKnob (prefix + "aux_send1", "Aux 1", &primaryKnobs, false, 0.0, true);
                addKnob (prefix + "aux_send2", "Aux 2", &primaryKnobs, false, 0.0, true);
                addKnob (prefix + "aux_pass", "Pass", &primaryKnobs, false, 0.0, true);
                addKnob (prefix + "aux_att", "Attack", nullptr, false, 0.0, true);
                addKnob (prefix + "aux_attstd", "Att Std", nullptr, false, 0.0, true);
                addKnob (prefix + "aux_rel", "Release", nullptr, false, 0.0, true);
                addKnob (prefix + "aux_relstd", "Rel Std", nullptr, false, 0.0, true);
                addKnob (prefix + "aux_attcurve", "Att Curve", nullptr, true, 0.5, true);
                addKnob (prefix + "aux_attcurvestd", "AttCv Std", nullptr, false, 0.0, true);
                addKnob (prefix + "aux_relcurve", "Rel Curve", nullptr, true, 0.5, true);
                addKnob (prefix + "aux_relcurvestd", "RelCv Std", nullptr, false, 0.0, true);
                addWeights (prefix + "aux_", true);
                break;

            case EffectType::Panner:
                addCombo (prefix + "pan_mode", "Mode");
                addKnob (prefix + "pan_glide", "Glide", nullptr, false, 0.0, true);
                addKnob (prefix + "pan_mix", "Mix", &primaryKnobs, false, 0.0, true);
                addWeights (prefix + "pan_", true);
                break;
        }

        theme::styleInfo (info, accent);
        info.setInfo (effectTypeNames()[(int) type], helpFor (type));
        addAndMakeVisible (info);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (accent.withAlpha (0.6f));
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.0f);

        // All text in the lane's own accent, at varying alpha for hierarchy,
        // rather than a neutral grey/white - it reads as belonging to this
        // lane at a glance, which matters once several panels are visible.
        g.setColour (accent);
        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        g.drawText (title, r.removeFromTop (22).reduced (8, 0),
                    juce::Justification::centredLeft);

        if (! weightKnobs.empty())
        {
            g.setColour (accent.withAlpha (0.7f));
            g.setFont (juce::Font (juce::FontOptions (12.0f)));
            g.drawText ("duration probabilities", weightsLabelArea,
                        juce::Justification::centredLeft);
        }

        // The override-language keywords this effect understands, as a
        // reference for the block text entry below.
        if (! keywordsArea.isEmpty())
        {
            auto area = keywordsArea;
            g.setColour (accent.withAlpha (0.7f));
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawText ("block keys", area.removeFromTop (15),
                        juce::Justification::centredLeft);
            g.setColour (accent.withAlpha (0.85f));
            g.setFont (juce::Font (juce::FontOptions (12.5f)));
            g.drawFittedText (keywords, area, juce::Justification::topLeft, 3);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        r.removeFromTop (22);   // title

        // The info button belongs to the title text, so it is laid out in the
        // row paint() actually draws that text in — the *unpadded* top 22 px.
        // Deriving it from `r` instead would drop it 8 px below the text.
        auto titleRow = getLocalBounds().removeFromTop (22).reduced (8, 0);
        const auto titleFont = juce::Font (juce::FontOptions (14.0f, juce::Font::bold));
        const int titleWidth = juce::GlyphArrangement::getStringWidthInt (titleFont, title);
        titleRow.removeFromLeft (juce::jmin (titleWidth + theme::infoGap,
                                             juce::jmax (0, titleRow.getWidth() - theme::infoSize)));
        info.setBounds (titleRow.removeFromLeft (theme::infoSize)
                                .withSizeKeepingCentre (theme::infoSize, theme::infoSize));

        for (auto& c : combos)
        {
            auto row = r.removeFromTop (22);
            c.label.setBounds (row.removeFromLeft (52));
            c.box.setBounds (row);
            r.removeFromTop (3);
        }

        // Panels with mean/std pairs get them stacked (a size down from the
        // usual 70, matching the weight knobs below). Grain and Delay each
        // have one further knob (Fade, Porta) that doesn't pair, held out at
        // the original 70 in its own column - see layoutKnobPairs. Mix (or
        // Amount, or AuxSend's sends/Pass) never sits in this grid at all -
        // see primaryKnobs below. Everything else keeps the plain row layout.
        switch (type)
        {
            case EffectType::Gater:     layoutKnobPairs (knobs, r, 5, 52); break;
            case EffectType::Grain:     layoutKnobPairs (knobs, r, 5, 52, 70, 0, 1); break;   // Fade trailing
            case EffectType::Delay:     layoutKnobPairs (knobs, r, 5, 52, 70, 0, 1); break;   // Porta trailing
            case EffectType::Quantizer: layoutKnobPairs (knobs, r, 5, 52); break;
            case EffectType::RingMod:   layoutKnobPairs (knobs, r, 5, 52); break;
            case EffectType::Freeze:    layoutKnobPairs (knobs, r, 5, 52); break;
            case EffectType::AuxSend:   layoutKnobPairs (knobs, r, 5, 52); break;
            default:                    layoutKnobRow (knobs, r, 5, 70); break;   // Distortion/FilterEnv/Reverser/Panner
        }

        if (! weightKnobs.empty())
        {
            weightsLabelArea = r.removeFromTop (18);
            // 5/row keeps the two groups on their own lines: the five note
            // values, then the three modifiers. 4/row would spill w64 onto
            // the modifier row and read as if it were one.
            layoutKnobRow (weightKnobs, r, 5, 52);
        }

        keywordsArea = r.removeFromBottom (juce::jmin (50, r.getHeight())).reduced (2, 0);

        // Mix/Amount/the aux sends ride along the same band as the
        // block-keys text instead of taking a slot in the grid above -
        // squared off to that band's height so it keeps a sane aspect
        // ratio rather than the full-width stretch a lone knob got before.
        if (! primaryKnobs.empty())
        {
            const int box = keywordsArea.getHeight();
            auto primaryArea = keywordsArea.removeFromRight (box * (int) primaryKnobs.size());
            for (auto& k : primaryKnobs)
                k.slider->setBounds (primaryArea.removeFromLeft (box).reduced (2));
        }
    }

    int laneOf() const  { return laneIndex; }
    EffectType typeOf() const { return type; }

    /** Re-accents every control — the lane's colour follows its bus. */
    void setAccent (juce::Colour newAccent)
    {
        if (accent == newAccent)
            return;
        accent = newAccent;
        for (auto* list : { &knobs, &weightKnobs, &primaryKnobs })
            for (auto& k : *list)
                theme::styleKnob (*k.slider, k.slider->getName(), accent);
        for (auto& c : combos)
            theme::styleCombo (c.box, accent);
        theme::styleInfo (info, accent);
        repaint();
    }

private:
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    /** The override-language keys the effect reads (see the effect headers
        and OverrideParser.h — keep in sync when adding keys). */
    static juce::String overrideKeysFor (EffectType t)
    {
        static const juce::String weights ("w4 w8 w16 w32 w64 wstr wtrip wdot");
        switch (t)
        {
            case EffectType::Gater:      return "dur att rel attcurve relcurve mix\n" + weights;
            case EffectType::Grain:      return "dur att rel attcurve relcurve mix\n" + weights;
            case EffectType::Delay:      return "dur fb damp porta mix";
            case EffectType::Distortion: return "model drive bias sag gain mix";
            case EffectType::FilterEnv:  return "dur mode q f0 f1 v0 v1 mix\n" + weights;
            case EffectType::Quantizer:  return "bits down mix";
            case EffectType::RingMod:    return "dur f0 f1 amp\n" + weights;
            case EffectType::Reverser:   return "dur fade mix\n" + weights;
            case EffectType::Freeze:     return "mix width\n" + weights;
            case EffectType::AuxSend:    return "dur att rel attcurve relcurve aux1 aux2 pass\n" + weights;
            case EffectType::Panner:     return "dur mode glide mix\n" + weights;
        }
        return {};
    }

    /** What this effect does, for the info button's callout. Kept next to
        the control list so the two are edited together. */
    static juce::String helpFor (EffectType t)
    {
        static const juce::String drawn (
            "\n\nThe rate is not a fixed value: you weight how likely each "
            "note length is (1/4 to 1/64, straight / triplet / dotted) and "
            "the actual one is drawn at the start of each block. The draw "
            "depends only on the seed, the lane and the block - so every "
            "pass through the pattern plays exactly the sequence the block "
            "picture shows, and changing the seed re-rolls everything.");

        switch (t)
        {
            case EffectType::Gater:
                return "Chops the sound on and off: open, closed, open, ... "
                       "starting open, at the drawn rate.\n\n"
                       "Attack and Release are fractions of the open phase, not "
                       "fixed times, so the shape follows the rate. If they add "
                       "up to more than 1 they share the phase between them and "
                       "you get a triangle with no sustain. The curve knobs bend "
                       "the edges: 0 slow, 0.5 straight, 1 very fast - a fast "
                       "release sounds like a plucked decay.\n\n"
                       "Each of those four has a Std knob: every block draws its "
                       "own value around the mean instead of repeating it exactly "
                       "(0 keeps every block identical), so a rack of gated lanes "
                       "doesn't chop in lockstep." + drawn;

            case EffectType::Grain:
                return "Records a short grain when the block starts and loops it "
                       "for the whole block, so the sound freezes into a stutter, "
                       "repeating exactly at the drawn rate.\n\n"
                       "Attack and Release shape every repetition (not the block), "
                       "as fractions of one grain, and each has a Std knob: every "
                       "block draws its own value around the knob's mean instead of "
                       "repeating it exactly, so the stutter doesn't chop with an "
                       "identical envelope every pass (0 keeps it identical). Att Cv "
                       "and Rel Cv Std do the same to the curve knobs.\n\n"
                       "Fade is the seam crossfade - short keeps repeats tight, long "
                       "smooths a grain that isn't looping cleanly on its own, at the "
                       "cost of blurring the attack. Short grains give pitched "
                       "buzzes, long ones give stutters." + drawn;

            case EffectType::Delay:
                return "A feedback delay whose buffer keeps running between blocks, "
                       "so repeats carry over from one block to the next.\n\n"
                       "Damping darkens each repeat, and Porta is how fast the delay "
                       "time glides when it changes - short for clean pitch slides, "
                       "long for tape-style warps.\n\n"
                       "Set the delay time to one period of a MIDI note and push "
                       "feedback near 1 and it becomes a plucked string tuned by "
                       "your keyboard: type  dur=mididur fb=0.99  into the block.\n\n"
                       "Time, Feedback and Damping each have a Std knob: whenever one "
                       "is above 0 that knob stops gliding live and instead draws a "
                       "fresh value around its mean on every block entry (still "
                       "gliding into it via Porta) - 0 keeps tracking the knob "
                       "continuously, exactly as before.";

            case EffectType::Distortion:
                return "Tube-style saturation, in four flavours: Standard, Dynamic, "
                       "Triode and Class AB.\n\n"
                       "Drive sets how hard it is pushed, Bias moves the signal off "
                       "centre for asymmetric, odd-harmonic grit, and Sag imitates a "
                       "power supply buckling under loud passages.\n\n"
                       "How loud saturation comes out depends on how loud you feed "
                       "it, so there is no automatic make-up: Out dB is the manual "
                       "level trim, applied before the mix.";

            case EffectType::FilterEnv:
                return "A filter that sweeps from a start to an end frequency and "
                       "repeats at the drawn rate, so it moves in time with the "
                       "pattern.\n\n"
                       "In LP / HP mode Q sets the resonance at the sweep. In Formant "
                       "mode it glides between two vowels instead (a e i o u), which "
                       "makes the sound talk." + drawn;

            case EffectType::Quantizer:
                return "Lo-fi in two independent stages.\n\n"
                       "Bits throws away amplitude resolution (1 bit is a square "
                       "wave); Downsmp holds each sample for several samples, which "
                       "folds high frequencies down into the audible range. The "
                       "aliasing is the point - it is not filtered away.\n\n"
                       "Mix blends the crushed signal back with the clean one. Bits "
                       "Std and Down Std each draw a fresh value around the knob's "
                       "mean on every block entry (0 = deterministic), same "
                       "live-vs-drawn tradeoff as the delay's std knobs.";

            case EffectType::RingMod:
                return "Multiplies the sound by a sine carrier, which replaces its "
                       "harmonics with sum and difference tones - metallic, "
                       "bell-like, rarely in key.\n\n"
                       "The carrier glides from the start to the end frequency over "
                       "the drawn ramp and repeats for the block. Below about 20 Hz "
                       "it stops sounding like modulation and becomes tremolo.\n\n"
                       "Amount is this effect's mix: 0 is clean, 1 is full ring "
                       "modulation. Start Std and End Std draw each block's actual "
                       "frequency around the knob's mean (0 = deterministic), in Hz - "
                       "a small std goes further at the low end than the high." + drawn;

            case EffectType::Reverser:
                return "Cuts the incoming audio into slices and plays each one "
                       "backwards.\n\n"
                       "A slice has to be recorded before it can be reversed, so it "
                       "plays one slice behind - and the first slice of a block "
                       "passes through untouched, because there is nothing recorded "
                       "yet. Fade softens the joins between slices." + drawn;

            case EffectType::Freeze:
                return "Captures about 43 ms of sound when the block starts and holds "
                       "its spectrum as a still, non-repeating wash.\n\n"
                       "It is a spectral freeze, not a loop: there is no cycle to "
                       "hear. Blocks shorter than the capture stay dry, since it is "
                       "still recording.\n\n"
                       "The duration weights set a rhythmic retrigger: the wash is "
                       "re-captured from the live sound on that grid (drawn like "
                       "every other rate here), seamlessly, so it tracks a changing "
                       "input in time. The default is a straight 1/4; a block no "
                       "longer than one step is captured just once." + drawn +
                       "\n\nWidth sets how alike the two channels are - 1 fully "
                       "decorrelated and wide, 0 mono. The level stays constant "
                       "across the range. Width Std draws each block's actual value "
                       "around the knob's mean instead of repeating it exactly "
                       "(0 keeps it identical).";

            case EffectType::AuxSend:
                return "The gater's rhythm, but it routes the sound instead of "
                       "cutting it: while the gate is open the shaped signal is "
                       "added to the plugin's two aux outputs.\n\n"
                       "Enable Aux 1 / Aux 2 in your host to hear them - without "
                       "that, the sends go nowhere.\n\n"
                       "Pass is how much carries on down the main chain, and it is "
                       "a steady level, not gated: 1 leaves the lane transparent and "
                       "sends a copy on top, 0 takes the sound out of the main mix "
                       "entirely so only the aux outputs hear it.\n\n"
                       "Attack, Release and their curves each have a Std knob, same "
                       "as the gater: every block draws its own value around the "
                       "mean instead of repeating it exactly (0 keeps it identical)."
                       + drawn;

            case EffectType::Panner:
                return "Steps the sound between three positions - hard left, centre, "
                       "hard right - one per drawn step.\n\n"
                       "Mode picks the order: Cycle -> sweeps rightwards, Cycle <- "
                       "leftwards, Cycle <-> turns round at the edges instead of "
                       "jumping back across, and Random draws each step from the "
                       "seed (so it still repeats identically every pass).\n\n"
                       "Glide is how much of a step is spent travelling to the new "
                       "position. At 0 the jumps are instant and will click on "
                       "anything but silence." + drawn;
        }
        return {};
    }

    struct Knob
    {
        std::unique_ptr<fxme::FxmeSlider>  slider;
        std::unique_ptr<SliderAttachment>  att;
    };

    struct Combo
    {
        juce::ComboBox box;
        juce::Label    label;
        std::unique_ptr<ComboBoxAttachment> att;
    };

    // numberBox is a trial: EffectType::Gater currently opts in below, every
    // other panel still gets the default fxme::FxmeSlider. Same Theme
    // colours either way (see FxmeNumberBox.h) - only the shape changes.
    void addKnob (const juce::String& paramId, const juce::String& label,
                  std::vector<Knob>* target = nullptr, bool bipolar = false,
                  double centreValue = 0.0, bool numberBox = false)
    {
        auto& list = *(target != nullptr ? target : &knobs);
        auto& k = list.emplace_back();
        if (numberBox)
            k.slider = std::make_unique<fxme::FxmeNumberBox>();
        else
            k.slider = std::make_unique<fxme::FxmeSlider>();
        theme::styleKnob (*k.slider, label, accent, bipolar, centreValue);
        addAndMakeVisible (*k.slider);
        k.att = std::make_unique<SliderAttachment> (apvts, paramId, *k.slider);
    }

    void addCombo (const juce::String& paramId, const juce::String& label)
    {
        auto& c = combos.emplace_back();
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (paramId)))
            c.box.addItemList (choice->choices, 1);
        theme::styleCombo (c.box, accent);
        addAndMakeVisible (c.box);

        c.label.setText (label, juce::dontSendNotification);
        c.label.setColour (juce::Label::textColourId, theme::dimText);
        c.label.setFont (juce::Font (juce::FontOptions (12.0f)));
        addAndMakeVisible (c.label);

        c.att = std::make_unique<ComboBoxAttachment> (apvts, paramId, c.box);
    }

    void addWeights (const juce::String& weightPrefix, bool numberBox = false)
    {
        for (int b = 0; b < fxme::kNumNoteBases; ++b)
            addKnob (weightPrefix + DurationWeights::baseSuffixes[b],
                     DurationWeights::baseLabels[b], &weightKnobs, false, 0.0, numberBox);
        for (int m = 0; m < fxme::kNumNoteMods; ++m)
            addKnob (weightPrefix + DurationWeights::modSuffixes[m],
                     DurationWeights::modLabels[m], &weightKnobs, false, 0.0, numberBox);
    }

    static void layoutKnobRow (std::vector<Knob>& list, juce::Rectangle<int>& area,
                               int perRow, int rowHeight)
    {
        for (size_t i = 0; i < list.size(); i += (size_t) perRow)
        {
            auto row = area.removeFromTop (rowHeight);
            const int w = row.getWidth() / perRow;
            for (size_t j = i; j < juce::jmin (list.size(), i + (size_t) perRow); ++j)
                list[j].slider->setBounds (row.removeFromLeft (w).reduced (2, 0));
        }
    }

    /** Lays out `list` as `leadingSingles` independent controls, then pairs
        (a control immediately followed by its std knob, stacked in one
        column), then `trailingSingles` more independent controls - so the
        caller supplies the list pre-ordered to match (Grain: 4 pairs then
        Fade trailing; Delay: 3 pairs then Porta trailing; everyone else
        here is pure pairs). Each column is a fixed `rowArea.getWidth() /
        perRow` wide, same as layoutKnobRow uses - so a panel with fewer
        columns than perRow (Freeze's single pair, say) keeps every
        control's aspect ratio instead of stretching to fill the panel. A
        single gets a column of its own, sized `oddRowHeight` (defaults to
        `rowHeight`) and centred within the full two-row height rather than
        stretched into it. Wraps to further rows of columns once one is
        full, same as layoutKnobRow. */
    static void layoutKnobPairs (std::vector<Knob>& list, juce::Rectangle<int>& area,
                                 int perRow, int rowHeight, int oddRowHeight = -1,
                                 int leadingSingles = 0, int trailingSingles = 0)
    {
        if (oddRowHeight < 0)
            oddRowHeight = rowHeight;

        const int numPairs = ((int) list.size() - leadingSingles - trailingSingles) / 2;
        const int numCols  = leadingSingles + numPairs + trailingSingles;

        for (int firstCol = 0; firstCol < numCols; firstCol += perRow)
        {
            auto rowArea = area.removeFromTop (rowHeight * 2);
            const int colsHere = juce::jmin (perRow, numCols - firstCol);
            const int w = rowArea.getWidth() / perRow;
            for (int c = 0; c < colsHere; ++c)
            {
                auto col = rowArea.removeFromLeft (w).reduced (2, 0);
                const int colIdx = firstCol + c;

                if (colIdx < leadingSingles)
                {
                    list[(size_t) colIdx].slider->setBounds (
                        col.withSizeKeepingCentre (col.getWidth(), oddRowHeight));
                }
                else if (colIdx < leadingSingles + numPairs)
                {
                    const size_t i = (size_t) leadingSingles
                                    + (size_t) (colIdx - leadingSingles) * 2;
                    list[i].slider->setBounds     (col.removeFromTop (rowHeight));
                    list[i + 1].slider->setBounds (col.removeFromTop (rowHeight));
                }
                else
                {
                    const size_t i = (size_t) leadingSingles + (size_t) numPairs * 2
                                    + (size_t) (colIdx - leadingSingles - numPairs);
                    list[i].slider->setBounds (
                        col.withSizeKeepingCentre (col.getWidth(), oddRowHeight));
                }
            }
        }
    }

    juce::AudioProcessorValueTreeState& apvts;
    const int        laneIndex;
    const EffectType type;
    juce::Colour     accent;   // follows the lane's bus (setAccent)
    juce::String     title;

    std::vector<Knob>  knobs, weightKnobs, primaryKnobs;   // primaryKnobs: Mix/Amount/aux sends, by the block-keys text
    std::deque<Combo>  combos;
    fxme::InfoButton   info;
    juce::Rectangle<int> weightsLabelArea, keywordsArea;
    juce::String keywords;   // the keys this effect reads

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectPanel)
};

} // namespace mng
