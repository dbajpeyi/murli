#include "Widgets.h"

using namespace juce;

namespace
{
    constexpr float kStartAngle = MathConstants<float>::pi * -0.78f;
    constexpr float kEndAngle   = MathConstants<float>::pi *  0.78f;

    RetroLookAndFeel& retro (Component& c) { return static_cast<RetroLookAndFeel&> (c.getLookAndFeel()); }
}

//==============================================================================
Knob::Knob (AudioProcessorValueTreeState& state, const String& paramID, const String& n)
    : name (n.toUpperCase()),
      param (state.getParameter (paramID)),
      attachment (state, paramID, slider)
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (kStartAngle + MathConstants<float>::twoPi, kEndAngle + MathConstants<float>::twoPi, true);
    slider.setMouseDragSensitivity (220);
    slider.setVelocityBasedMode (false);
    slider.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
    slider.setPopupMenuEnabled (false);
    slider.addListener (this);
    slider.addMouseListener (this, false);
    addAndMakeVisible (slider);
    setRepaintsOnMouseActivity (false);
}

Knob::~Knob() { slider.removeListener (this); }

void Knob::resized()
{
    auto r = getLocalBounds();
    const int labelH = 22;
    const int size = jmin (r.getWidth() - 38, r.getHeight() - labelH - 26);
    slider.setBounds (r.withTrimmedBottom (labelH).withSizeKeepingCentre (size, size));
}

void Knob::paint (Graphics& g)
{
    auto& lnf = retro (*this);
    const auto& f = lnf.getFinish();
    const auto knobArea = slider.getBounds().toFloat();
    const auto c = knobArea.getCentre();
    const float r = knobArea.getWidth() * 0.5f;

    // silk-screened scale
    g.setColour (f.plateInk);
    g.setFont (lnf.labelFont (10.5f));
    for (int i = 0; i <= 10; ++i)
    {
        const float a = kStartAngle + (kEndAngle - kStartAngle) * (float) i / 10.0f;
        const float sx = std::sin (a), cy = -std::cos (a);
        const float inner = r + 2.0f, outer = r + (i % 5 == 0 ? 7.0f : 5.0f);
        g.drawLine (c.x + sx * inner, c.y + cy * inner, c.x + sx * outer, c.y + cy * outer, i % 5 == 0 ? 1.4f : 1.0f);
        if (i % 2 == 0)
        {
            const Point<float> p (c.x + sx * (r + 12.5f), c.y + cy * (r + 11.5f));
            g.drawText (String (i), Rectangle<float> (20.0f, 12.0f).withCentre (p), Justification::centred);
        }
    }

    // name plate, or the live value while the knob is touched
    const bool showValue = slider.isMouseOverOrDragging();
    auto label = getLocalBounds().removeFromBottom (22).toFloat().reduced (6.0f, 2.0f);
    g.setColour (f.plateInk);
    g.setFont (lnf.labelFont (17.0f).withExtraKerningFactor (showValue ? 0.02f : 0.12f));
    const auto text = showValue ? param->getCurrentValueAsText() : name;
    g.drawText (text, label, Justification::centred);
}

//==============================================================================
void VUMeter::setLevel (float rms)
{
    const float dbfs = Decibels::gainToDecibels (rms, -100.0f);
    const float vu = dbfs + 18.0f;                                // 0 VU = -18 dBFS
    const float target = jlimit (0.0f, 1.04f, scalePos (vu));
    // ~300 ms VU integration at a 30 Hz UI rate
    const float coeff = target > needle ? 0.28f : 0.16f;
    const float next = needle + (target - needle) * coeff;
    if (std::abs (next - needle) > 0.0005f) { needle = next; repaint(); }
}

