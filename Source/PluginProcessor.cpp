#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

namespace
{
    String tenScale (float v, int)  { return String (v * 10.0f, 1); }
    String percent (float v, int)   { return String (juce::roundToInt (v * 100.0f)) + " %"; }
}

AudioProcessorValueTreeState::ParameterLayout MurliProcessor::createLayout()
{
    using P = AudioParameterFloat;
    using A = AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<RangedAudioParameter>> ps;

    auto add = [&] (const char* id, const char* name, NormalisableRange<float> range, float def, A attrs)
    {
        ps.push_back (std::make_unique<P> (ParameterID { id, 1 }, name, range, def, attrs));
    };

    add (ParamID::volume,  "Volume",  { 0.0f, 1.0f }, 0.7f, A().withStringFromValueFunction (tenScale));
    add (ParamID::vibrato, "Vibrato", { 0.0f, 1.0f }, 0.0f, A().withStringFromValueFunction (tenScale));
    add (ParamID::speed,   "Speed",   { 3.0f, 9.0f }, 5.63f,
         A().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " Hz"; }));
    add (ParamID::bark,    "Bark",    { 0.4f, 1.5f }, 1.0f, A().withStringFromValueFunction (percent));
    add (ParamID::drive,   "Drive",   { -12.0f, 12.0f }, 0.0f,
         A().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " dB"; }));
    add (ParamID::attack,  "Attack",  { 0.0f, 2.0f }, 1.0f, A().withStringFromValueFunction (percent));
    add (ParamID::speaker, "Speaker", { 0.0f, 1.0f }, 1.0f,
         A().withStringFromValueFunction ([] (float v, int)
         {
             return v < 0.01f ? String ("Direct") : v > 0.99f ? String ("Speakers") : percent (v, 0);
         }));
    add (ParamID::noise,   "Noise",   { 0.0f, 1.0f }, 0.0f, A().withStringFromValueFunction (tenScale));
    add (ParamID::tune,    "Tune",    { 430.0f, 450.0f }, 440.0f,
         A().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) { return "A=" + String (v, 1); }));

    // --- effects loop pedals ---
    auto toggle = [&] (const char* id, const char* name)
    {
        ps.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, false));
    };
    auto choice = [&] (const char* id, const char* name, StringArray items, int def)
    {
        ps.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def));
    };

    toggle (ParamID::chOn, "Chorus On");
    add (ParamID::chRate, "Chorus Rate", NormalisableRange<float> (0.05f, 8.0f, 0.0f, 0.35f), 0.8f,
         A().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) { return String (v, v < 1 ? 2 : 1) + " Hz"; }));
    add (ParamID::chShape, "Chorus Shape", { 0.0f, 1.0f }, 0.0f,
         A().withStringFromValueFunction ([] (float v, int) { return v < 0.05f ? String ("Triangle") : v > 0.95f ? String ("Square") : percent (v, 0); }));
    add (ParamID::chDepth, "Chorus Depth", { 0.0f, 1.0f }, 0.5f, A().withStringFromValueFunction (tenScale));
    add (ParamID::chDimension, "Chorus Dimension", { 0.0f, 1.0f }, 0.15f, A().withStringFromValueFunction (tenScale));
    add (ParamID::chAnimate, "Chorus Animate", { 0.0f, 1.0f }, 0.35f, A().withStringFromValueFunction (tenScale));
    add (ParamID::chIntensity, "Chorus Intensity", { 0.0f, 1.0f }, 0.6f, A().withStringFromValueFunction (tenScale));

    toggle (ParamID::ecOn, "Echo On");
    add (ParamID::ecTime, "Echo Time", { 0.5f, 1.5f }, 1.0f,
         A().withStringFromValueFunction ([] (float v, int) { return String (juce::roundToInt (75.0f * v)) + " ms/head"; }));
    add (ParamID::ecSwell, "Echo Swell", { 0.0f, 1.0f }, 0.35f, A().withStringFromValueFunction (tenScale));
    choice (ParamID::ecHeads, "Echo Heads",
            { "1", "2", "3", "4", "1+2", "2+3", "3+4", "1+3", "2+4", "1+2+3", "2+3+4", "1+2+3+4" }, 3);
    choice (ParamID::ecMode, "Echo Mode", { "Echo", "Repeat", "Swell" }, 0);
    add (ParamID::ecTone, "Echo Tone", { 0.0f, 1.0f }, 0.5f, A().withStringFromValueFunction (tenScale));
    add (ParamID::ecMix, "Echo Mix", { 0.0f, 1.0f }, 0.4f, A().withStringFromValueFunction (tenScale));

    toggle (ParamID::rvOn, "Spring On");
    add (ParamID::rvDwell, "Spring Dwell", { 0.0f, 1.0f }, 0.5f, A().withStringFromValueFunction (tenScale));
    add (ParamID::rvDecay, "Spring Decay", { 0.0f, 1.0f }, 0.5f,
         A().withStringFromValueFunction ([] (float v, int) { return String (1.2 * std::pow (3.75, (double) v), 1) + " s"; }));
    add (ParamID::rvTone, "Spring Tone", { 0.0f, 1.0f }, 0.5f, A().withStringFromValueFunction (tenScale));
    add (ParamID::rvMix, "Spring Mix", { 0.0f, 1.0f }, 0.35f, A().withStringFromValueFunction (tenScale));

    return { ps.begin(), ps.end() };
}

