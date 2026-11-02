#include "PresetManager.h"
#include "PluginProcessor.h"

using namespace juce;

namespace
{
    // Values are in each parameter's own units (Hz, dB, 0..1, choice index, 0/1).
    // Anything a preset doesn't mention goes back to its default.
    struct FactoryPreset
    {
        const char* name;
        std::vector<std::pair<const char*, float>> values;
    };

    const std::vector<FactoryPreset>& factoryPresets()
    {
        using namespace ParamID;
        static const std::vector<FactoryPreset> presets {
            { "Classic 200A", {} },
            { "Soft Ballad", { { volume, 0.62f }, { bark, 0.7f }, { attack, 0.7f },
                               { rvOn, 1 }, { rvDwell, 0.3f }, { rvDecay, 0.45f }, { rvTone, 0.4f }, { rvMix, 0.22f } } },
            { "Barking Lead", { { volume, 0.8f }, { bark, 1.4f }, { drive, 5.0f }, { attack, 1.3f } } },
            { "Vibrato Soul", { { vibrato, 0.6f }, { speed, 5.63f }, { bark, 1.1f }, { volume, 0.72f } } },
            { "Direct Out", { { speaker, 0.0f }, { bark, 1.05f } } },
            { "Cranked Amp", { { volume, 1.0f }, { drive, 9.0f }, { bark, 1.3f }, { attack, 1.2f } } },
            { "Dusty Rehearsal Room", { { vibrato, 0.35f }, { noise, 0.45f }, { bark, 1.15f }, { tune, 438.0f },
                                        { rvOn, 1 }, { rvDwell, 0.45f }, { rvMix, 0.3f } } },
            { "Shimmer Chorus", { { chOn, 1 }, { chRate, 0.6f }, { chDepth, 0.4f }, { chAnimate, 0.25f },
                                  { chIntensity, 0.55f }, { chDimension, 0.2f },
                                  { rvOn, 1 }, { rvMix, 0.25f }, { rvDecay, 0.55f } } },
            { "Seasick Chorus", { { chOn, 1 }, { chRate, 1.4f }, { chShape, 0.35f }, { chDepth, 0.85f },
                                  { chAnimate, 0.8f }, { chIntensity, 0.8f }, { chDimension, 0.35f } } },
            { "Drum Echo Dub", { { bark, 1.2f }, { ecOn, 1 }, { ecHeads, 7 }, { ecMode, 1 }, { ecSwell, 0.55f },
                                 { ecTone, 0.35f }, { ecMix, 0.5f }, { rvOn, 1 }, { rvMix, 0.3f } } },
            { "Echo Swell Ambient", { { volume, 0.6f }, { bark, 0.8f }, { chOn, 1 }, { chRate, 0.3f }, { chDepth, 0.5f },
                                      { ecOn, 1 }, { ecHeads, 11 }, { ecMode, 2 }, { ecSwell, 0.75f }, { ecMix, 0.45f },
                                      { ecTime, 1.2f }, { rvOn, 1 }, { rvDecay, 0.85f }, { rvMix, 0.4f } } },
            { "Surf Spring", { { bark, 1.2f }, { vibrato, 0.45f }, { rvOn, 1 }, { rvDwell, 0.9f }, { rvDecay, 0.6f },
                               { rvTone, 0.65f }, { rvMix, 0.55f } } },
        };
        return presets;
    }

    // Normalised value for a parameter in its own units.
    float normalised (RangedAudioParameter& p, float v) { return p.convertTo0to1 (v); }
}

PresetManager::PresetManager (AudioProcessorValueTreeState& state) : apvts (state)
{
    rescan();
    takeSnapshot();
}

File PresetManager::userFolder()
{
   #if JUCE_MAC
    return File::getSpecialLocation (File::userHomeDirectory).getChildFile ("Library/Audio/Presets/Murli");
   #else
    return File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Murli/Presets");
   #endif
}

int PresetManager::numFactory() const { return (int) factoryPresets().size(); }

