#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Widgets.h"

// Colours of one stompbox enclosure.
struct PedalLook
{
    juce::Colour body, ink, accent;
};

// Small knurled pedal knob, name silk-screened above, live value while touched.
class PedalKnob final : public juce::Component, private juce::Slider::Listener
{
public:
    PedalKnob (juce::AudioProcessorValueTreeState&, const juce::String& paramID, const juce::String& name, juce::Colour ink);
    ~PedalKnob() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void sliderValueChanged (juce::Slider*) override { repaint(); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

    juce::Slider slider;
    juce::String name;
    juce::Colour ink;
    juce::RangedAudioParameter* param;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

// 5 mm pedal LED with a glow that spills onto the enclosure.
class Led final : public juce::Component
{
public:
    explicit Led (juce::Colour c) : colour (c) { setInterceptsMouseClicks (false, false); }
    void setBrightness (float b);
    void paint (juce::Graphics&) override;

private:
    juce::Colour colour;
    float brightness = 0.0f;
};

// Chrome stomp switch toggling a bool parameter.
class Footswitch final : public juce::Component
{
public:
    Footswitch (juce::AudioProcessorValueTreeState&, const juce::String& paramID);
    bool isOn() const { return on; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::RangedAudioParameter* param;
    juce::ParameterAttachment attachment;
    bool on = false, pressed = false;
};

// Three-position bat-handle toggle (Echo / Repeat / Swell).
class ToggleSwitch3 final : public juce::Component
{
public:
    ToggleSwitch3 (juce::AudioProcessorValueTreeState&, const juce::String& paramID, juce::Colour ink);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::RangedAudioParameter* param;
    juce::ParameterAttachment attachment;
    juce::Colour ink;
    int position = 0;
};

// One stompbox: knobs on top, name in the middle, LED and footswitch at the bottom.
class Pedal final : public juce::Component
{
public:
    struct KnobSpec { const char* id; const char* name; };

    Pedal (juce::AudioProcessorValueTreeState&, const juce::String& title, const juce::String& kind, PedalLook,
           const juce::String& onParam, std::initializer_list<KnobSpec> topRow,
           std::initializer_list<KnobSpec> bottomRow, const char* toggleParam = nullptr);

    void paint (juce::Graphics&) override;
    void resized() override;

    // Rate LED (the chorus shows its LFO, even in bypass); < 0 hides it.
    void setRateLed (float brightness) { if (rateLed != nullptr) rateLed->setBrightness (brightness); }
    void refreshStatus() { status.setBrightness (footswitch.isOn() ? 1.0f : 0.0f); }
    void addRateLed (juce::Colour);

private:
    juce::String title, kind;
    PedalLook look;
    std::vector<std::unique_ptr<PedalKnob>> top, bottom;
    std::unique_ptr<ToggleSwitch3> toggle;
    std::unique_ptr<Led> rateLed;
    Led status { juce::Colour (0xffff2a1a) };
    Footswitch footswitch;
};

// The back of the instrument: jack panel with the effects-loop mod, and a pedalboard
// patched SEND → chorus → echo → spring → RETURN (signal runs right to left, as on a board).
class RearPanel final : public juce::Component
{
public:
    RearPanel (juce::AudioProcessorValueTreeState&, RetroLookAndFeel&);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    void update (float chorusLfo);   // UI timer

    juce::TextButton frontButton { "FRONT PANEL" };

private:
    RetroLookAndFeel& lnf;
    Pedal spring, echo, chorus;
};
