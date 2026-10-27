#include "RearPanel.h"
#include "../PluginProcessor.h"

using namespace juce;

namespace
{
    constexpr float kStart = MathConstants<float>::pi * -0.75f;
    constexpr float kEnd   = MathConstants<float>::pi *  0.75f;

    RetroLookAndFeel& retro (Component& c) { return static_cast<RetroLookAndFeel&> (c.getLookAndFeel()); }

    // rear-panel layout (canvas coordinates)
    constexpr int kStripBottom = 92;
    const Rectangle<int> kSpringBox { 30, 112, 296, 306 };
    const Rectangle<int> kEchoBox   { 352, 112, 296, 306 };
    const Rectangle<int> kChorusBox { 674, 112, 296, 306 };
    const Point<float> kReturnJack { 70.0f, 56.0f }, kSendJack { 930.0f, 56.0f };

    void drawJack (Graphics& g, Point<float> c, float r)
    {
        g.setGradientFill (ColourGradient (Colour (0xffeeeeea), c.x - r, c.y - r, Colour (0xff6a6a66), c.x + r, c.y + r, false));
        g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
        const float h = r * 0.55f;
        g.setColour (Colour (0xff0d0c0c));
        g.fillEllipse (c.x - h, c.y - h, 2 * h, 2 * h);
    }

