#pragma once

#include "PluginProcessor.h"
#include "gui/Widgets.h"
#include "gui/RearPanel.h"
#include "gui/PresetBar.h"

class MurliEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MurliEditor (MurliProcessor&);
    ~MurliEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;

private:
    void timerCallback() override;
    void applyFinish (const juce::String& name);
    void showRear (bool rear);
    void layoutPresetBar();

    MurliProcessor& proc;
    RetroLookAndFeel lnf;

    // Everything is laid out on a fixed 1000 x 560 canvas and scaled as one.
    juce::Component canvas;
    Cabinet cabinet { lnf };
    VUMeter meter;
    Lamp powerLamp { juce::Colour (0xffe0281c) }, vibratoLamp { juce::Colour (0xffffa526) };
    RetroKeyboard keyboard;
    juce::TextButton finishButton, rearButton { "REAR PANEL" };
    std::vector<std::unique_ptr<Knob>> knobs;
    RearPanel rear;
    PresetBar presetBar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MurliEditor)
};