void PresetManager::rescan()
{
    list.clear();
    for (int i = 0; i < numFactory(); ++i)
        list.push_back ({ factoryPresets()[(size_t) i].name, true, i, {} });

    auto files = userFolder().findChildFiles (File::findFiles, false, String ("*") + kExtension);
    files.sort();
    for (auto& f : files)
        list.push_back ({ f.getFileNameWithoutExtension(), false, -1, f });
}

void PresetManager::applyValues (const std::map<String, float>& values)
{
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
        {
            const auto it = values.find (rp->getParameterID());
            const float target = it != values.end() ? normalised (*rp, it->second) : rp->getDefaultValue();
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (target);
            rp->endChangeGesture();
        }
}

void PresetManager::loadFactory (int factoryIndex)
{
    if (! isPositiveAndBelow (factoryIndex, numFactory()))
        return;
    std::map<String, float> values;
    for (auto& [id, v] : factoryPresets()[(size_t) factoryIndex].values)
        values[id] = v;
    applyValues (values);
    apvts.state.setProperty ("preset", factoryPresets()[(size_t) factoryIndex].name, nullptr);
    restoredDirty = false;
    takeSnapshot();
}

void PresetManager::load (int index)
{
    if (! isPositiveAndBelow (index, (int) list.size()))
        return;
    const auto& e = list[(size_t) index];
    if (e.factory)
    {
        loadFactory (e.factoryIndex);
        return;
    }

    auto xml = parseXML (e.file);
    if (xml == nullptr || ! xml->hasTagName ("MurliPreset"))
        return;
    std::map<String, float> values;
    for (auto* p : xml->getChildWithTagNameIterator ("Param"))
        values[p->getStringAttribute ("id")] = (float) p->getDoubleAttribute ("value");
    applyValues (values);
    apvts.state.setProperty ("preset", e.name, nullptr);
    restoredDirty = false;
    takeSnapshot();
}

bool PresetManager::save (const String& rawName)
{
    const auto name = File::createLegalFileName (rawName.trim());
    if (name.isEmpty())
        return false;

    XmlElement xml ("MurliPreset");
    xml.setAttribute ("version", 1);
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
        {
            auto* e = xml.createNewChildElement ("Param");
            e->setAttribute ("id", rp->getParameterID());
            e->setAttribute ("value", (double) rp->convertFrom0to1 (rp->getValue()));
        }

    auto folder = userFolder();
    if (! folder.createDirectory())
        return false;
    if (! xml.writeTo (folder.getChildFile (name + kExtension)))
        return false;

    rescan();
    apvts.state.setProperty ("preset", name, nullptr);
    restoredDirty = false;
    takeSnapshot();
    return true;
}

bool PresetManager::remove (int index)
{
    if (! isPositiveAndBelow (index, (int) list.size()) || list[(size_t) index].factory)
        return false;
    const bool wasCurrent = index == currentIndex();
    if (! list[(size_t) index].file.deleteFile())
        return false;
    rescan();
    if (wasCurrent)
        restoredDirty = true;   // the sound stays, but it no longer matches a saved preset
    return true;
}

void PresetManager::step (int delta)
{
    const int n = (int) list.size();
    if (n == 0) return;
    const int cur = currentIndex();
    load (cur < 0 ? 0 : ((cur + delta) % n + n) % n);
}

int PresetManager::currentIndex() const
{
    const auto name = currentName();
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].name == name)
            return (int) i;
    return -1;
}

String PresetManager::currentName() const
{
    return apvts.state.getProperty ("preset", "Classic 200A").toString();
}

void PresetManager::takeSnapshot()
{
    snapshot.clear();
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            snapshot.push_back ({ rp, rp->getValue() });
}

bool PresetManager::isDirty() const
{
    if (restoredDirty)
        return true;
    for (auto& [p, v] : snapshot)
        if (std::abs (p->getValue() - v) > 1.0e-4f)
            return true;
    return false;
}

void PresetManager::writeToState()
{
    apvts.state.setProperty ("presetDirty", isDirty(), nullptr);
}

void PresetManager::readFromState()
{
    takeSnapshot();
    restoredDirty = (bool) apvts.state.getProperty ("presetDirty", false);
}