MurliProcessor::MurliProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MURLI", createLayout())
{
    p.volume  = apvts.getRawParameterValue (ParamID::volume);
    p.vibrato = apvts.getRawParameterValue (ParamID::vibrato);
    p.speed   = apvts.getRawParameterValue (ParamID::speed);
    p.bark    = apvts.getRawParameterValue (ParamID::bark);
    p.drive   = apvts.getRawParameterValue (ParamID::drive);
    p.attack  = apvts.getRawParameterValue (ParamID::attack);
    p.speaker = apvts.getRawParameterValue (ParamID::speaker);
    p.noise   = apvts.getRawParameterValue (ParamID::noise);
    p.tune    = apvts.getRawParameterValue (ParamID::tune);
    auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    p.chOn = raw (ParamID::chOn); p.chRate = raw (ParamID::chRate); p.chShape = raw (ParamID::chShape);
    p.chDepth = raw (ParamID::chDepth); p.chDimension = raw (ParamID::chDimension);
    p.chAnimate = raw (ParamID::chAnimate); p.chIntensity = raw (ParamID::chIntensity);
    p.ecOn = raw (ParamID::ecOn); p.ecTime = raw (ParamID::ecTime); p.ecSwell = raw (ParamID::ecSwell);
    p.ecHeads = raw (ParamID::ecHeads); p.ecMode = raw (ParamID::ecMode); p.ecTone = raw (ParamID::ecTone);
    p.ecMix = raw (ParamID::ecMix);
    p.rvOn = raw (ParamID::rvOn); p.rvDwell = raw (ParamID::rvDwell); p.rvDecay = raw (ParamID::rvDecay);
    p.rvTone = raw (ParamID::rvTone); p.rvMix = raw (ParamID::rvMix);
    apvts.state.setProperty ("finish", "Coral", nullptr);
    apvts.state.setProperty ("rear", false, nullptr);
}

bool MurliProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == AudioChannelSet::mono() || out == AudioChannelSet::stereo();
}

void MurliProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    rightScratch.assign ((size_t) std::max (samplesPerBlock, 1), 0.0f);
    setLatencySamples (0);
    engine.prepare (sampleRate);
    keyboardState.reset();
}

void MurliProcessor::handleMidi (const MidiMessage& m)
{
    if (m.isNoteOn())                   engine.noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())             engine.noteOff (m.getNoteNumber());
    else if (m.isSustainPedalOn())      engine.setSustain (true);
    else if (m.isSustainPedalOff())     engine.setSustain (false);
    else if (m.isAllSoundOff())         engine.panic();
    else if (m.isAllNotesOff() || m.isResetAllControllers()) engine.allNotesOff();
}

void MurliProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    // Merge the on-screen keyboard (and QWERTY keys) into the incoming MIDI.
    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    murli::EngineParams ep;
    ep.volume  = p.volume->load();
    ep.vibrato = p.vibrato->load();
    ep.speed   = p.speed->load();
    ep.bark    = p.bark->load();
    ep.driveDb = p.drive->load();
    ep.attack  = p.attack->load();
    ep.speaker = p.speaker->load();
    ep.noise   = p.noise->load();
    ep.tuneA4  = p.tune->load();

    auto& c = ep.pedals.chorus;
    c.on = p.chOn->load() > 0.5f; c.rate = p.chRate->load(); c.shape = p.chShape->load(); c.depth = p.chDepth->load();
    c.dimension = p.chDimension->load(); c.animate = p.chAnimate->load(); c.intensity = p.chIntensity->load();
    auto& e = ep.pedals.echo;
    e.on = p.ecOn->load() > 0.5f; e.time = p.ecTime->load(); e.swell = p.ecSwell->load();
    e.heads = (int) p.ecHeads->load(); e.mode = (int) p.ecMode->load(); e.tone = p.ecTone->load(); e.mix = p.ecMix->load();
    auto& r = ep.pedals.reverb;
    r.on = p.rvOn->load() > 0.5f; r.dwell = p.rvDwell->load(); r.decay = p.rvDecay->load();
    r.tone = p.rvTone->load(); r.mix = p.rvMix->load();
    engine.setParams (ep);

    // A mono host bus gets the two sides summed; the right side then renders into scratch.
    const bool stereo = buffer.getNumChannels() > 1;
    if (! stereo && (int) rightScratch.size() < numSamples)
        rightScratch.resize ((size_t) numSamples);   // only if the host broke its block-size promise
    float* left = buffer.getWritePointer (0);
    float* right = stereo ? buffer.getWritePointer (1) : rightScratch.data();

    // Render between events so every note starts on its exact sample.
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = jlimit (0, numSamples, meta.samplePosition);
        if (at > pos) { engine.render (left + pos, right + pos, at - pos); pos = at; }
        handleMidi (meta.getMessage());
    }
    if (pos < numSamples)
        engine.render (left + pos, right + pos, numSamples - pos);

    if (! stereo)
        for (int i = 0; i < numSamples; ++i) left[i] = 0.5f * (left[i] + right[i]);
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    meterLevel.store (buffer.getRMSLevel (0, 0, numSamples));
    lampLevel.store (engine.lamp() * p.vibrato->load());
    chorusLed.store (engine.chorusLed());
}

AudioProcessorEditor* MurliProcessor::createEditor()
{
    return new MurliEditor (*this);
}

String MurliProcessor::getFinish() const
{
    return apvts.state.getProperty ("finish", "Coral").toString();
}

void MurliProcessor::setFinish (const String& f)
{
    apvts.state.setProperty ("finish", f, nullptr);
}

bool MurliProcessor::showingRear() const
{
    return (bool) apvts.state.getProperty ("rear", false);
}

void MurliProcessor::setShowingRear (bool rear)
{
    apvts.state.setProperty ("rear", rear, nullptr);
}

int MurliProcessor::getCurrentProgram()
{
    const int i = presets.currentIndex();
    return i >= 0 && i < presets.numFactory() ? i : 0;
}

const String MurliProcessor::getProgramName (int index)
{
    return isPositiveAndBelow (index, presets.numFactory()) ? presets.entries()[(size_t) index].name : String();
}

void MurliProcessor::getStateInformation (MemoryBlock& dest)
{
    presets.writeToState();
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void MurliProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (ValueTree::fromXml (*xml));
            presets.readFromState();
        }
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MurliProcessor();
}
