#pragma once

#include "../PresetManager.h"
#include "RetroLookAndFeel.h"

// Amber preset window with step arrows and a SAVE button. Click the window for the
// full list, Save As, Delete and the presets folder.
class PresetBar final : public juce::Component
{
public:
    explicit PresetBar (PresetManager&);
    ~PresetBar() override { if (dialog != nullptr) dialog->setLookAndFeel (nullptr); }

    void resized() override;
    void refresh();   // UI timer: picks up host program changes and the dirty marker

private:
    class StepButton final : public juce::Button
    {
    public:
        explicit StepButton (bool forward) : Button (forward ? "next" : "previous"), fwd (forward) {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    private:
        bool fwd;
    };

    class Display final : public juce::Component
    {
    public:
        explicit Display (PresetBar& o) : owner (o) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override { owner.showMenu(); }
        juce::String text;
    private:
        PresetBar& owner;
    };

    void showMenu();
    void askToSave();

    PresetManager& presets;
    StepButton prev { false }, next { true };
    Display display { *this };
    juce::TextButton save { "SAVE" };
    std::unique_ptr<juce::AlertWindow> dialog;
};
