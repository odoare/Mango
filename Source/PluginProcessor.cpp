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
#include <cstring>
#include <functional>
#include <string>

using namespace mng;

//==============================================================================
MangoAudioProcessor::MangoAudioProcessor()
    // The two aux outputs are declared disabled: a host (or the standalone
    // app) that only wires up a stereo pair keeps working untouched, and
    // enabling them is a deliberate act. Lanes running the AuxSend effect
    // feed them.
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Aux 1",  juce::AudioChannelSet::stereo(), false)
                      .withOutput ("Aux 2",  juce::AudioChannelSet::stereo(), false))
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

        // replaceState moved the config selector, which queued a recall:
        // cancel it, or the preset's live setup would be overwritten by
        // its own stored config.
        pendingRecall.store (-1);
        armedSlot = -1;
        noteConfigApplied (activeConfig());

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
    // The engine works on the main bus; the aux buses are pure destinations
    // (getTotalNumOutputChannels would count them and oversize everything).
    engine.prepare (sampleRate, samplesPerBlock, getMainBusNumOutputChannels());

    for (auto& tap : meters)
        for (auto& m : tap)
            m.prepare (sampleRate);

    silence.setSize (1, juce::jmax (1, samplesPerBlock));
    silence.clear();
}

void MangoAudioProcessor::releaseResources()
{
}

bool MangoAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (! ((in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo())
        && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
        && in.size() <= out.size()))
        return false;

    // Each aux output is either off or stereo — the send writes a pair.
    for (int bus = 1; bus <= 2; ++bus)
    {
        const auto aux = layouts.getChannelSet (false, bus);
        if (! (aux.isDisabled() || aux == juce::AudioChannelSet::stereo()))
            return false;
    }
    return true;
}

void MangoAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();

    auto main = getBusBuffer (buffer, false, 0);
    for (int ch = getMainBusNumInputChannels(); ch < main.getNumChannels(); ++ch)
        main.clear (ch, 0, numSamples);

    // The aux buses are additive destinations: whatever the host left in
    // them is not ours, so they start silent every block.
    // (setDataToReferTo, not assignment: these are non-owning views onto the
    // host's buffer — copy-assigning an AudioBuffer would allocate here.)
    juce::AudioBuffer<float> aux[2];
    juce::AudioBuffer<float>* auxPtr[2] { nullptr, nullptr };
    float* auxChannels[2][2] {};
    for (int bus = 0; bus < 2; ++bus)
        if (auto* b = getBus (false, bus + 1); b != nullptr && b->isEnabled())
        {
            const int base = getChannelIndexInProcessBlockBuffer (false, bus + 1, 0);
            const int n    = juce::jmin (2, b->getNumberOfChannels());
            for (int ch = 0; ch < n; ++ch)
                auxChannels[bus][ch] = buffer.getWritePointer (base + ch);
            aux[bus].setDataToReferTo (auxChannels[bus], n, numSamples);
            aux[bus].clear();
            auxPtr[bus] = &aux[bus];
        }

    // mididur: the period of the last note-on received. MIDI passes through.
    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn())
            engine.setMididurSeconds (
                (float) (1.0 / juce::MidiMessage::getMidiNoteInHertz (msg.getNoteNumber())));
    }

    // Input is metered before the engine runs — it processes `main` in place.
    feedMeter (MeterInput, main, numSamples, getMainBusNumInputChannels());

    engine.process (main, getPlayHead() != nullptr ? getPlayHead()->getPosition()
                                                   : juce::Optional<juce::AudioPlayHead::PositionInfo>(),
                    apvts.getRawParameterValue (pid::drywet)->load(),
                    auxPtr[0], auxPtr[1]);

    feedMeter (MeterOutput, main, numSamples);
    for (int bus = 0; bus < 2; ++bus)
        feedMeter (MeterAux1 + bus,
                   auxPtr[bus] != nullptr ? *auxPtr[bus] : silence, numSamples);
}

