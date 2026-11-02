#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "dsp/MurliEngine.h"
#include "PresetManager.h"

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

    // rear-panel effects loop
    inline constexpr const char* chOn = "chOn", *chRate = "chRate", *chShape = "chShape", *chDepth = "chDepth",
                                 *chDimension = "chDimension", *chAnimate = "chAnimate", *chIntensity = "chIntensity";
    inline constexpr const char* ecOn = "ecOn", *ecTime = "ecTime", *ecSwell = "ecSwell", *ecHeads = "ecHeads",
                                 *ecMode = "ecMode", *ecTone = "ecTone", *ecMix = "ecMix";
    inline constexpr const char* rvOn = "rvOn", *rvDwell = "rvDwell", *rvDecay = "rvDecay", *rvTone = "rvTone", *rvMix = "rvMix";
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

    // The host's program list is the factory presets; user presets live in the editor.
    int getNumPrograms() override { return presets.numFactory(); }
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override { presets.loadFactory (index); }
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets { apvts };
    juce::MidiKeyboardState keyboardState;

    // Read by the editor's timer.
    std::atomic<float> meterLevel { 0.0f };   // block RMS of the output
    std::atomic<float> lampLevel  { 0.0f };   // tremolo lamp brightness
    std::atomic<float> chorusLed  { 0.0f };   // chorus rate LED (blinks even in bypass)

    // Editor-only state saved with the session (not automatable).
    juce::String getFinish() const;
    void setFinish (const juce::String&);
    bool showingRear() const;
    void setShowingRear (bool);

private:
    void handleMidi (const juce::MidiMessage&);

    murli::Engine engine;

    struct Params
    {
        std::atomic<float>* volume; std::atomic<float>* vibrato; std::atomic<float>* speed;
        std::atomic<float>* bark;   std::atomic<float>* drive;   std::atomic<float>* attack;
        std::atomic<float>* speaker; std::atomic<float>* noise;  std::atomic<float>* tune;
        std::atomic<float>* chOn; std::atomic<float>* chRate; std::atomic<float>* chShape; std::atomic<float>* chDepth;
        std::atomic<float>* chDimension; std::atomic<float>* chAnimate; std::atomic<float>* chIntensity;
        std::atomic<float>* ecOn; std::atomic<float>* ecTime; std::atomic<float>* ecSwell; std::atomic<float>* ecHeads;
        std::atomic<float>* ecMode; std::atomic<float>* ecTone; std::atomic<float>* ecMix;
        std::atomic<float>* rvOn; std::atomic<float>* rvDwell; std::atomic<float>* rvDecay; std::atomic<float>* rvTone;
        std::atomic<float>* rvMix;
    } p {};

    std::vector<float> rightScratch;   // used when the host gives us a mono output

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MurliProcessor)
};
