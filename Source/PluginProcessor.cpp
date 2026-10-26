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
    apvts.state.setProperty ("finish", "Coral", nullptr);
}

bool MurliProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == AudioChannelSet::mono() || out == AudioChannelSet::stereo();
}

void MurliProcessor::prepareToPlay (double sampleRate, int)
{
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
    engine.setParams (ep);

    // Render between events so every note starts on its exact sample.
    float* out = buffer.getWritePointer (0);
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = jlimit (0, numSamples, meta.samplePosition);
        if (at > pos) { engine.render (out + pos, at - pos); pos = at; }
        handleMidi (meta.getMessage());
    }
    if (pos < numSamples)
        engine.render (out + pos, numSamples - pos);

    for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);

    meterLevel.store (buffer.getRMSLevel (0, 0, numSamples));
    lampLevel.store (engine.lamp() * p.vibrato->load());
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

void MurliProcessor::getStateInformation (MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void MurliProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (ValueTree::fromXml (*xml));
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MurliProcessor();
}