    // A patch cable between two points, sagging under its own weight, with plugs.
    void drawCable (Graphics& g, Point<float> a, Point<float> b, float sag, Colour colour)
    {
        Path p;
        p.startNewSubPath (a);
        p.cubicTo (a.translated (0, sag), b.translated (0, sag), b);
        g.setColour (Colours::black.withAlpha (0.35f));
        g.strokePath (p, PathStrokeType (7.0f, PathStrokeType::curved, PathStrokeType::rounded), AffineTransform::translation (2, 4));
        g.setColour (colour);
        g.strokePath (p, PathStrokeType (6.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (colour.brighter (0.5f).withAlpha (0.5f));
        g.strokePath (p, PathStrokeType (1.5f, PathStrokeType::curved, PathStrokeType::rounded), AffineTransform::translation (-1, -1.5f));
    }

    // Right-angle plug body sitting against a pedal's side jack.
    void drawPlug (Graphics& g, Point<float> jack, bool pointsRight)
    {
        auto body = Rectangle<float> (14.0f, 12.0f).withCentre (jack.translated (pointsRight ? 7.0f : -7.0f, 0));
        RetroLookAndFeel::drawChromeRect (g, body, 2.5f);
        g.setColour (Colour (0xff1a1a1a));
        g.fillRoundedRectangle (body.withHeight (5.0f).withY (body.getY() - 5.0f).reduced (2.0f, 0), 1.5f);
    }
}

//==============================================================================
PedalKnob::PedalKnob (AudioProcessorValueTreeState& state, const String& paramID, const String& n, Colour i)
    : name (n.toUpperCase()), ink (i),
      param (state.getParameter (paramID)),
      attachment (state, paramID, slider)
{
    slider.getProperties().set ("pedal", true);
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (kStart + MathConstants<float>::twoPi, kEnd + MathConstants<float>::twoPi, true);
    slider.setMouseDragSensitivity (200);
    slider.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
    slider.addListener (this);
    slider.addMouseListener (this, false);
    addAndMakeVisible (slider);
}

PedalKnob::~PedalKnob() { slider.removeListener (this); }

void PedalKnob::resized()
{
    auto r = getLocalBounds().withTrimmedTop (16);
    const int size = jmin (r.getWidth() - 10, r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (size, size).withY (r.getY()));
}

void PedalKnob::paint (Graphics& g)
{
    auto& lnf = retro (*this);
    const bool showValue = slider.isMouseOverOrDragging();
    g.setColour (ink);
    g.setFont (lnf.labelFont (14.0f).withExtraKerningFactor (showValue ? 0.02f : 0.1f));
    g.drawText (showValue ? param->getCurrentValueAsText() : name, getLocalBounds().removeFromTop (16), Justification::centred);
}

//==============================================================================
void Led::setBrightness (float b)
{
    b = jlimit (0.0f, 1.0f, b);
    if (std::abs (b - brightness) > 0.01f) { brightness = b; repaint(); }
}

void Led::paint (Graphics& g)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const float r = 5.0f, R = (float) getWidth() * 0.5f;
    if (brightness > 0.02f)
    {
        g.setGradientFill (ColourGradient (colour.withAlpha (0.75f * brightness), c.x, c.y, colour.withAlpha (0.0f), c.x + R, c.y, true));
        g.fillEllipse (c.x - R, c.y - R, 2 * R, 2 * R);
    }
    g.setColour (Colour (0xff1a1a1a));   // bezel
    g.fillEllipse (c.x - r - 1.5f, c.y - r - 1.5f, 2 * r + 3, 2 * r + 3);
    const auto off = colour.darker (2.5f);
    const auto lit = colour.interpolatedWith (Colours::white, 0.35f);
    g.setGradientFill (ColourGradient (off.interpolatedWith (lit, brightness), c.x - r * 0.3f, c.y - r * 0.3f,
                                       off.interpolatedWith (colour, brightness), c.x + r, c.y + r, true));
    g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    g.setColour (Colours::white.withAlpha (0.45f));
    g.fillEllipse (c.x - r * 0.55f, c.y - r * 0.65f, r * 0.6f, r * 0.45f);
}

//==============================================================================
Footswitch::Footswitch (AudioProcessorValueTreeState& state, const String& paramID)
    : param (state.getParameter (paramID)),
      attachment (*param, [this] (float v) { on = v > 0.5f; repaint(); }, nullptr)
{
    attachment.sendInitialUpdate();
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void Footswitch::mouseDown (const MouseEvent&)
{
    pressed = true;
    attachment.setValueAsCompleteGesture (on ? 0.0f : 1.0f);
    repaint();
}

void Footswitch::mouseUp (const MouseEvent&) { pressed = false; repaint(); }

void Footswitch::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const auto c = r.getCentre();
    const float R = jmin (r.getWidth(), r.getHeight()) * 0.5f - 2.0f;

    // hex nut
    Path hex;
    for (int i = 0; i < 6; ++i)
    {
        const float a = MathConstants<float>::pi / 3.0f * (float) i + MathConstants<float>::pi / 6.0f;
        const Point<float> p (c.x + std::cos (a) * R, c.y + std::sin (a) * R);
        if (i == 0) hex.startNewSubPath (p); else hex.lineTo (p);
    }
    hex.closeSubPath();
    g.setColour (Colours::black.withAlpha (0.4f));
    g.fillPath (hex, AffineTransform::translation (2, 3));
    g.setGradientFill (ColourGradient (Colour (0xfff2f2ef), c.x - R, c.y - R, Colour (0xff70706b), c.x + R, c.y + R, false));
    g.fillPath (hex);

    // plunger
    const float pr = R * (pressed ? 0.56f : 0.6f);
    g.setGradientFill (ColourGradient (Colour (0xffffffff), c.x - pr, c.y - pr, Colour (0xff8b8b86), c.x + pr, c.y + pr, true));
    g.fillEllipse (c.x - pr, c.y - pr, 2 * pr, 2 * pr);
    g.setColour (Colours::black.withAlpha (0.35f));
    g.drawEllipse (c.x - pr, c.y - pr, 2 * pr, 2 * pr, 1.0f);
}

//==============================================================================
ToggleSwitch3::ToggleSwitch3 (AudioProcessorValueTreeState& state, const String& paramID, Colour i)
    : param (state.getParameter (paramID)),
      attachment (*param, [this] (float v) { position = jlimit (0, 2, roundToInt (v)); repaint(); }, nullptr),
      ink (i)
{
    attachment.sendInitialUpdate();
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void ToggleSwitch3::mouseDown (const MouseEvent& e)
{
    // click a label to jump to it, or anywhere else to step
    const int third = getWidth() / 3;
    const int target = e.y < getHeight() - 16 ? (position + 1) % 3 : jlimit (0, 2, e.x / third);
    attachment.setValueAsCompleteGesture ((float) target);
}

void ToggleSwitch3::paint (Graphics& g)
{
    auto& lnf = retro (*this);
    auto r = getLocalBounds().toFloat();
    auto labels = r.removeFromBottom (14.0f);
    const auto c = r.getCentre();

    // mounting nut
    RetroLookAndFeel::drawChromeRect (g, Rectangle<float> (16, 16).withCentre (c), 8.0f);
    // bat handle
    const float angle = (float) (position - 1) * 0.55f;
    const Point<float> tip (c.x + std::sin (angle) * r.getHeight() * 0.48f, c.y - std::cos (angle) * r.getHeight() * 0.48f);
    g.setColour (Colours::black.withAlpha (0.35f));
    g.drawLine ({ c.translated (2, 2), tip.translated (2, 2) }, 5.0f);
    g.setGradientFill (ColourGradient (Colour (0xfff8f8f6), tip.x, tip.y, Colour (0xff7a7a75), c.x, c.y, false));
    g.drawLine ({ c, tip }, 4.5f);
    g.fillEllipse (Rectangle<float> (7, 7).withCentre (tip));

    g.setFont (lnf.labelFont (11.0f));
    const char* names[] = { "ECHO", "REPEAT", "SWELL" };
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (ink.withAlpha (i == position ? 1.0f : 0.55f));
        g.drawText (names[i], labels.withWidth (r.getWidth() / 3.0f).withX (r.getWidth() / 3.0f * (float) i), Justification::centred);
    }
}

//==============================================================================
Pedal::Pedal (AudioProcessorValueTreeState& state, const String& t, const String& k, PedalLook l,
              const String& onParam, std::initializer_list<KnobSpec> topRow,
              std::initializer_list<KnobSpec> bottomRow, const char* toggleParam)
    : title (t), kind (k), look (l), footswitch (state, onParam)
{
    for (auto& s : topRow)    top.push_back (std::make_unique<PedalKnob> (state, s.id, s.name, look.ink));
    for (auto& s : bottomRow) bottom.push_back (std::make_unique<PedalKnob> (state, s.id, s.name, look.ink));
    for (auto& kn : top)    addAndMakeVisible (*kn);
    for (auto& kn : bottom) addAndMakeVisible (*kn);
    if (toggleParam != nullptr)
    {
        toggle = std::make_unique<ToggleSwitch3> (state, toggleParam, look.ink);
        addAndMakeVisible (*toggle);
    }
    addAndMakeVisible (status);
    addAndMakeVisible (footswitch);
}

void Pedal::addRateLed (Colour c)
{
    rateLed = std::make_unique<Led> (c);
    addAndMakeVisible (*rateLed);
}

void Pedal::resized()
{
    auto r = getLocalBounds().reduced (14, 12);
    auto row1 = r.removeFromTop (74);
    auto row2 = r.removeFromTop (74);

    auto layRow = [] (std::vector<std::unique_ptr<PedalKnob>>& knobs, Rectangle<int> row, int slots)
    {
        const int w = row.getWidth() / slots;
        for (size_t i = 0; i < knobs.size(); ++i)
            knobs[i]->setBounds (row.getX() + (int) i * w, row.getY(), w, row.getHeight());
    };
    layRow (top, row1, 3);
    layRow (bottom, row2, 3);
    if (toggle != nullptr)   // fills the third slot of the bottom row
        toggle->setBounds (row2.getX() + row2.getWidth() * 2 / 3 + 4, row2.getY() + 14, row2.getWidth() / 3 - 8, row2.getHeight() - 16);

    if (top.size() == 2)   // two-knob rows sit centred
    {
        const int w = row1.getWidth() / 3;
        top[0]->setBounds (row1.getX() + w / 2, row1.getY(), w, row1.getHeight());
        top[1]->setBounds (row1.getX() + w * 3 / 2, row1.getY(), w, row1.getHeight());
        if (bottom.size() == 2)
        {
            bottom[0]->setBounds (row2.getX() + w / 2, row2.getY(), w, row2.getHeight());
            bottom[1]->setBounds (row2.getX() + w * 3 / 2, row2.getY(), w, row2.getHeight());
        }
    }

    status.setBounds (getWidth() / 2 - 15, 204, 30, 30);
    if (rateLed != nullptr)
        rateLed->setBounds (getWidth() - 38, 2, 28, 28);
    footswitch.setBounds (getWidth() / 2 - 26, getHeight() - 66, 52, 52);
}

void Pedal::paint (Graphics& g)
{
    auto& lnf = retro (*this);
    auto r = getLocalBounds().toFloat().reduced (2.0f);

    // powder-coated die-cast enclosure
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (r.translated (3, 6), 14.0f);
    g.setGradientFill (ColourGradient (look.body.brighter (0.18f), r.getX(), r.getY(), look.body.darker (0.35f), r.getRight(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 14.0f);
    Random rng ((int64) title.hashCode());
    for (int i = 0; i < 1400; ++i)   // powder-coat speckle
    {
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (0.05f));
        g.fillRect (r.getX() + rng.nextFloat() * r.getWidth(), r.getY() + rng.nextFloat() * r.getHeight(), 1.2f, 1.2f);
    }
    g.setColour (Colours::white.withAlpha (0.18f));
    g.drawRoundedRectangle (r.reduced (1.0f), 13.0f, 1.2f);
    g.setColour (Colours::black.withAlpha (0.45f));
    g.drawRoundedRectangle (r, 14.0f, 1.0f);
    RetroLookAndFeel::drawScrews (g, r, 9.0f);

    // name plate between the knobs and the footswitch
    auto band = Rectangle<float> (r.getX() + 16, 164.0f, r.getWidth() - 32, 40.0f);
    g.setColour (look.accent);
    g.fillRoundedRectangle (band, 6.0f);
    g.setColour (look.ink);
    g.setFont (lnf.logoFont (30.0f));
    g.drawText (title, band.withTrimmedRight (band.getWidth() * 0.45f).translated (0, -2), Justification::centred);
    g.setFont (lnf.labelFont (14.0f).withExtraKerningFactor (0.18f));
    g.drawFittedText (kind, band.withTrimmedLeft (band.getWidth() * 0.55f).withTrimmedRight (8).toNearestInt(),
                      Justification::centredLeft, 1, 0.8f);
}

//==============================================================================
RearPanel::RearPanel (AudioProcessorValueTreeState& state, RetroLookAndFeel& l)
    : lnf (l),
      spring (state, "Coil", "SPRING  REVERB",
              { Colour (0xffe7dcc3), Colour (0xff2b221c), Colour (0xff2b221c).withAlpha (0.12f) },
              ParamID::rvOn,
              { { ParamID::rvDwell, "Dwell" }, { ParamID::rvDecay, "Decay" } },
              { { ParamID::rvTone, "Tone" }, { ParamID::rvMix, "Mix" } }),
      echo (state, "Magdrum", "DRUM  ECHO",
            { Colour (0xff4a5638), Colour (0xfff0e7d0), Colours::black.withAlpha (0.22f) },
            ParamID::ecOn,
            { { ParamID::ecTime, "Time" }, { ParamID::ecSwell, "Swell" }, { ParamID::ecTone, "Tone" } },
            { { ParamID::ecHeads, "Heads" }, { ParamID::ecMix, "Mix" } },
            ParamID::ecMode),
      chorus (state, "Undertow", "CHORUS",
              { Colour (0xff2c7b83), Colour (0xfff3ecdb), Colours::black.withAlpha (0.2f) },
              ParamID::chOn,
              { { ParamID::chDimension, "Dimension" }, { ParamID::chShape, "Shape" }, { ParamID::chRate, "Rate" } },
              { { ParamID::chDepth, "Depth" }, { ParamID::chAnimate, "Animate" }, { ParamID::chIntensity, "Intensity" } })
{
    chorus.addRateLed (Colour (0xff3d8bff));   // the Sea Machine's blue tempo LED
    addAndMakeVisible (spring);
    addAndMakeVisible (echo);
    addAndMakeVisible (chorus);
    addAndMakeVisible (frontButton);
    setOpaque (true);
}

void RearPanel::resized()
{
    spring.setBounds (kSpringBox);
    echo.setBounds (kEchoBox);
    chorus.setBounds (kChorusBox);
    frontButton.setBounds (440, 34, 120, 26);
}

void RearPanel::update (float chorusLfo)
{
    chorus.setRateLed (chorusLfo);
    for (auto* p : { &spring, &echo, &chorus }) p->refreshStatus();
}

void RearPanel::paint (Graphics& g)
{
    const float W = (float) getWidth(), H = (float) getHeight();
    const auto& f = lnf.getFinish();

    // --- back panel: hardboard with the cabinet colour round the edge ------------------
    auto strip = Rectangle<float> (0, 0, W, (float) kStripBottom);
    g.setColour (f.body.darker (0.2f));
    g.fillRect (strip);
    auto board = strip.reduced (10.0f, 8.0f);
    g.setGradientFill (ColourGradient (Colour (0xff4a3a2c), 0, board.getY(), Colour (0xff2e241b), 0, board.getBottom(), false));
    g.fillRoundedRectangle (board, 4.0f);
    Random rng (200);
    for (int i = 0; i < 2500; ++i)
    {
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (0.05f));
        g.fillRect (board.getX() + rng.nextFloat() * board.getWidth(), board.getY() + rng.nextFloat() * board.getHeight(), 2.0f, 1.0f);
    }
    g.setColour (Colour (0xff120d09));   // vent holes either side of the serial plate
    for (float x = 270.0f; x < 740.0f; x += 16.0f)
        if (x < 380.0f || x > 620.0f)
            for (float y : { 22.0f, 36.0f })
                g.fillEllipse (x, y, 5, 5);
    RetroLookAndFeel::drawScrews (g, board, 8.0f);

    // effects-loop mod plates at each end
    g.setFont (lnf.labelFont (13.0f).withExtraKerningFactor (0.2f));
    for (auto [jack, label] : { std::pair<Point<float>, const char*> { kReturnJack, "LOOP RETURN" },
                                std::pair<Point<float>, const char*> { kSendJack, "LOOP SEND" } })
    {
        auto plate = Rectangle<float> (104, 66).withCentre (jack.translated (0, -2));
        RetroLookAndFeel::drawChromeRect (g, plate, 4.0f);
        drawJack (g, jack, 12.0f);
        g.setColour (Colour (0xff222020));
        g.drawText (label, plate.removeFromTop (14).translated (0, 1), Justification::centred);
    }

    // centre: serial plate and the stock jacks
    g.setColour (Colour (0xffd9cfb8));
    g.setFont (lnf.labelFont (12.0f).withExtraKerningFactor (0.3f));
    g.drawText ("MURLI ELECTRONIC PIANO  -  MODEL 200  -  FX LOOP MOD", Rectangle<float> (300, 64, 400, 14), Justification::centred);
    for (auto [x, label] : { std::pair<float, const char*> { 210.0f, "AUX OUT" }, { 790.0f, "PEDAL" } })
    {
        drawJack (g, { x, 40.0f }, 9.0f);
        g.setColour (Colour (0xffd9cfb8));
        g.drawText (label, Rectangle<float> (x - 40, 54, 80, 12), Justification::centred);
    }

    // --- pedalboard ---------------------------------------------------------------
    auto pb = Rectangle<float> (0, (float) kStripBottom, W, H - (float) kStripBottom);
    g.setGradientFill (ColourGradient (Colour (0xff2a2a2c), 0, pb.getY(), Colour (0xff141415), 0, pb.getBottom(), false));
    g.fillRect (pb);
    for (int i = 0; i < 9000; ++i)   // carpet
    {
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (0.06f));
        g.fillRect (rng.nextFloat() * W, pb.getY() + rng.nextFloat() * pb.getHeight(), 1.0f, 1.0f);
    }
    g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.55f), 0, pb.getY(), Colours::transparentBlack, 0, pb.getY() + 14, false));
    g.fillRect (pb.withHeight (14));
}

