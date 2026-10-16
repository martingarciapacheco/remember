#pragma once
#include <JuceHeader.h>
#include <functional>
#include <memory>

// The production window/tray lifecycle. JUCEApplication supplies the final
// quit callback; background shutdown keeps message dispatch alive until ready.
class AppShell final : private juce::Timer
{
public:
    explicit AppShell (std::function<void()> onReadyToQuit);
    ~AppShell() override;
    void open();
    void requestQuit();
    bool hasTaskbarButtons() const;

private:
    class MainWindow;
    class TrayIcon;
    void timerCallback() override;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<TrayIcon> trayIcon;
    std::function<void()> readyToQuit;
    bool closing = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AppShell)
};
