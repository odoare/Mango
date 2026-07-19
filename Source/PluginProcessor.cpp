/*
  ------------------------------------------------------------------------------
    PluginProcessor.cpp
    Author: Olivier Doaré, github.com/odoare
    SPDX-License-Identifier: LGPL-3.0-or-later
  ------------------------------------------------------------------------------
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamIDs.h"

using namespace mng;

//==============================================================================
MangoAudioProcessor::MangoAudioProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    engine.bindParameters (apvts);

    // Listen to every parameter so changes take effect on the sounding
    // blocks immediately (see parameterChanged).
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            apvts.addParameterListener (rp->paramID, this);

    applyGridFromParameters();

    // Presets are the plain APVTS state, so the sequencer blocks (side
    // state) are merged in before a save and rebuilt after a load — the
    // same dance as get/setStateInformation.
    presetManager.onBeforeSave = [this]
    {
        apvts.state.removeChild (apvts.state.getChildWithName ("MangoSeq"), nullptr);
        apvts.state.appendChild (sequencersToTree(), nullptr);
    };
    presetManager.onAfterLoad = [this]
    {
        applyGridFromParameters();   // grid before blocks, so ranges are right
        sequencersFromTree (apvts.state.getChildWithName ("MangoSeq"));
        sendChangeMessage();         // editor: reload everything
    };
}

MangoAudioProcessor::~MangoAudioProcessor()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            apvts.removeParameterListener (rp->paramID, this);
}

//==============================================================================
const juce::String MangoAudioProcessor::getName() const   { return JucePlugin_Name; }
bool MangoAudioProcessor::acceptsMidi() const              { return true; }
bool MangoAudioProcessor::producesMidi() const             { return false; }
bool MangoAudioProcessor::isMidiEffect() const             { return false; }
double MangoAudioProcessor::getTailLengthSeconds() const   { return 0.1; }

int MangoAudioProcessor::getNumPrograms()                  { return 1; }
int MangoAudioProcessor::getCurrentProgram()               { return 0; }
void MangoAudioProcessor::setCurrentProgram (int)          {}
const juce::String MangoAudioProcessor::getProgramName (int) { return {}; }
void MangoAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void MangoAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
}

void MangoAudioProcessor::releaseResources()
{
}

bool MangoAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo())
        && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
        && in.size() <= out.size();
}

void MangoAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // mididur: the period of the last note-on received. MIDI passes through.
    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn())
            engine.setMididurSeconds (
                (float) (1.0 / juce::MidiMessage::getMidiNoteInHertz (msg.getNoteNumber())));
    }

    engine.process (buffer, getPlayHead() != nullptr ? getPlayHead()->getPosition()
                                                     : juce::Optional<juce::AudioPlayHead::PositionInfo>(),
                    apvts.getRawParameterValue (pid::drywet)->load());
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MangoAudioProcessor::createParameters()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        pid::drywet, "Dry/Wet",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));

    params.push_back (std::make_unique<juce::AudioParameterInt> (
        pid::seed, "Seed", 0, 99999, 0));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        pid::stepsize, "Step size",
        juce::StringArray { "1/16", "1/8", "1/4", "1/2", "1/1" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterInt> (
        pid::numsteps, "Steps", 1, 64, 16));

    params.push_back (std::make_unique<juce::AudioParameterInt> (
        pid::numlanes, "Lanes", 1, numLanes, defaultNumLanes));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        pid::busmode, "Bus Routing",
        juce::StringArray { "Parallel", "3 after 1+2", "4 after 1-3" }, 0));
    for (int b = 0; b < numBuses; ++b)
    {
        const auto nameP = "Bus " + juce::String (b + 1) + " ";
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            pid::busWet (b), nameP + "Wet",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            pid::busPan (b), nameP + "Pan",
            juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f));
    }

    MangoEngine::addLaneParameters (params);

    return { params.begin(), params.end() };
}

//==============================================================================
void MangoAudioProcessor::parameterChanged (const juce::String& id, float)
{
    // May fire on any thread — only atomics / async triggers here.
    if (id == pid::stepsize || id == pid::numsteps)
    {
        triggerAsyncUpdate();   // grid applies on the message thread
        return;
    }

    if (id == pid::seed)
    {
        engine.noteGlobalParamsChanged();
        return;
    }

    // Per-lane parameters ("l<i>_..."): refresh that lane's sounding block.
    if (id.length() > 2 && id[0] == 'l' && id[2] == '_')
        engine.noteLaneParamsChanged (id.substring (1, 2).getIntValue());
}

void MangoAudioProcessor::handleAsyncUpdate()
{
    applyGridFromParameters();
    sendChangeMessage();    // let the editor repaint the grids
}

void MangoAudioProcessor::applyGridFromParameters()
{
    const auto stepSize = (fxme::SeqStepSize) (int) apvts.getRawParameterValue (pid::stepsize)->load();
    const int  numSteps = (int) apvts.getRawParameterValue (pid::numsteps)->load();
    engine.setGrid (stepSize, numSteps);
}

//==============================================================================
bool MangoAudioProcessor::setBlockContent (int laneIndex, int blockId, const juce::String& text)
{
    {
        const juce::ScopedLock sl (engine.lock());
        engine.sequencerFor (laneIndex).setContent (blockId, text.toStdString());
    }
    engine.rebuildOverrides();
    return ! engine.blockHasParseError (laneIndex, blockId);
}

juce::String MangoAudioProcessor::blockContent (int laneIndex, int blockId) const
{
    const juce::ScopedLock sl (engine.lock());
    if (const auto* b = engine.sequencerFor (laneIndex).blockById (blockId))
        return juce::String (b->content);
    return {};
}

//==============================================================================
juce::ValueTree MangoAudioProcessor::sequencersToTree() const
{
    const juce::ScopedLock sl (engine.lock());

    juce::ValueTree tree ("MangoSeq");

    juce::StringArray orderStrings;
    for (int lane : engine.laneOrder())
        orderStrings.add (juce::String (lane));
    tree.setProperty ("laneOrder", orderStrings.joinIntoString (" "), nullptr);

    for (int i = 0; i < numLanes; ++i)
    {
        juce::ValueTree laneTree ("Lane");
        laneTree.setProperty ("index", i, nullptr);
        for (const auto& b : engine.sequencerFor (i).blocks())
        {
            juce::ValueTree blockTree ("Block");
            blockTree.setProperty ("id",    b.id, nullptr);
            blockTree.setProperty ("start", b.startStep, nullptr);
            blockTree.setProperty ("len",   b.endStep - b.startStep, nullptr);
            blockTree.setProperty ("text",  juce::String (b.content), nullptr);
            laneTree.appendChild (blockTree, nullptr);
        }
        tree.appendChild (laneTree, nullptr);
    }
    return tree;
}

void MangoAudioProcessor::sequencersFromTree (const juce::ValueTree& tree)
{
    if (! tree.hasType ("MangoSeq"))
        return;

    {
        const juce::ScopedLock sl (engine.lock());

        // Identity default; a saved order with fewer entries (a session from
        // a lower-lane-count build) fills the leading rows and leaves the
        // rest in place, which stays a valid permutation.
        std::array<int, numLanes> order {};
        for (int i = 0; i < numLanes; ++i)
            order[(size_t) i] = i;
        const auto orderStrings = juce::StringArray::fromTokens (
            tree.getProperty ("laneOrder").toString(), " ", "");
        for (int i = 0; i < juce::jmin ((int) orderStrings.size(), (int) numLanes); ++i)
            order[(size_t) i] = orderStrings[i].getIntValue();
        engine.setLaneOrder (order);

        for (const auto& laneTree : tree)
        {
            if (! laneTree.hasType ("Lane"))
                continue;
            const int laneIndex = (int) laneTree.getProperty ("index", -1);
            if (laneIndex < 0 || laneIndex >= numLanes)
                continue;

            auto& seq = engine.sequencerFor (laneIndex);
            seq.clear();
            for (const auto& blockTree : laneTree)
            {
                if (! blockTree.hasType ("Block"))
                    continue;
                const int id    = (int) blockTree.getProperty ("id", -1);
                const int start = (int) blockTree.getProperty ("start", 0);
                const int len   = (int) blockTree.getProperty ("len", 1);
                if (seq.addBlockWithId (id, start, len))
                    seq.setContent (id, blockTree.getProperty ("text").toString().toStdString());
            }
        }
    }

    engine.rebuildOverrides();
}

void MangoAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    state.removeChild (state.getChildWithName ("MangoSeq"), nullptr);
    state.appendChild (sequencersToTree(), nullptr);

    juce::MemoryOutputStream mos (destData, true);
    state.writeToStream (mos);
}

void MangoAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto tree = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (! tree.isValid())
        return;

    const auto seqTree = tree.getChildWithName ("MangoSeq");
    tree.removeChild (seqTree, nullptr);
    apvts.replaceState (tree);

    applyGridFromParameters();   // grid before blocks, so ranges are right
    sequencersFromTree (seqTree);
    sendChangeMessage();         // editor: reload everything
}

//==============================================================================
bool MangoAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* MangoAudioProcessor::createEditor()
{
    return new MangoAudioProcessorEditor (*this);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MangoAudioProcessor();
}