void VUMeter::paint (Graphics& g)
{
    auto& lnf = retro (*this);
    auto r = getLocalBounds().toFloat();

    // bezel
    RetroLookAndFeel::drawChromeRect (g, r, 8.0f);
    auto face = r.reduced (7.0f);
    g.setGradientFill (ColourGradient (Colour (0xfffbe6a6), face.getCentreX(), face.getY() + face.getHeight() * 0.35f,
                                       Colour (0xffd59a3c), face.getX(), face.getBottom(), true));
    g.fillRoundedRectangle (face, 5.0f);

    const Point<float> pivot (face.getCentreX(), face.getBottom() + face.getHeight() * 0.32f);
    const float radius = face.getHeight() * 0.98f;
    const float a0 = -0.64f, a1 = 0.64f;   // radians either side of vertical
    auto angleFor = [&] (float t) { return a0 + (a1 - a0) * t; };
    auto pt = [&] (float a, float rad) { return Point<float> (pivot.x + std::sin (a) * rad, pivot.y - std::cos (a) * rad); };

    // scale arc: black to 0 VU, red above
    const float zero = scalePos (0.0f);
    Path arc, red;
    arc.addCentredArc (pivot.x, pivot.y, radius * 0.86f, radius * 0.86f, 0.0f, angleFor (0), angleFor (zero), true);
    red.addCentredArc (pivot.x, pivot.y, radius * 0.86f, radius * 0.86f, 0.0f, angleFor (zero), angleFor (1), true);
    g.setColour (Colour (0xff2a1d12));
    g.strokePath (arc, PathStrokeType (1.6f));
    g.setColour (Colour (0xffb3261e));
    g.strokePath (red, PathStrokeType (4.0f));

    g.setFont (lnf.labelFont (12.0f));
    const int marks[] = { -20, -10, -7, -5, -3, -1, 0, 1, 2, 3 };
    for (int m : marks)
    {
        const float a = angleFor (scalePos ((float) m));
        g.setColour (m > 0 ? Colour (0xffb3261e) : Colour (0xff2a1d12));
        g.drawLine ({ pt (a, radius * 0.86f), pt (a, radius * 0.93f) }, 1.3f);
        g.drawText (String (std::abs (m)), Rectangle<float> (22.0f, 12.0f).withCentre (pt (a, radius * 1.0f)), Justification::centred);
    }
    g.setColour (Colour (0xff2a1d12));
    g.setFont (lnf.labelFont (20.0f).withExtraKerningFactor (0.15f));
    g.drawText ("VU", face.withTrimmedTop (face.getHeight() * 0.48f).withHeight (20.0f), Justification::centred);

    // needle
    g.saveState();
    g.reduceClipRegion (face.toNearestInt());
    const float a = angleFor (jlimit (0.0f, 1.04f, needle));
    g.setColour (Colours::black.withAlpha (0.25f));
    g.drawLine ({ pt (a, 0.0f).translated (2.0f, 2.0f), pt (a, radius * 0.97f).translated (2.0f, 2.0f) }, 1.6f);
    g.setColour (Colour (0xff141010));
    g.drawLine ({ pt (a, 0.0f), pt (a, radius * 0.97f) }, 1.6f);
    g.restoreState();

    // glass glare
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.28f), face.getX(), face.getY(),
                                       Colours::transparentWhite, face.getX(), face.getCentreY(), false));
    g.fillRoundedRectangle (face.withHeight (face.getHeight() * 0.45f), 5.0f);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (face, 5.0f, 1.0f);
}

//==============================================================================
void Lamp::setBrightness (float b)
{
    b = jlimit (0.0f, 1.0f, b);
    if (std::abs (b - brightness) > 0.01f) { brightness = b; repaint(); }
}

void Lamp::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const auto c = r.getCentre();
    const float rad = r.getWidth() * 0.5f;

    if (brightness > 0.02f)   // glow
    {
        g.setGradientFill (ColourGradient (colour.withAlpha (0.8f * brightness), c.x, c.y,
                                           colour.withAlpha (0.0f), c.x + rad, c.y, true));
        g.fillEllipse (r);
    }
    RetroLookAndFeel::drawChromeRect (g, r.reduced (rad * 0.28f), rad);
    auto jewel = r.reduced (rad * 0.36f);
    const auto lit = colour.brighter (0.4f).interpolatedWith (Colours::white, 0.35f * brightness);
    const auto dark = colour.darker (2.2f);
    g.setGradientFill (ColourGradient (dark.interpolatedWith (lit, 0.25f + 0.75f * brightness), c.x, c.y,
                                       dark, c.x + jewel.getWidth() * 0.5f, c.y, true));
    g.fillEllipse (jewel);
    g.setColour (Colours::white.withAlpha (0.5f));
    g.fillEllipse (jewel.getX() + jewel.getWidth() * 0.25f, jewel.getY() + jewel.getHeight() * 0.18f,
                   jewel.getWidth() * 0.28f, jewel.getHeight() * 0.2f);
}

//==============================================================================
RetroKeyboard::RetroKeyboard (MidiKeyboardState& s)
    : MidiKeyboardComponent (s, horizontalKeyboard)
{
    setAvailableRange (33, 96);
    setOctaveForMiddleC (4);
    setKeyPressBaseOctave (5);
    setScrollButtonsVisible (false);
    setVelocity (0.8f, true);
    setBlackNoteLengthProportion (0.62f);
    setBlackNoteWidthProportion (0.62f);
    setWantsKeyboardFocus (true);
    setOpaque (true);
}

void RetroKeyboard::resized()
{
    // 38 white keys from A1 to C7 fill the width exactly
    setKeyWidth ((float) getWidth() / 38.0f);
    MidiKeyboardComponent::resized();
    setLowestVisibleKey (33);
}

