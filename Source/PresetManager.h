#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Factory presets (compiled in) and user presets (one XML file each, in a folder the
// user can back up or share). A preset stores parameter values only — not the cabinet
// finish or which side of the instrument is showing.
class PresetManager
{
public:
    struct Entry
    {
        juce::String name;
        bool factory = false;
        int factoryIndex = -1;
        juce::File file;
    };

    explicit PresetManager (juce::AudioProcessorValueTreeState&);

    static juce::File userFolder();
    static constexpr const char* kExtension = ".murlipreset";

    // Factory presets first, then the user's, alphabetically. Call rescan() after
    // something outside this class might have changed the folder.
    const std::vector<Entry>& entries() const { return list; }
    void rescan();
    int numFactory() const;

    // Message thread only (they move parameters with host notification).
    void load (int index);
    void loadFactory (int factoryIndex);
    bool save (const juce::String& name);
    bool remove (int index);
    void step (int delta);

    int currentIndex() const;
    juce::String currentName() const;

    // True once any parameter differs from the loaded preset.
    bool isDirty() const;

    // Session state: name and dirty flag ride along in the plugin state.
    void writeToState();
    void readFromState();

private:
    void applyValues (const std::map<juce::String, float>& values);
    void takeSnapshot();

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<Entry> list;
    std::vector<std::pair<juce::RangedAudioParameter*, float>> snapshot;   // normalised values
    bool restoredDirty = false;
};
