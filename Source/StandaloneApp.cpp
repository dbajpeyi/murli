// Standalone application: JUCE's stock standalone wrapper, but it asks the audio
// device for a 64-sample buffer at 48 kHz (≈1.3 ms) and opens every MIDI input it
// finds, so a controller plugged in before launch just plays.
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_processors/juce_audio_processors.h>

#if JucePlugin_Build_Standalone

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace
{
using namespace juce;

class MurliStandaloneApp final : public JUCEApplication
{
public:
    MurliStandaloneApp()
    {
        PropertiesFile::Options options;
        options.applicationName     = CharPointer_UTF8 (JucePlugin_Name);
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        options.folderName          = "~/.config";
       #endif
        appProperties.setStorageParameters (options);
    }

    const String getApplicationName() override    { return CharPointer_UTF8 (JucePlugin_Name); }
    const String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override    { return true; }
    void anotherInstanceStarted (const String&) override {}

    void initialise (const String&) override
    {
        AudioDeviceManager::AudioDeviceSetup lowLatency;
        lowLatency.sampleRate = 48000.0;
        lowLatency.bufferSize = 64;

        auto holder = std::make_unique<StandalonePluginHolder> (appProperties.getUserSettings(),
                                                                false,
                                                                String(),
                                                                &lowLatency,
                                                                Array<StandalonePluginHolder::PluginInOuts>(),
                                                                true /* auto-open MIDI inputs */);

        mainWindow = std::make_unique<StandaloneFilterWindow> (getApplicationName(),
                                                               Colour (0xff121010),
                                                               std::move (holder));
        mainWindow->setResizable (true, false);
        mainWindow->setVisible (true);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            mainWindow->pluginHolder->savePluginState();

        if (ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            Timer::callAfterDelay (100, []
            {
                if (auto app = JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

private:
    ApplicationProperties appProperties;
    std::unique_ptr<StandaloneFilterWindow> mainWindow;
};
} // namespace

juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication() { return new MurliStandaloneApp(); }

#endif
