#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Cabinet colourways, after the 1960s–70s finishes.
struct Finish
{
    juce::String name;
    juce::Colour body, bodyDark, plate, plateInk, logo, felt, knob, knobPointer;

    static const Finish& byName (const juce::String& name);
    static const juce::StringArray& names();
};

class RetroLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    RetroLookAndFeel();

    void setFinish (const Finish& f) { finish = &f; }
    const Finish& getFinish() const  { return *finish; }

    juce::Font logoFont (float height) const;
    juce::Font labelFont (float height) const;

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;

    static void drawChromeRect (juce::Graphics&, juce::Rectangle<float>, float corner);
    static void drawScrews (juce::Graphics&, juce::Rectangle<float> area, float inset);

private:
    juce::Typeface::Ptr logoFace, labelFace;
    const Finish* finish;
};