void MangoAudioProcessor::feedMeter (int tap, const juce::AudioBuffer<float>& source,
                                     int numSamples, int numChannels)
{
    const int sourceChannels = juce::jlimit (
        0, source.getNumChannels(),
        numChannels < 0 ? source.getNumChannels() : numChannels);
    if (sourceChannels < 1 || numSamples < 1)
        return;

    const int n = juce::jmin (numSamples, source.getNumSamples());
    for (int ch = 0; ch < 2; ++ch)
        meters[(size_t) tap][(size_t) ch].process (
            source.getReadPointer (juce::jmin (ch, sourceChannels - 1)), n);
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
        juce::StringArray { "Parallel", "3 after 1+2", "4 after 1-3",
                            "Diamond 1>2,3>4", "Fan-out 1>2,3,4" }, 0));

    // The config bank's only parameter: which slot is active. The configs
    // themselves live in the state tree (see the bank helpers below).
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        pid::config, "Config", 1, numConfigs, 1));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        pid::configsync, "Config Recall",
        juce::StringArray { "Immediate", "Next bar", "Next pattern" }, 0));
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
void MangoAudioProcessor::parameterChanged (const juce::String& id, float newValue)
{
    // May fire on any thread — only atomics / async triggers here.
    if (id == pid::stepsize || id == pid::numsteps)
    {
        triggerAsyncUpdate();   // grid applies on the message thread
        return;
    }

    if (id == pid::config)
    {
        // Queue the recall; the async handler applies it (message thread),
        // honouring the timing mode. A state/preset load cancels this.
        pendingRecall.store ((int) newValue - 1);
        triggerAsyncUpdate();
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

    const int slot = pendingRecall.exchange (-1);
    if (slot < 0)
        return;

    const int sync = (int) apvts.getRawParameterValue (pid::configsync)->load();
    if (sync == 0)
        applyConfig (slot);
    else
        armRecall (slot);
}

void MangoAudioProcessor::timerCallback()
{
    // Quantised recall: wait for the engine to cross the next boundary.
    if (armedSlot < 0)
    {
        stopTimer();
        return;
    }

    const int sync = (int) apvts.getRawParameterValue (pid::configsync)->load();
    const uint32_t now = sync == 2 ? engine.patternWrapCount() : engine.barWrapCount();

    // Boundaries only arrive while the plugin is processing; give up after
    // ~5 s (suspended / not rendering) rather than staying armed forever.
    if (now == armedAtWrap && ++armedTicks < 300)
        return;

    const int slot = armedSlot;
    armedSlot = -1;
    stopTimer();
    applyConfig (slot);
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

//==============================================================================
// Sequencer config bank. Configs live in the state tree (child "MangoBank"),
// so they cost no parameters, travel inside sessions and presets, and can
// hold the blocks — which are side state and could never be parameters.

juce::ValueTree MangoAudioProcessor::bankTree()
{
    return apvts.state.getOrCreateChildWithName ("MangoBank", nullptr);
}

juce::ValueTree MangoAudioProcessor::configTree (int slot) const
{
    const auto bank = apvts.state.getChildWithName ("MangoBank");
    if (bank.isValid())
        for (const auto& cfg : bank)
            if (cfg.hasType ("Config") && (int) cfg.getProperty ("index", -1) == slot)
                return cfg;
    return {};
}

bool MangoAudioProcessor::configIsStored (int slot) const
{
    return configTree (slot).isValid();
}

bool MangoAudioProcessor::configHasParams (int slot) const
{
    const auto cfg = configTree (slot);
    return cfg.isValid() && (bool) cfg.getProperty ("hasParams", false);
}

void MangoAudioProcessor::storeConfig (int slot, bool includeParams)
{
    if (slot < 0 || slot >= numConfigs)
        return;

    juce::ValueTree cfg ("Config");
    cfg.setProperty ("index", slot, nullptr);
    cfg.setProperty ("hasParams", includeParams, nullptr);

    juce::ValueTree params ("Params");
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto kind = configParamKind (rp->paramID);
            if (kind == ConfigParamKind::Always
                || (includeParams && kind == ConfigParamKind::Optional))
                params.setProperty (juce::Identifier (rp->paramID),
                                    apvts.getRawParameterValue (rp->paramID)->load(), nullptr);
        }
    cfg.appendChild (params, nullptr);
    cfg.appendChild (sequencersToTree(), nullptr);

    auto bank = bankTree();
    const auto previous = configTree (slot);
    undoSlot = slot;                                       // one level of undo:
    undoTree = previous.isValid() ? previous.createCopy()  // keep what we replace
                                  : juce::ValueTree();     // (invalid = was empty)
    if (previous.isValid())
        bank.removeChild (previous, nullptr);
    bank.appendChild (cfg, nullptr);

    // Storing makes the slot current, so the active marker follows the
    // click. The live setup already *is* that config, so cancel the recall
    // the selector move just queued.
    requestConfigRecallSelectorOnly (slot);
    pendingRecall.store (-1);
    armedSlot = -1;

    noteConfigApplied (slot);
    sendChangeMessage();
}

bool MangoAudioProcessor::configIncludeParams() const
{
    // Off by default: a config is a pattern, not a sound. Structure (grid,
    // routing, bus levels, seed) travels regardless.
    const auto bank = apvts.state.getChildWithName ("MangoBank");
    return bank.isValid() ? (bool) bank.getProperty ("includeParams", false) : false;
}

void MangoAudioProcessor::setConfigIncludeParams (bool shouldInclude)
{
    bankTree().setProperty ("includeParams", shouldInclude, nullptr);
    sendChangeMessage();
}

void MangoAudioProcessor::clearConfig (int slot)
{
    const auto cfg = configTree (slot);
    if (! cfg.isValid())
        return;

    undoSlot = slot;
    undoTree = cfg.createCopy();
    bankTree().removeChild (cfg, nullptr);
    sendChangeMessage();
}

void MangoAudioProcessor::undoStore()
{
    if (undoSlot < 0)
        return;

    auto bank = bankTree();
    const auto current = configTree (undoSlot);
    if (current.isValid())
        bank.removeChild (current, nullptr);
    if (undoTree.isValid())
        bank.appendChild (undoTree.createCopy(), nullptr);

    undoSlot = -1;
    undoTree = juce::ValueTree();
    sendChangeMessage();
}

int MangoAudioProcessor::activeConfig() const
{
    return (int) apvts.getRawParameterValue (pid::config)->load() - 1;
}

void MangoAudioProcessor::requestConfigRecall (int slot)
{
    if (slot < 0 || slot >= numConfigs)
        return;

    const bool alreadyActive = activeConfig() == slot;
    requestConfigRecallSelectorOnly (slot);

    // The parameter didn't move, so no recall was queued: force one, which
    // makes re-clicking the active slot a "revert to stored".
    if (alreadyActive)
    {
        pendingRecall.store (slot);
        triggerAsyncUpdate();
    }
}

void MangoAudioProcessor::requestConfigRecallSelectorOnly (int slot)
{
    if (auto* param = apvts.getParameter (pid::config))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 ((float) (slot + 1)));
        param->endChangeGesture();
    }
}

