#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "RetroLookAndFeel.h"

// A knob with a silk-screened 0–10 scale and a name plate that shows the
// parameter's real value while you hover or drag.
class Knob final : public juce::Component, private juce::Slider::Listener
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramID, const juce::String& name);
    ~Knob() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void sliderValueChanged (juce::Slider*) override { repaint(); }
    void sliderDragStarted (juce::Slider*) override  { repaint(); }
    void sliderDragEnded (juce::Slider*) override    { repaint(); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

    juce::Slider slider;
    juce::String name;
    juce::RangedAudioParameter* param;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

// Amber-lit VU meter with needle ballistics (0 VU = -18 dBFS).
class VUMeter final : public juce::Component
{
public:
    void setLevel (float rms);   // call from a UI timer
    void paint (juce::Graphics&) override;

private:
    // Real VU scales are linear in voltage: 0 VU sits ~70 % across, +3 at the end.
    static float scalePos (float vu) { return std::pow (10.0f, (vu - 3.0f) / 20.0f); }

    float needle = 0.0f;   // 0..1 across the scale
};

// Jewel pilot lamp.
class Lamp final : public juce::Component
{
public:
    explicit Lamp (juce::Colour c) : colour (c) { setInterceptsMouseClicks (false, false); }
    void setBrightness (float b);
    void paint (juce::Graphics&) override;

private:
    juce::Colour colour;
    float brightness = 0.0f;
};

// 64 keys, A1–C7, drawn like the real keybed.
class RetroKeyboard final : public juce::MidiKeyboardComponent
{
public:
    explicit RetroKeyboard (juce::MidiKeyboardState&);

    void drawWhiteNote (int note, juce::Graphics&, juce::Rectangle<float> area,
                        bool isDown, bool isOver, juce::Colour, juce::Colour) override;
    void drawBlackNote (int note, juce::Graphics&, juce::Rectangle<float> area,
                        bool isDown, bool isOver, juce::Colour) override;
    void paint (juce::Graphics&) override;
    void resized() override;
};

// Everything that never moves: cabinet, nameplate, grille, control plate, felt.
// Painted once into an image, so redrawing knobs and meters stays cheap.
class Cabinet final : public juce::Component
{
public:
    explicit Cabinet (RetroLookAndFeel& l) : lnf (l) { setBufferedToImage (true); setOpaque (true); }
    void paint (juce::Graphics&) override;

    // Layout shared with the editor (base 1000 x 560 coordinates).
    static constexpr int kWidth = 1000, kHeight = 560;
    static constexpr int kLidBottom = 292, kPlateTop = 300, kPlateBottom = 432, kFeltBottom = 444;
    static constexpr int kKeysBottom = 548, kCheek = 26;

private:
    RetroLookAndFeel& lnf;
};