void RetroKeyboard::paint (Graphics& g)
{
    g.fillAll (Colour (0xff141210));
    MidiKeyboardComponent::paint (g);
    // shadow cast by the felt strip
    g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.45f), 0, 0, Colours::transparentBlack, 0, 10, false));
    g.fillRect (getLocalBounds().withHeight (10));
}

void RetroKeyboard::drawWhiteNote (int, Graphics& g, Rectangle<float> area, bool isDown, bool isOver, Colour, Colour)
{
    auto key = area.reduced (0.6f, 0.0f);
    const auto top = Colour (0xfff6f0e0), bottom = Colour (0xffe2d8c0);
    g.setGradientFill (ColourGradient (isDown ? top.darker (0.12f) : top, key.getX(), key.getY(),
                                       isDown ? bottom.darker (0.15f) : bottom, key.getX(), key.getBottom(), false));
    g.fillRoundedRectangle (key.withTrimmedTop (-4.0f), 3.0f);
    if (isOver && ! isDown)
    {
        g.setColour (Colours::white.withAlpha (0.2f));
        g.fillRect (key);
    }
    // front lip
    auto lip = key.removeFromBottom (isDown ? 4.0f : 7.0f);
    g.setColour (Colour (0xffcfc3a7));
    g.fillRoundedRectangle (lip, 2.0f);
    g.setColour (Colours::black.withAlpha (0.28f));
    g.drawVerticalLine ((int) area.getRight(), area.getY(), area.getBottom());
}

