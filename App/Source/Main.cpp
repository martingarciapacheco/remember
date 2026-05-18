#include <JuceHeader.h>
#include "MainComponent.h"

class RememberApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName()    override { return "Remember"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool               moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override { mainWindow.reset(); }

    void systemRequestedQuit() override { quit(); }

    // ── Main window ───────────────────────────────────────────────────────────
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name,
                              juce::Colour (0xff1a1a2e),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, false);
            centreWithSize (820, 620);
            setVisible (true);

            // Some hosts/launchers (and Windows in certain startup contexts) hand
            // us a SW_SHOWMINIMIZED hint that JUCE forwards to the native peer.
            // Force the window restored + frontmost on first show.
            setMinimised (false);
            toFront (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (RememberApplication)
