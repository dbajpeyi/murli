#include "RetroLookAndFeel.h"
#include <BinaryData.h>

using namespace juce;

const StringArray& Finish::names()
{
    static const StringArray n { "Coral", "Ebony", "Beige" };
    return n;
}

const Finish& Finish::byName (const String& name)
{
    static const Finish coral { "Coral", Colour (0xffc4473a), Colour (0xff7d2620), Colour (0xffeee4cc),
                                Colour (0xff3a2a22), Colour (0xfff6ecd4), Colour (0xff6e1a1a),
                                Colour (0xff1c1a19), Colour (0xfff1e8d2) };
    static const Finish ebony { "Ebony", Colour (0xff222020), Colour (0xff0c0b0b), Colour (0xffd9d5cc),
                                Colour (0xff1b1918), Colour (0xffebe1ca), Colour (0xff7d1818),
                                Colour (0xff1c1a19), Colour (0xfff1e8d2) };
    static const Finish beige { "Beige", Colour (0xffd8c4a0), Colour (0xff8f7752), Colour (0xff2e2926),
                                Colour (0xffece2cb), Colour (0xff5b3a26), Colour (0xff6e1a1a),
                                Colour (0xffeee6d3), Colour (0xff2a211c) };
    if (name == "Ebony") return ebony;
    if (name == "Beige") return beige;
    return coral;
}

RetroLookAndFeel::RetroLookAndFeel()
    : finish (&Finish::byName ("Coral"))
{
    logoFace  = Typeface::createSystemTypefaceFor (BinaryData::PacificoRegular_ttf, BinaryData::PacificoRegular_ttfSize);
    labelFace = Typeface::createSystemTypefaceFor (BinaryData::BebasNeueRegular_ttf, BinaryData::BebasNeueRegular_ttfSize);
    setDefaultSansSerifTypeface (labelFace);
}

Font RetroLookAndFeel::logoFont (float height) const  { return Font (FontOptions (logoFace).withHeight (height)); }
Font RetroLookAndFeel::labelFont (float height) const { return Font (FontOptions (labelFace).withHeight (height)); }

void RetroLookAndFeel::drawChromeRect (Graphics& g, Rectangle<float> r, float corner)
{
    ColourGradient grad (Colour (0xfff4f4f2), r.getX(), r.getY(), Colour (0xff8d8d8a), r.getX(), r.getBottom(), false);
    grad.addColour (0.45, Colour (0xffc9c9c5));
    grad.addColour (0.55, Colour (0xffa3a39f));
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, corner);
    g.setColour (Colours::black.withAlpha (0.35f));
    g.drawRoundedRectangle (r, corner, 0.8f);
}

void RetroLookAndFeel::drawScrews (Graphics& g, Rectangle<float> area, float inset)
{
    const Point<float> pts[] = { area.getTopLeft() + Point<float> (inset, inset),
                                 area.getTopRight() + Point<float> (-inset, inset),
                                 area.getBottomLeft() + Point<float> (inset, -inset),
                                 area.getBottomRight() + Point<float> (-inset, -inset) };
    for (auto c : pts)
    {
        const float r = 4.0f;
        g.setGradientFill (ColourGradient (Colour (0xffe8e8e4), c.x - r, c.y - r, Colour (0xff6c6c68), c.x + r, c.y + r, true));
        g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
        g.setColour (Colour (0xff4a4a46));
        g.drawLine (c.x - r * 0.7f, c.y + r * 0.2f, c.x + r * 0.7f, c.y - r * 0.2f, 1.2f);
    }
}