void RetroKeyboard::drawBlackNote (int, Graphics& g, Rectangle<float> area, bool isDown, bool isOver, Colour)
{
    auto key = area.reduced (1.0f, 0.0f);
    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (key.translated (2.0f, 2.0f), 2.0f);
    g.setGradientFill (ColourGradient (Colour (isDown ? 0xff2c2826 : 0xff1a1817), key.getX(), key.getY(),
                                       Colour (0xff050505), key.getX(), key.getBottom(), false));
    g.fillRoundedRectangle (key, 2.0f);
    // glossy top face
    auto face = key.reduced (key.getWidth() * 0.16f, 0.0f).withTrimmedBottom (isDown ? 3.0f : 6.0f);
    g.setGradientFill (ColourGradient (Colour (isDown ? 0xff36312e : 0xff47423f), face.getX(), face.getY(),
                                       Colour (0xff1c1a19), face.getX(), face.getBottom(), false));
    g.fillRoundedRectangle (face, 1.5f);
    if (isOver)
    {
        g.setColour (Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (face, 1.5f);
    }
}

//==============================================================================
void Cabinet::paint (Graphics& g)
{
    const auto& f = lnf.getFinish();
    const float W = (float) getWidth(), H = (float) getHeight();
    g.fillAll (Colour (0xff121010));

    // --- lid: tolex body with a fine grain --------------------------------
    auto lid = Rectangle<float> (0, 0, W, (float) kLidBottom);
    g.setGradientFill (ColourGradient (f.body.brighter (0.08f), 0, 0, f.body.darker (0.25f), 0, lid.getBottom(), false));
    g.fillRect (lid);
    Random rng (1960);
    for (int i = 0; i < 9000; ++i)
    {
        const float x = rng.nextFloat() * W, y = rng.nextFloat() * lid.getBottom();
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (0.035f));
        g.fillRect (x, y, 1.6f, 1.0f);
    }

    // chrome trim across the top and along the lid edge
    RetroLookAndFeel::drawChromeRect (g, { 0, 0, W, 9 }, 0.0f);
    RetroLookAndFeel::drawChromeRect (g, { 0, lid.getBottom() - 6, W, 6 }, 0.0f);

    // nameplate badge
    auto badge = Rectangle<float> (48, 42, 430, 150);
    g.setColour (Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (badge.translated (3, 5), 14.0f);
    g.setGradientFill (ColourGradient (f.bodyDark.brighter (0.15f), badge.getX(), badge.getY(),
                                       f.bodyDark.darker (0.4f), badge.getX(), badge.getBottom(), false));
    g.fillRoundedRectangle (badge, 14.0f);
    g.setColour (Colour (0xffd8d6d0));
    g.drawRoundedRectangle (badge.reduced (5.0f), 10.0f, 2.0f);
    RetroLookAndFeel::drawScrews (g, badge, 12.0f);

    g.setFont (lnf.logoFont (84.0f));
    auto logoArea = badge.withTrimmedBottom (40).translated (0, 6);
    g.setColour (Colours::black.withAlpha (0.45f));
    g.drawText ("Murli", logoArea.translated (2.5f, 3.5f), Justification::centred);
    g.setColour (f.logo);
    g.drawText ("Murli", logoArea, Justification::centred);

    g.setFont (lnf.labelFont (19.0f).withExtraKerningFactor (0.42f));
    g.setColour (f.logo.withAlpha (0.85f));
    g.drawText ("ELECTRONIC  PIANO", badge.removeFromBottom (44).translated (0, -6), Justification::centred);

    // speaker grille: woven cloth behind chrome slats
    auto grille = Rectangle<float> (500, 42, 236, 150);
    g.setColour (Colour (0xff2b2622));
    g.fillRoundedRectangle (grille, 10.0f);
    for (float y = grille.getY() + 3; y < grille.getBottom() - 3; y += 3.0f)
    {
        g.setColour (Colour (0xff3c352f).withAlpha (0.8f));
        g.drawHorizontalLine ((int) y, grille.getX() + 4, grille.getRight() - 4);
    }
    for (int i = 0; i < 6; ++i)
        RetroLookAndFeel::drawChromeRect (g, { grille.getX() + 10, grille.getY() + 16 + (float) i * 22.0f, grille.getWidth() - 20, 5 }, 2.5f);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (grille, 10.0f, 2.0f);

    // pilot lamp legends (the lamps themselves are live components)
    g.setFont (lnf.labelFont (13.0f).withExtraKerningFactor (0.25f));
    g.setColour (f.logo.withAlpha (0.8f));
    g.drawText ("POWER", Rectangle<float> (40, 252, 80, 16), Justification::centred);
    g.drawText ("VIBRATO", Rectangle<float> (120, 252, 80, 16), Justification::centred);

    // --- control plate ------------------------------------------------------
    auto plate = Rectangle<float> (0, (float) kPlateTop, W, (float) (kPlateBottom - kPlateTop));
    g.setColour (Colour (0xff0e0c0b));
    g.fillRect (Rectangle<float> (0, lid.getBottom(), W, plate.getY() - lid.getBottom()));
    g.setGradientFill (ColourGradient (f.plate.brighter (0.06f), 0, plate.getY(), f.plate.darker (0.12f), 0, plate.getBottom(), false));
    g.fillRect (plate);
    for (int i = 0; i < 400; ++i)   // brushed finish
    {
        const float y = plate.getY() + rng.nextFloat() * plate.getHeight();
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (0.025f));
        g.drawHorizontalLine ((int) y, rng.nextFloat() * W * 0.5f, W * (0.5f + rng.nextFloat() * 0.5f));
    }
    RetroLookAndFeel::drawScrews (g, plate, 10.0f);

    // group legends and dividers
    g.setColour (f.plateInk.withAlpha (0.75f));
    const float groups[][2] = { { 22, 337 }, { 342, 657 }, { 662, 977 } };
    const char* legends[] = { "AMPLIFIER", "REED  &  PICKUP", "CABINET" };
    g.setFont (lnf.labelFont (13.0f).withExtraKerningFactor (0.35f));
    for (int i = 0; i < 3; ++i)
    {
        auto lineY = plate.getY() + 13.0f;
        auto legend = Rectangle<float> (groups[i][0], lineY - 7, groups[i][1] - groups[i][0], 14);
        const float textW = 130.0f;
        g.drawText (legends[i], legend, Justification::centred);
        g.drawHorizontalLine ((int) lineY, legend.getX(), legend.getCentreX() - textW * 0.5f);
        g.drawHorizontalLine ((int) lineY, legend.getCentreX() + textW * 0.5f, legend.getRight());
    }

    // --- felt strip -----------------------------------------------------------
    auto felt = Rectangle<float> (0, (float) kPlateBottom, W, (float) (kFeltBottom - kPlateBottom));
    g.setColour (f.felt);
    g.fillRect (felt);
    for (int i = 0; i < 1500; ++i)
    {
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (0.06f));
        g.fillRect (rng.nextFloat() * W, felt.getY() + rng.nextFloat() * felt.getHeight(), 1.0f, 1.0f);
    }

    // --- key cheeks and front rail --------------------------------------------
    for (auto cheek : { Rectangle<float> (0, felt.getY(), (float) kCheek, H - felt.getY()),
                        Rectangle<float> (W - (float) kCheek, felt.getY(), (float) kCheek, H - felt.getY()) })
    {
        g.setGradientFill (ColourGradient (f.body.darker (0.1f), cheek.getX(), 0, f.body.darker (0.5f), cheek.getRight(), 0, false));
        g.fillRect (cheek);
    }
    auto rail = Rectangle<float> (0, (float) kKeysBottom, W, H - (float) kKeysBottom);
    g.setGradientFill (ColourGradient (f.body.darker (0.2f), 0, rail.getY(), f.body.darker (0.7f), 0, rail.getBottom(), false));
    g.fillRect (rail);
}
