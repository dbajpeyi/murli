#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "dsp/MurliEngine.h"

namespace ParamID
{
    inline constexpr const char* volume  = "volume";
    inline constexpr const char* vibrato = "vibrato";
    inline constexpr const char* speed   = "speed";
    inline constexpr const char* bark    = "bark";
    inline constexpr const char* drive   = "drive";
    inline constexpr const char* attack  = "attack";
    inline constexpr const char* speaker = "speaker";
    inline constexpr const char* noise   = "noise";
    inline constexpr const char* tune    = "tune";
}

class MurliProcessor final : public juce::AudioProcessor
{
public:
    MurliProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;

    // Read by the editor's timer.
    std::atomic<float> meterLevel { 0.0f };   // block RMS of the output
    std::atomic<float> lampLevel  { 0.0f };   // tremolo lamp brightness

    // Cabinet finish shown by the editor (not automatable, saved with the session).
    juce::String getFinish() const;
    void setFinish (const juce::String&);

private:
    void handleMidi (const juce::MidiMessage&);

    murli::Engine engine;

    struct Params
    {
        std::atomic<float>* volume; std::atomic<float>* vibrato; std::atomic<float>* speed;
        std::atomic<float>* bark;   std::atomic<float>* drive;   std::atomic<float>* attack;
        std::atomic<float>* speaker; std::atomic<float>* noise;  std::atomic<float>* tune;
    } p {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MurliProcessor)
};
