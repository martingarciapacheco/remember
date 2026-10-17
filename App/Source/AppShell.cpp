#include "AppShell.h"
#include "MainComponent.h"
#include "ProjectScanner.h"
#include "../../Shared/LibraryStore.h"
#include "../../Shared/BackgroundRetirement.h"
#include "WindowsTaskbarTransport.h"

class AppShell::MainWindow : public juce::DocumentWindow, private juce::Timer
{
public:
    explicit MainWindow (const juce::String& name)
        : DocumentWindow (name,
                          RememberUI::colour (Pal::BgDeep),
                          DocumentWindow::allButtons)
    {
       #if JUCE_WINDOWS
        // JUCE retains native caption hit testing (drag, snap, system menu and
        // window controls) while the shared theme paints the client title bar.
        setLookAndFeel (&windowTheme);
        setUsingNativeTitleBar (false);
        setTitleBarHeight (34);
        setTitleBarButtonsRequired (DocumentWindow::allButtons, false);
       #else
        setUsingNativeTitleBar (true);
       #endif
        setContentOwned (new RememberUI::ZoomView (std::make_unique<MainComponent>()), true);
        setResizable (true, false);
        const auto frame = getContentComponentBorder();
        setResizeLimits (640 + frame.getLeftAndRight(), 480 + frame.getTopAndBottom(), 4096, 4096);
        centreWithSize (960 + frame.getLeftAndRight(), 680 + frame.getTopAndBottom());
    }

    void activate()
    {
        const bool firstShow = ! boundsReady;
        // Native frame adjustments during the first show are initialisation,
        // not a user move. Read saved bounds against the final shown geometry.
        setVisible (true);
       #if JUCE_WINDOWS
        if (! taskbar && getPeer() != nullptr)
        {
            auto safe = juce::Component::SafePointer<MainComponent> (mainContent());
            taskbar = std::make_unique<WindowsTaskbarTransport> (static_cast<HWND> (getPeer()->getNativeHandle()),
                [safe]
                {
                    return safe == nullptr ? WindowsTaskbarTransport::State{}
                        : WindowsTaskbarTransport::State { safe->taskbarPlaying(), safe->taskbarCanPlay(), safe->taskbarHasQueue() };
                }, [safe] (int command)
                {
                    juce::MessageManager::callAsync ([safe, command]
                    {
                        if (safe == nullptr) return;
                        if (command == 0) safe->taskbarPrevious();
                        else if (command == 1) safe->taskbarTogglePlay();
                        else if (command == 2) safe->taskbarNext();
                        else if (command == 3) safe->mediaPlay();
                        else if (command == 4) safe->mediaPause();
                        else if (command == 5) safe->mediaStop();
                    });
                });
        }
       #endif
        if (auto* content = mainContent()) content->setUiActive (true);
        setMinimised (false);
        toFront (true);
        if (! firstShow) return;
        boundsReady = true;
        lastObservedBounds = getBounds();
        auto savedBounds = std::make_shared<juce::String>();
        const auto initialBounds = getBounds();
        ProjectScanner::queryAsync ([savedBounds]
        {
            return ProjectScanner::readWindowState (*savedBounds);
        }, [safe = juce::Component::SafePointer<MainWindow> (this), savedBounds, initialBounds] (juce::Result result)
        {
            if (safe == nullptr) return;
            safe->boundsReadSucceeded = result.wasOk();
            // A late disk read must not undo a move/resize by the user.
            if (result.wasOk() && ! safe->boundsEdited && safe->getBounds() == initialBounds
                && savedBounds->isNotEmpty())
            {
                safe->boundsReady = false;
                safe->restoreWindowStateFromString (*savedBounds);
                safe->lastObservedBounds = safe->getBounds();
                safe->boundsReady = true;
            }
        });
    }

    bool hasTaskbarButtons() const
    {
       #if JUCE_WINDOWS
        return taskbar && taskbar->hasButtons();
       #else
        return false;
       #endif
    }

