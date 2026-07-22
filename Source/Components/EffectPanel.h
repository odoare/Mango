/*
  ------------------------------------------------------------------------------
    EffectPanel.h

    The right-column control panel for one (lane, effect type) pair: knobs,
    choice boxes and — for the duration-randomised effects — the 4+3
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
            case EffectType::Gater:
                addKnob (prefix + "gate_att", "Attack");
                addKnob (prefix + "gate_rel", "Release");
                addKnob (prefix + "gate_attcurve", "Att Curve");
                addKnob (prefix + "gate_relcurve", "Rel Curve");
                addKnob (prefix + "gate_mix", "Mix");
                addWeights (prefix + "gate_");
                break;

            case EffectType::Grain:
                addKnob (prefix + "grain_att", "Attack");
                addKnob (prefix + "grain_attcurve", "Att Curve");
                addKnob (prefix + "grain_rel", "Release");
                addKnob (prefix + "grain_relcurve", "Rel Curve");
                addKnob (prefix + "grain_mix", "Mix");
                addWeights (prefix + "grain_");
                break;

            case EffectType::Delay:
                addKnob (prefix + "dly_dur", "Time");
                addKnob (prefix + "dly_fb", "Feedback");
                addKnob (prefix + "dly_damp", "Damping");
                addKnob (prefix + "dly_porta", "Porta ms");
                addKnob (prefix + "dly_mix", "Mix");
                break;

            case EffectType::Distortion:
                addCombo (prefix + "dist_model", "Model");
                addKnob (prefix + "dist_drive", "Drive");
                addKnob (prefix + "dist_bias", "Bias");
                addKnob (prefix + "dist_sag", "Sag");
                addKnob (prefix + "dist_gain", "Out dB");
                addKnob (prefix + "dist_mix", "Mix");
                break;

            case EffectType::FilterEnv:
                addCombo (prefix + "flt_mode", "Mode");
                addCombo (prefix + "flt_v0", "Vowels");
                addCombo (prefix + "flt_v1", "to");
                addKnob (prefix + "flt_q", "Q");
                addKnob (prefix + "flt_f0", "Start Hz");
                addKnob (prefix + "flt_f1", "End Hz");
                addKnob (prefix + "flt_mix", "Mix");
                addWeights (prefix + "flt_");
                break;

            case EffectType::Quantizer:
                addKnob (prefix + "qnt_bits", "Bits");
                addKnob (prefix + "qnt_down", "Downsmp");
                addKnob (prefix + "qnt_mix", "Mix");
                break;

            case EffectType::RingMod:
                addKnob (prefix + "ring_f0", "Start Hz");
                addKnob (prefix + "ring_f1", "End Hz");
                addKnob (prefix + "ring_amp", "Amount");
                addWeights (prefix + "ring_");
                break;

            case EffectType::Reverser:
                addKnob (prefix + "rev_fade", "Fade");
                addKnob (prefix + "rev_mix", "Mix");
                addWeights (prefix + "rev_");
                break;

            case EffectType::Freeze:
                addKnob (prefix + "frz_mix", "Mix");
                addKnob (prefix + "frz_width", "Width");
                break;

            case EffectType::AuxSend:
                addKnob (prefix + "aux_send1", "Aux 1");
                addKnob (prefix + "aux_send2", "Aux 2");
                addKnob (prefix + "aux_pass", "Pass");
                addKnob (prefix + "aux_att", "Attack");
                addKnob (prefix + "aux_rel", "Release");
                addKnob (prefix + "aux_attcurve", "Att Curve");
                addKnob (prefix + "aux_relcurve", "Rel Curve");
                addWeights (prefix + "aux_");
                break;

            case EffectType::Panner:
                addCombo (prefix + "pan_mode", "Mode");
                addKnob (prefix + "pan_glide", "Glide");
                addKnob (prefix + "pan_mix", "Mix");
                addWeights (prefix + "pan_");
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

        g.setColour (theme::text);
        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        g.drawText (title, r.removeFromTop (22).reduced (8, 0),
                    juce::Justification::centredLeft);

        if (! weightKnobs.empty())
        {
            g.setColour (theme::dimText);
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawText ("duration probabilities", weightsLabelArea,
                        juce::Justification::centredLeft);
        }

        // The override-language keywords this effect understands, as a
        // reference for the block text entry below.
        if (! keywordsArea.isEmpty())
        {
            auto area = keywordsArea;
            g.setColour (theme::dimText);
            g.setFont (juce::Font (juce::FontOptions (10.0f)));
            g.drawText ("block keys", area.removeFromTop (13),
                        juce::Justification::centredLeft);
            g.setColour (theme::text.withAlpha (0.8f));
            g.setFont (juce::Font (juce::FontOptions (11.5f)));
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

        layoutKnobRow (knobs, r, 5, 70);   // 5/row: the widest sets keep one row

        if (! weightKnobs.empty())
        {
            weightsLabelArea = r.removeFromTop (16);
            layoutKnobRow (weightKnobs, r, 4, 52);
        }

        keywordsArea = r.removeFromBottom (juce::jmin (46, r.getHeight())).reduced (2, 0);
    }

    int laneOf() const  { return laneIndex; }
    EffectType typeOf() const { return type; }

    /** Re-accents every control — the lane's colour follows its bus. */
    void setAccent (juce::Colour newAccent)
    {
        if (accent == newAccent)
            return;
        accent = newAccent;
        for (auto* list : { &knobs, &weightKnobs })
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
        static const juce::String weights ("w4 w8 w16 w32 wstr wtrip wdot");
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
            case EffectType::Freeze:     return "mix width";
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
            "note length is (1/4 to 1/32, straight / triplet / dotted) and "
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
                       "release sounds like a plucked decay." + drawn;

            case EffectType::Grain:
                return "Records a short grain when the block starts and loops it "
                       "for the whole block, so the sound freezes into a stutter.\n\n"
                       "Attack and Release shape every repetition (not the block), "
                       "as fractions of one grain. Seams are crossfaded over 15 ms. "
                       "Short grains give pitched buzzes, long ones give stutters." + drawn;

            case EffectType::Delay:
                return "A feedback delay whose buffer keeps running between blocks, "
                       "so repeats carry over from one block to the next.\n\n"
                       "Damping darkens each repeat, and Porta is how fast the delay "
                       "time glides when it changes - short for clean pitch slides, "
                       "long for tape-style warps.\n\n"
                       "Set the delay time to one period of a MIDI note and push "
                       "feedback near 1 and it becomes a plucked string tuned by "
                       "your keyboard: type  dur=mididur fb=0.99  into the block.";

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
                       "Mix blends the crushed signal back with the clean one.";

            case EffectType::RingMod:
                return "Multiplies the sound by a sine carrier, which replaces its "
                       "harmonics with sum and difference tones - metallic, "
                       "bell-like, rarely in key.\n\n"
                       "The carrier glides from the start to the end frequency over "
                       "the drawn ramp and repeats for the block. Below about 20 Hz "
                       "it stops sounding like modulation and becomes tremolo.\n\n"
                       "Amount is this effect's mix: 0 is clean, 1 is full ring "
                       "modulation." + drawn;

            case EffectType::Reverser:
                return "Cuts the incoming audio into slices and plays each one "
                       "backwards.\n\n"
                       "A slice has to be recorded before it can be reversed, so it "
                       "plays one slice behind - and the first slice of a block "
                       "passes through untouched, because there is nothing recorded "
                       "yet. Fade softens the joins between slices." + drawn;

            case EffectType::Freeze:
                return "Captures about 43 ms of sound when the block starts and holds "
                       "its spectrum as a still, non-repeating wash for the rest of "
                       "the block.\n\n"
                       "It is a spectral freeze, not a loop: there is no cycle to "
                       "hear. Blocks shorter than the capture stay dry, since it is "
                       "still recording.\n\n"
                       "Width sets how alike the two channels are - 1 fully "
                       "decorrelated and wide, 0 mono. The level stays constant "
                       "across the range.";

            case EffectType::AuxSend:
                return "The gater's rhythm, but it routes the sound instead of "
                       "cutting it: while the gate is open the shaped signal is "
                       "added to the plugin's two aux outputs.\n\n"
                       "Enable Aux 1 / Aux 2 in your host to hear them - without "
                       "that, the sends go nowhere.\n\n"
                       "Pass is how much carries on down the main chain, and it is "
                       "a steady level, not gated: 1 leaves the lane transparent and "
                       "sends a copy on top, 0 takes the sound out of the main mix "
                       "entirely so only the aux outputs hear it." + drawn;

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

    void addKnob (const juce::String& paramId, const juce::String& label,
                  std::vector<Knob>* target = nullptr)
    {
        auto& list = *(target != nullptr ? target : &knobs);
        auto& k = list.emplace_back();
        k.slider = std::make_unique<fxme::FxmeSlider>();
        theme::styleKnob (*k.slider, label, accent);
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

    void addWeights (const juce::String& weightPrefix)
    {
        for (int b = 0; b < fxme::kNumNoteBases; ++b)
            addKnob (weightPrefix + DurationWeights::baseSuffixes[b],
                     DurationWeights::baseLabels[b], &weightKnobs);
        for (int m = 0; m < fxme::kNumNoteMods; ++m)
            addKnob (weightPrefix + DurationWeights::modSuffixes[m],
                     DurationWeights::modLabels[m], &weightKnobs);
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

    juce::AudioProcessorValueTreeState& apvts;
    const int        laneIndex;
    const EffectType type;
    juce::Colour     accent;   // follows the lane's bus (setAccent)
    juce::String     title;

    std::vector<Knob>  knobs, weightKnobs;
    std::deque<Combo>  combos;
    fxme::InfoButton   info;
    juce::Rectangle<int> weightsLabelArea, keywordsArea;
    juce::String keywords;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectPanel)
};

} // namespace mng
