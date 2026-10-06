#include <JuceHeader.h>
#include "MainComponent.h"
#include "RememberLogger.h"
#include "BuildInfo.h"

class RememberApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName()    override { return "Remember"; }
    const juce::String getApplicationVersion() override { return REMEMBER_VERSION; }
    bool               moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        RememberLogger::ensureInitialised ("app",
            juce::String ("App ") + REMEMBER_VERSION_LINE);
        juce::Logger::writeToLog ("App started.");

        trayIcon = std::make_unique<TrayIcon> ([this] { showMainWindow(); },
                                             [this] { systemRequestedQuit(); });
        showMainWindow();
    }

    void anotherInstanceStarted (const juce::String&) override
    {
        showMainWindow();
    }

    void shutdown() override
    {
        juce::Logger::writeToLog ("App shutting down.");
        juce::PopupMenu::dismissAllActiveMenus();
        trayIcon.reset();
        mainWindow.reset();
        // The FileLogger leak is intentional — letting JUCE tear it down here
        // would close the file before any late destructor logging.
    }

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
        }

        void closeButtonPressed() override
        {
            setVisible (false);
        }
    };

private:
    class TrayIcon final : public juce::SystemTrayIconComponent
    {
    public:
        TrayIcon (std::function<void()> open, std::function<void()> exit)
            : onOpen (std::move (open)), onExit (std::move (exit))
        {
            setIconImage (createIcon(), {});
            setIconTooltip ("Remember - click to open");
        }

        void mouseDown (const juce::MouseEvent& event) override
        {
            if (event.mods.isPopupMenu())
            {
                juce::PopupMenu menu;
                menu.addItem (1, "Open Remember");
                menu.addSeparator();
                menu.addItem (2, "Exit");
                menu.showMenuAsync (juce::PopupMenu::Options(),
                    [safe = juce::Component::SafePointer<TrayIcon> (this)] (int result)
                    {
                        if (safe == nullptr)
                            return;

                        if (result == 1)
                            safe->onOpen();
                        else if (result == 2)
                            safe->onExit();
                    });
            }
            else if (event.mods.isLeftButtonDown())
            {
                onOpen();
            }
        }

        void mouseDoubleClick (const juce::MouseEvent& event) override
        {
            if (event.mods.isLeftButtonDown())
                onOpen();
        }

    private:
        static juce::Image createIcon()
        {
            juce::Image icon (juce::Image::ARGB, 32, 32, true);
            juce::Graphics graphics (icon);
            graphics.setColour (juce::Colour (0xff8b5cf6));
            graphics.fillRoundedRectangle (1.0f, 1.0f, 30.0f, 30.0f, 7.0f);
            graphics.setColour (juce::Colours::white);

            juce::Path waveform;
            waveform.startNewSubPath (6.0f, 16.0f);
            waveform.lineTo (10.0f, 16.0f);
            waveform.lineTo (13.0f, 8.0f);
            waveform.lineTo (18.0f, 24.0f);
            waveform.lineTo (22.0f, 16.0f);
            waveform.lineTo (26.0f, 16.0f);
            graphics.strokePath (waveform, juce::PathStrokeType (2.5f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            return icon;
        }

        std::function<void()> onOpen;
        std::function<void()> onExit;
    };

    void showMainWindow()
    {
        if (mainWindow == nullptr)
            mainWindow = std::make_unique<MainWindow> (getApplicationName());

        mainWindow->setVisible (true);
        mainWindow->setMinimised (false);
        mainWindow->toFront (true);
    }

    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<TrayIcon> trayIcon;
};

START_JUCE_APPLICATION (RememberApplication)