    ~MainWindow() override
    {
        setLookAndFeel (nullptr);
       #if JUCE_WINDOWS
        taskbar.reset();
       #endif
        stopTimer(); persistBounds();
    }
    void moved() override
    {
        DocumentWindow::moved(); // retain the latest visible position before hiding
        boundsChanged();
    }
    void resized() override
    {
        DocumentWindow::resized();
        boundsChanged();
    }

    void lookAndFeelChanged() override
    {
        setBackgroundColour (RememberUI::colour (Pal::BgDeep));
        DocumentWindow::lookAndFeelChanged();
    }

    void closeButtonPressed() override
    {
        ProjectScanner::setLibraryVisible (false);
        if (auto* content = mainContent()) content->setUiActive (false);
        setVisible (false);
    }
private:
    MainComponent* mainContent() const
    {
        auto* zoom = dynamic_cast<RememberUI::ZoomView*> (getContentComponent());
        return zoom ? dynamic_cast<MainComponent*> (zoom->getContent()) : nullptr;
    }
    void boundsChanged()
    {
        if (! boundsReady || isMinimised() || lastObservedBounds == getBounds()) return;
        lastObservedBounds = getBounds();
        boundsEdited = true;
        startTimer (600);
    }
    void timerCallback() override { stopTimer(); persistBounds(); }
    void persistBounds()
    {
        // Untouched startup geometry is provisional until a successful
        // read. A deliberate move/resize remains authoritative even if
        // the old settings read is still pending or failed.
        if (! boundsReady || (! boundsReadSucceeded && ! boundsEdited) || isMinimised()) return;
        const auto bounds = getWindowStateAsString();
        ProjectScanner::editAsync ([bounds]
        {
            juce::var patch (new juce::DynamicObject());
            patch.getDynamicObject()->setProperty ("windowState", bounds);
            return LibraryStore::patchJson (LibraryStore::configFile(), patch, true);
        });
    }
    bool boundsReady = false;
   #if JUCE_WINDOWS
    RememberUI::MainWindowLookAndFeel windowTheme;
    std::unique_ptr<WindowsTaskbarTransport> taskbar;
   #endif
    bool boundsEdited = false;
    bool boundsReadSucceeded = false;
    juce::Rectangle<int> lastObservedBounds;
};


class AppShell::TrayIcon final : public juce::SystemTrayIconComponent
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
        graphics.setColour (RememberUI::colour (Pal::Accent));
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

AppShell::AppShell (std::function<void()> onReadyToQuit) : readyToQuit (std::move (onReadyToQuit))
{
    trayIcon = std::make_unique<TrayIcon> ([this] { open(); }, [this] { requestQuit(); });
    open();
}
AppShell::~AppShell()
{
    stopTimer();
    juce::PopupMenu::dismissAllActiveMenus();
    trayIcon.reset();
    mainWindow.reset();
}
void AppShell::open()
{
    if (closing) return;
    if (mainWindow == nullptr) mainWindow = std::make_unique<MainWindow> ("Remember");
    ProjectScanner::setLibraryVisible (true);
    ProjectScanner::refreshAsync();
    mainWindow->activate();
}
void AppShell::requestQuit()
{
    if (closing) return;
    closing = true;
    juce::PopupMenu::dismissAllActiveMenus();
    mainWindow.reset();
    ProjectScanner::beginShutdown();
    trayIcon->setIconTooltip ("Remember - finishing background work");
    startTimer (50);
}
bool AppShell::hasTaskbarButtons() const { return mainWindow && mainWindow->hasTaskbarButtons(); }
void AppShell::timerCallback()
{
    if (ProjectScanner::shutdownReady() && WaveformPlayerComponent::pendingLoads() == 0
        && BackgroundRetirement::pending() == 0)
    { stopTimer(); if (readyToQuit) readyToQuit(); }
}