void RearPanel::paintOverChildren (Graphics& g)
{
    // jacks on the pedals' sides: input on the right, output on the left
    const float jy = 150.0f;
    auto in  = [jy] (Rectangle<int> b) { return Point<float> ((float) b.getRight() + 2.0f, jy); };
    auto out = [jy] (Rectangle<int> b) { return Point<float> ((float) b.getX() - 2.0f, jy); };

    const Colour cable (0xff3a4a5c), red (0xff9a2420);
    // SEND → chorus in
    drawCable (g, kSendJack, in (kChorusBox).translated (14, 0), 60.0f, red);
    drawPlug (g, in (kChorusBox), true);
    // chorus out → echo in, echo out → spring in
    drawCable (g, out (kChorusBox).translated (-14, 0), in (kEchoBox).translated (14, 0), 90.0f, cable);
    drawPlug (g, out (kChorusBox), false);
    drawPlug (g, in (kEchoBox), true);
    drawCable (g, out (kEchoBox).translated (-14, 0), in (kSpringBox).translated (14, 0), 90.0f, cable);
    drawPlug (g, out (kEchoBox), false);
    drawPlug (g, in (kSpringBox), true);
    // spring out → RETURN
    drawCable (g, out (kSpringBox).translated (-14, 0), kReturnJack, 60.0f, red);
    drawPlug (g, out (kSpringBox), false);
    // cable ends at the panel jacks
    for (auto j : { kSendJack, kReturnJack })
    {
        g.setColour (Colour (0xff1a1a1a));
        g.fillEllipse (Rectangle<float> (14, 14).withCentre (j));
        RetroLookAndFeel::drawChromeRect (g, Rectangle<float> (10, 10).withCentre (j), 5.0f);
    }
}
