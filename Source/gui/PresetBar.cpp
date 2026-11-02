#include "PresetBar.h"

using namespace juce;

PresetBar::PresetBar (PresetManager& pm) : presets (pm)
{
    addAndMakeVisible (prev);
    addAndMakeVisible (display);
    addAndMakeVisible (next);
    addAndMakeVisible (save);
    prev.setTooltip ("Previous preset");
    next.setTooltip ("Next preset");
    save.setTooltip ("Save the current sound as a preset");
    prev.onClick = [this] { presets.rescan(); presets.step (-1); refresh(); };
    next.onClick = [this] { presets.rescan(); presets.step (1); refresh(); };
    save.onClick = [this] { askToSave(); };
    refresh();
}

void PresetBar::resized()
{
    auto r = getLocalBounds();
    const int h = r.getHeight();
    save.setBounds (r.removeFromRight (64));
    r.removeFromRight (6);
    prev.setBounds (r.removeFromLeft (h + 4));
    next.setBounds (r.removeFromRight (h + 4));
    display.setBounds (r.reduced (4, 0));
}

void PresetBar::refresh()
{
    const int index = presets.currentIndex();
    String text;
    if (index >= 0 && presets.entries()[(size_t) index].factory)
        text << String (index + 1).paddedLeft ('0', 2) << "  ";
    else
        text << "USER  ";
    text << presets.currentName().toUpperCase();
    if (presets.isDirty())
        text << " *";
    if (text != display.text)
    {
        display.text = text;
        display.repaint();
    }
}

void PresetBar::showMenu()
{
    presets.rescan();
    const auto& list = presets.entries();
    const int current = presets.currentIndex();

    PopupMenu menu, factory, user;
    for (size_t i = 0; i < list.size(); ++i)
        (list[i].factory ? factory : user).addItem ((int) i + 1, list[i].name, true, (int) i == current);

    menu.addSectionHeader ("Factory");
    for (PopupMenu::MenuItemIterator it (factory); it.next();)
        menu.addItem (it.getItem());
    menu.addSectionHeader ("User");
    if (user.getNumItems() == 0)
        menu.addItem (-100, "(none saved yet)", false);
    for (PopupMenu::MenuItemIterator it (user); it.next();)
        menu.addItem (it.getItem());

    menu.addSeparator();
    constexpr int kSaveAs = 10001, kDelete = 10002, kFolder = 10003;
    menu.addItem (kSaveAs, "Save As...");
    const bool canDelete = current >= 0 && ! list[(size_t) current].factory;
    menu.addItem (kDelete, canDelete ? "Delete \"" + list[(size_t) current].name + "\"" : String ("Delete"), canDelete);
    menu.addItem (kFolder, "Show Presets Folder");

    menu.setLookAndFeel (&getLookAndFeel());
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&display),
                        [this, current] (int result)
                        {
                            if (result == kSaveAs) askToSave();
                            else if (result == kDelete) presets.remove (current);
                            else if (result == kFolder)
                            {
                                auto folder = PresetManager::userFolder();
                                folder.createDirectory();
                                folder.revealToUser();
                            }
                            else if (result > 0) presets.load (result - 1);
                            refresh();
                        });
}

void PresetBar::askToSave()
{
    const int index = presets.currentIndex();
    const bool userPreset = index >= 0 && ! presets.entries()[(size_t) index].factory;

    dialog = std::make_unique<AlertWindow> ("Save Preset", "Name this sound:", MessageBoxIconType::NoIcon, this);
    dialog->addTextEditor ("name", userPreset ? presets.currentName() : String ("My ") + presets.currentName());
    dialog->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    dialog->setLookAndFeel (&getLookAndFeel());   // it's a top-level window, so it won't inherit ours
    dialog->enterModalState (true, ModalCallbackFunction::create ([safeThis = SafePointer<PresetBar> (this)] (int result)
    {
        auto* self = safeThis.getComponent();
        if (self == nullptr)   // the editor closed while the dialog was up
            return;
        if (result == 1 && self->dialog != nullptr)
        {
            const auto name = self->dialog->getTextEditorContents ("name");
            if (! self->presets.save (name))
                AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Save Preset",
                                                  "Couldn't save \"" + name + "\" to " + PresetManager::userFolder().getFullPathName());
        }
        if (self->dialog != nullptr)
            self->dialog->setLookAndFeel (nullptr);
        self->dialog.reset();
        self->refresh();
    }), false);

    if (auto* editor = dialog->getTextEditor ("name"))
    {
        editor->grabKeyboardFocus();
        editor->selectAll();
    }
}

void PresetBar::StepButton::paintButton (Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    RetroLookAndFeel::drawChromeRect (g, r, 3.0f);
    if (highlighted || down)
    {
        g.setColour (Colours::black.withAlpha (down ? 0.18f : 0.08f));
        g.fillRoundedRectangle (r, 3.0f);
    }
    const auto c = r.getCentre();
    const float s = r.getHeight() * 0.22f;
    Path tri;
    if (fwd) tri.addTriangle (c.x - s * 0.8f, c.y - s, c.x - s * 0.8f, c.y + s, c.x + s, c.y);
    else     tri.addTriangle (c.x + s * 0.8f, c.y - s, c.x + s * 0.8f, c.y + s, c.x - s, c.y);
    g.setColour (Colour (0xff2a2622));
    g.fillPath (tri);
}

void PresetBar::Display::paint (Graphics& g)
{
    auto& lnf = static_cast<RetroLookAndFeel&> (getLookAndFeel());
    auto r = getLocalBounds().toFloat();
    // dark glass in a chrome bezel, like a radio dial window
    RetroLookAndFeel::drawChromeRect (g, r, 5.0f);
    auto glass = r.reduced (3.0f);
    g.setGradientFill (ColourGradient (Colour (0xff231a12), glass.getX(), glass.getY(), Colour (0xff0d0906), glass.getX(), glass.getBottom(), false));
    g.fillRoundedRectangle (glass, 3.0f);

    g.setFont (lnf.labelFont (glass.getHeight() * 0.78f).withExtraKerningFactor (0.12f));
    auto textArea = glass.reduced (10.0f, 0.0f);
    g.setColour (Colour (0xffffb347).withAlpha (0.35f));   // phosphor bloom
    g.drawText (text, textArea.translated (0.0f, 0.5f).expanded (0.5f), Justification::centredLeft, true);
    g.setColour (Colour (0xffffc46b));
    g.drawText (text, textArea, Justification::centredLeft, true);

    // small down-arrow: it's a menu
    const float s = 4.0f;
    const Point<float> a (glass.getRight() - 12.0f, glass.getCentreY());
    Path tri;
    tri.addTriangle (a.x - s, a.y - s * 0.5f, a.x + s, a.y - s * 0.5f, a.x, a.y + s * 0.6f);
    g.fillPath (tri);

    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.12f), glass.getX(), glass.getY(),
                                       Colours::transparentWhite, glass.getX(), glass.getCentreY(), false));
    g.fillRoundedRectangle (glass.withHeight (glass.getHeight() * 0.5f), 3.0f);
}
