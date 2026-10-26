#include "PluginEditor.h"

using namespace juce;

MurliEditor::MurliEditor (MurliProcessor& p)
    : AudioProcessorEditor (p), proc (p), keyboard (p.keyboardState)
{
    setLookAndFeel (&lnf);
    canvas.setLookAndFeel (&lnf);
    addAndMakeVisible (canvas);

    canvas.addAndMakeVisible (cabinet);
    canvas.addAndMakeVisible (meter);
    canvas.addAndMakeVisible (powerLamp);
    canvas.addAndMakeVisible (vibratoLamp);
    canvas.addAndMakeVisible (keyboard);
    canvas.addAndMakeVisible (finishButton);
    powerLamp.setBrightness (1.0f);

    const std::pair<const char*, const char*> controls[] = {
        { ParamID::volume, "Volume" },  { ParamID::vibrato, "Vibrato" }, { ParamID::speed, "Speed" },
        { ParamID::bark, "Bark" },      { ParamID::drive, "Drive" },     { ParamID::attack, "Attack" },
        { ParamID::speaker, "Speaker" }, { ParamID::noise, "Noise" },    { ParamID::tune, "Tune" } };
    for (auto& [id, name] : controls)
    {
        knobs.push_back (std::make_unique<Knob> (p.apvts, id, name));
        canvas.addAndMakeVisible (*knobs.back());
    }

    finishButton.setTooltip ("Cabinet finish");
    finishButton.onClick = [this]
    {
        const auto& names = Finish::names();
        const int next = (names.indexOf (proc.getFinish()) + 1) % names.size();
        applyFinish (names[next]);
    };
    applyFinish (proc.getFinish());

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) Cabinet::kWidth / Cabinet::kHeight);
    setResizeLimits (700, 392, 1800, 1008);
    setSize (Cabinet::kWidth, Cabinet::kHeight);

    // In the standalone app the computer keyboard plays the piano (A W S E D ... from C4);
    // inside a DAW we leave the keys to the host until the on-screen keys are clicked.
    if (p.wrapperType == AudioProcessor::wrapperType_Standalone)
        keyboard.grabKeyboardFocus();

    startTimerHz (30);
}

MurliEditor::~MurliEditor()
{
    canvas.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void MurliEditor::applyFinish (const String& name)
{
    lnf.setFinish (Finish::byName (name));
    proc.setFinish (Finish::byName (name).name);
    finishButton.setButtonText ("FINISH: " + Finish::byName (name).name.toUpperCase());
    canvas.repaint();
    cabinet.repaint();
    for (auto& k : knobs) k->repaint();
}

void MurliEditor::resized()
{
    canvas.setBounds (0, 0, Cabinet::kWidth, Cabinet::kHeight);
    canvas.setTransform (AffineTransform::scale ((float) getWidth() / (float) Cabinet::kWidth));

    cabinet.setBounds (0, 0, Cabinet::kWidth, Cabinet::kHeight);
    meter.setBounds (760, 42, 196, 150);
    finishButton.setBounds (846, 220, 110, 24);
    keyboard.setBounds (Cabinet::kCheek, Cabinet::kFeltBottom,
                        Cabinet::kWidth - 2 * Cabinet::kCheek, Cabinet::kKeysBottom - Cabinet::kFeltBottom);

    // three groups of three knobs, matching the legends painted on the plate
    const int groupX[] = { 22, 342, 662 };
    const int groupW[] = { 315, 315, 315 };
    const int top = Cabinet::kPlateTop + 22, h = Cabinet::kPlateBottom - top - 4;
    for (int i = 0; i < (int) knobs.size(); ++i)
    {
        const int g = i / 3, slot = i % 3;
        const int w = groupW[g] / 3;
        knobs[(size_t) i]->setBounds (groupX[g] + slot * w, top, w, h);
    }

    // pilot lamps on the lid, under the nameplate
    powerLamp.setBounds (64, 214, 32, 32);
    vibratoLamp.setBounds (144, 214, 32, 32);
}

void MurliEditor::timerCallback()
{
    meter.setLevel (proc.meterLevel.load());
    vibratoLamp.setBrightness (proc.lampLevel.load() * 1.4f);
}
