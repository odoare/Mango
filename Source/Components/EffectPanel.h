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
                addWeights (prefix + "gate_");
                break;

            case EffectType::Grain:
                addKnob (prefix + "grain_fade", "Fade");
                addKnob (prefix + "grain_att", "Attack");
                addKnob (prefix + "grain_attcurve", "Att Curve");
                addKnob (prefix + "grain_rel", "Release");
                addKnob (prefix + "grain_relcurve", "Rel Curve");
                addWeights (prefix + "grain_");
                break;

            case EffectType::Delay:
                addKnob (prefix + "dly_dur", "Time");
                addKnob (prefix + "dly_fb", "Feedback");
                addKnob (prefix + "dly_damp", "Damping");
                addKnob (prefix + "dly_porta", "Porta ms");
                break;

            case EffectType::Distortion:
                addCombo (prefix + "dist_model", "Model");
                addKnob (prefix + "dist_drive", "Drive");
                addKnob (prefix + "dist_bias", "Bias");
                addKnob (prefix + "dist_sag", "Sag");
                addKnob (prefix + "dist_mix", "Mix");
                break;

            case EffectType::FilterEnv:
                addCombo (prefix + "flt_mode", "Mode");
                addCombo (prefix + "flt_v0", "Vowels");
                addCombo (prefix + "flt_v1", "to");
                addKnob (prefix + "flt_q", "Q");
                addKnob (prefix + "flt_f0", "Start Hz");
                addKnob (prefix + "flt_f1", "End Hz");
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
                addWeights (prefix + "rev_");
                break;

            case EffectType::Freeze:
                addKnob (prefix + "frz_mix", "Mix");
                break;
        }
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

        for (auto& c : combos)
        {
            auto row = r.removeFromTop (22);
            c.label.setBounds (row.removeFromLeft (52));
            c.box.setBounds (row);
            r.removeFromTop (3);
        }

        layoutKnobRow (knobs, r, 3, 70);

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
            case EffectType::Gater:      return "dur att rel attcurve relcurve\n" + weights;
            case EffectType::Grain:      return "dur fade att rel attcurve relcurve\n" + weights;
            case EffectType::Delay:      return "dur fb damp porta";
            case EffectType::Distortion: return "model drive bias sag mix";
            case EffectType::FilterEnv:  return "dur mode q f0 f1 v0 v1\n" + weights;
            case EffectType::Quantizer:  return "bits down mix";
            case EffectType::RingMod:    return "dur f0 f1 amp\n" + weights;
            case EffectType::Reverser:   return "dur fade\n" + weights;
            case EffectType::Freeze:     return "mix";
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
    juce::Rectangle<int> weightsLabelArea, keywordsArea;
    juce::String keywords;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectPanel)
};

} // namespace mng