// Skirted "chicken-head" knob: chrome skirt, bakelite pointer body, inlaid line.
void RetroLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos,
                                         float startAngle, float endAngle, Slider& s)
{
    const auto bounds = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const auto& f = *finish;

    if (s.getProperties().contains ("pedal"))   // knurled black stompbox knob
    {
        const float kr = radius * 0.92f;
        g.setColour (Colours::black.withAlpha (0.45f));
        g.fillEllipse (c.x - kr + 2, c.y - kr + 4, kr * 2, kr * 2);
        g.setGradientFill (ColourGradient (Colour (0xff3a3836), c.x - kr, c.y - kr, Colour (0xff050505), c.x + kr, c.y + kr, false));
        g.fillEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2);
        g.setColour (Colours::white.withAlpha (0.10f));
        for (int i = 0; i < 36; ++i)   // knurling
        {
            const float a = angle + MathConstants<float>::twoPi * (float) i / 36.0f;
            g.drawLine (c.x + std::sin (a) * kr * 0.78f, c.y - std::cos (a) * kr * 0.78f,
                        c.x + std::sin (a) * kr * 0.98f, c.y - std::cos (a) * kr * 0.98f, 1.0f);
        }
        const float cap = kr * 0.7f;
        g.setGradientFill (ColourGradient (Colour (0xff2c2a29), c.x, c.y - cap, Colour (0xff0b0b0b), c.x, c.y + cap, false));
        g.fillEllipse (c.x - cap, c.y - cap, cap * 2, cap * 2);
        g.setColour (Colours::white);
        g.drawLine (c.x + std::sin (angle) * kr * 0.25f, c.y - std::cos (angle) * kr * 0.25f,
                    c.x + std::sin (angle) * kr * 0.92f, c.y - std::cos (angle) * kr * 0.92f, 2.0f);
        return;
    }

    // drop shadow
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillEllipse (c.x - radius * 0.86f + 2.5f, c.y - radius * 0.86f + 4.0f, radius * 1.72f, radius * 1.72f);

    // chrome skirt with radial knurl
    const float skirt = radius * 0.86f;
    g.setGradientFill (ColourGradient (Colour (0xfff7f7f5), c.x - skirt, c.y - skirt, Colour (0xff77776f), c.x + skirt, c.y + skirt, false));
    g.fillEllipse (c.x - skirt, c.y - skirt, skirt * 2, skirt * 2);
    g.setColour (Colours::black.withAlpha (0.18f));
    for (int i = 0; i < 48; ++i)
    {
        const float a = angle + MathConstants<float>::twoPi * (float) i / 48.0f;
        g.drawLine (c.x + std::sin (a) * skirt * 0.80f, c.y - std::cos (a) * skirt * 0.80f,
                    c.x + std::sin (a) * skirt * 0.98f, c.y - std::cos (a) * skirt * 0.98f, 0.8f);
    }
    g.setColour (Colours::black.withAlpha (0.4f));
    g.drawEllipse (c.x - skirt, c.y - skirt, skirt * 2, skirt * 2, 0.8f);

    // chicken-head body: a round boss plus a tapering pointer
    Path body;
    const float boss = radius * 0.52f;
    body.addEllipse (-boss, -boss, boss * 2, boss * 2);
    Path pointer;
    pointer.startNewSubPath (-boss * 0.62f, -boss * 0.35f);
    pointer.quadraticTo (-boss * 0.30f, -radius * 0.98f, 0.0f, -radius * 1.0f);
    pointer.quadraticTo (boss * 0.30f, -radius * 0.98f, boss * 0.62f, -boss * 0.35f);
    pointer.closeSubPath();
    body.addPath (pointer);
    body.applyTransform (AffineTransform::rotation (angle).translated (c));

    g.setColour (Colours::black.withAlpha (0.4f));
    g.fillPath (body, AffineTransform::translation (1.5f, 2.5f));
    const auto knobCol = f.knob;
    g.setGradientFill (ColourGradient (knobCol.brighter (0.55f), c.x - boss, c.y - boss * 1.4f,
                                       knobCol.darker (0.5f), c.x + boss, c.y + boss, false));
    g.fillPath (body);
    g.setColour (knobCol.darker (0.8f));
    g.strokePath (body, PathStrokeType (0.8f));

    // glossy highlight on the boss
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.35f), c.x, c.y - boss,
                                       Colours::transparentWhite, c.x, c.y + boss * 0.2f, false));
    g.fillEllipse (c.x - boss * 0.8f, c.y - boss * 0.95f, boss * 1.6f, boss * 1.1f);

    // inlaid pointer line
    const Point<float> tip (c.x + std::sin (angle) * radius * 0.9f, c.y - std::cos (angle) * radius * 0.9f);
    const Point<float> base (c.x + std::sin (angle) * boss * 0.2f, c.y - std::cos (angle) * boss * 0.2f);
    g.setColour (f.knobPointer);
    g.drawLine ({ base, tip }, 2.2f);

    if (! s.isEnabled())
    {
        g.setColour (Colours::black.withAlpha (0.4f));
        g.fillEllipse (bounds);
    }
}

void RetroLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    drawChromeRect (g, r, 3.0f);
    if (highlighted || down)
    {
        g.setColour (Colours::black.withAlpha (down ? 0.18f : 0.08f));
        g.fillRoundedRectangle (r, 3.0f);
    }
}

void RetroLookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool, bool)
{
    g.setColour (Colour (0xff2a2622));
    g.setFont (labelFont ((float) b.getHeight() * 0.72f).withExtraKerningFactor (0.08f));
    g.drawText (b.getButtonText(), b.getLocalBounds(), Justification::centred);
}