void MangoAudioProcessor::armRecall (int slot)
{
    const int sync = (int) apvts.getRawParameterValue (pid::configsync)->load();
    armedSlot   = slot;
    armedTicks  = 0;
    armedAtWrap = sync == 2 ? engine.patternWrapCount() : engine.barWrapCount();
    startTimerHz (60);   // lands within ~16 ms of the boundary
    sendChangeMessage(); // the panel shows the armed slot
}

void MangoAudioProcessor::applyConfig (int slot)
{
    const auto cfg = configTree (slot);
    if (! cfg.isValid())
        return;   // empty slot: never clobber the live setup

    const auto params = cfg.getChildWithName ("Params");
    for (int i = 0; i < params.getNumProperties(); ++i)
    {
        const auto id = params.getPropertyName (i);
        if (auto* param = apvts.getParameter (id.toString()))
            param->setValueNotifyingHost (
                param->convertTo0to1 ((float) (double) params.getProperty (id)));
    }

    applyGridFromParameters();   // grid before blocks, so ranges are right
    sequencersFromTree (cfg.getChildWithName ("MangoSeq"));

    noteConfigApplied (slot);
    sendChangeMessage();         // editor: reload everything
}

void MangoAudioProcessor::noteConfigApplied (int slot)
{
    signatureIncludesParams = configHasParams (slot);
    signatureAtApply = configSignature (signatureIncludesParams);
}

bool MangoAudioProcessor::configIsModified() const
{
    if (! configIsStored (activeConfig()))
        return false;
    return configSignature (signatureIncludesParams) != signatureAtApply;
}

juce::uint64 MangoAudioProcessor::configSignature (bool includeParams) const
{
    juce::uint64 h = 1469598103934665603ULL;          // FNV-1a
    auto mix = [&h] (juce::uint64 v) { h ^= v; h *= 1099511628211ULL; };

    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto kind = configParamKind (rp->paramID);
            if (kind == ConfigParamKind::Always
                || (includeParams && kind == ConfigParamKind::Optional))
            {
                const float v = apvts.getRawParameterValue (rp->paramID)->load();
                juce::uint32 bits;
                std::memcpy (&bits, &v, sizeof (bits));
                mix (bits);
            }
        }

    {
        const juce::ScopedLock sl (engine.lock());
        for (int lane : engine.laneOrder())
            mix ((juce::uint64) lane);
        for (int i = 0; i < numLanes; ++i)
            for (const auto& b : engine.sequencerFor (i).blocks())
            {
                mix ((juce::uint64) ((b.id << 20) ^ (b.startStep << 10) ^ b.endStep));
                mix ((juce::uint64) std::hash<std::string> {} (b.content));
            }
    }
    return h;
}

//==============================================================================
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

    // As in the preset hook: the restored session's live setup wins over
    // the config the selector happens to point at.
    pendingRecall.store (-1);
    armedSlot = -1;
    noteConfigApplied (activeConfig());

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
