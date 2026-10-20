#include <JuceHeader.h>
#include "AppShell.h"
#include "RememberLogger.h"
#include "BuildInfo.h"
#include "../../Shared/RememberLookAndFeel.h"

class RememberApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Remember"; }
    const juce::String getApplicationVersion() override { return REMEMBER_VERSION; }
    bool moreThanOneInstanceAllowed() override { return false; }
    void initialise (const juce::String&) override
    {
        // This standalone process owns its theme, including alerts/tooltips.
        // The VST3 uses component-scoped styles and never changes its host.
        juce::LookAndFeel::setDefaultLookAndFeel (&appTheme.get());
        RememberLogger::ensureInitialised ("app", juce::String ("App ") + REMEMBER_VERSION_LINE + " | " + REMEMBER_BUILD_PROVENANCE_LINE);
        juce::Logger::writeToLog ("App started.");
        shell = std::make_unique<AppShell> ([] { quit(); });
    }
    void anotherInstanceStarted (const juce::String&) override { if (shell) shell->open(); }
    void systemRequestedQuit() override { if (shell) shell->requestQuit(); }
    void shutdown() override
    {
        juce::Logger::writeToLog ("App shutting down.");
        shell.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }
private:
    juce::SharedResourcePointer<RememberUI::LookAndFeel> appTheme;
    std::unique_ptr<AppShell> shell;
};
START_JUCE_APPLICATION (RememberApplication)
