#pragma once
#include <JuceHeader.h>
#include "../../Shared/RememberLookAndFeel.h"
#if JUCE_WINDOWS
 #include <windows.h>
 #include <commctrl.h>
 #include <shobjidl.h>
 #include <array>

// Owns only our subclass and COM/icon resources; JUCE retains its native frame.
class WindowsTaskbarTransport final : private juce::Timer
{
public:
    struct State { bool playing = false, canPlay = false, hasQueue = false; };
    WindowsTaskbarTransport (HWND window, std::function<State()> readState, std::function<void(int)> command)
        : hwnd (window), state (std::move (readState)), action (std::move (command))
    {
        hooked = SetWindowSubclass (hwnd, procedure, reinterpret_cast<UINT_PTR> (this),
                                   reinterpret_cast<DWORD_PTR> (this)) != FALSE;
        if (! hooked) return;
        apartment = CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED (CoCreateInstance (CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS (&taskbar)))) return;
        if (FAILED (taskbar->HrInit())) return;
        startTimer (250);
    }
    ~WindowsTaskbarTransport() override
    {
        stopTimer();
        if (hooked && IsWindow (hwnd)) RemoveWindowSubclass (hwnd, procedure, reinterpret_cast<UINT_PTR> (this));
        for (auto icon : icons) if (icon != nullptr) DestroyIcon (icon);
        if (taskbar != nullptr) taskbar->Release();
        if (SUCCEEDED (apartment)) CoUninitialize();
    }
    static constexpr UINT previousId = 0x5240, playId = 0x5241, nextId = 0x5242;
    bool hasButtons() const noexcept { return added; }
private:
    static UINT createdMessage() { static const auto message = RegisterWindowMessageW (L"TaskbarButtonCreated"); return message; }
    static LRESULT CALLBACK procedure (HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                       UINT_PTR subclass, DWORD_PTR reference)
    {
        auto& self = *reinterpret_cast<WindowsTaskbarTransport*> (reference);
        if (message == createdMessage())
        {
            self.added = false; self.ready = true;
            self.timerCallback();
        }
        else if (message == WM_APPCOMMAND)
        {
            const auto command = GET_APPCOMMAND_LPARAM (lParam);
            int action = -1;
            if (command == APPCOMMAND_MEDIA_PREVIOUSTRACK) action = 0;
            else if (command == APPCOMMAND_MEDIA_PLAY_PAUSE) action = 1;
            else if (command == APPCOMMAND_MEDIA_NEXTTRACK) action = 2;
            else if (command == APPCOMMAND_MEDIA_PLAY) action = 3;
            else if (command == APPCOMMAND_MEDIA_PAUSE) action = 4;
            else if (command == APPCOMMAND_MEDIA_STOP) action = 5;
            if (action >= 0) { self.action (action); return TRUE; }
        }
        else if (message == WM_COMMAND && HIWORD (wParam) == THBN_CLICKED)
        {
            const auto id = LOWORD (wParam);
            if (id >= previousId && id <= nextId)
            {
                self.action ((int) (id - previousId));
                return 0;
            }
        }
        else if (message == WM_NCDESTROY)
        {
            self.stopTimer(); self.hooked = false;
            RemoveWindowSubclass (window, procedure, subclass);
        }
        return DefSubclassProc (window, message, wParam, lParam);
    }
    static HICON makeIcon (const juce::String& kind, int size, juce::Colour colour)
    {
        juce::Image image (juce::Image::ARGB, size, size, true, juce::SoftwareImageType{});
        juce::Graphics g (image);
        g.setColour (colour);
        g.addTransform (juce::AffineTransform::scale ((float) size / 16.0f));
        RememberUI::icon (g, kind, { 0, 0, 16, 16 });
        BITMAPINFO info {};
        info.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
        info.bmiHeader.biWidth = size; info.bmiHeader.biHeight = -size;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        void* pixels = nullptr;
        const auto bitmap = CreateDIBSection (nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (bitmap == nullptr) return nullptr;
        auto* output = static_cast<juce::uint32*> (pixels);
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
                output[y * size + x] = image.getPixelAt (x, y).getPixelARGB().getNativeARGB();
        const auto mask = CreateBitmap (size, size, 1, 1, nullptr);
        ICONINFO iconInfo {}; iconInfo.fIcon = TRUE; iconInfo.hbmColor = bitmap; iconInfo.hbmMask = mask;
        const auto result = mask != nullptr ? CreateIconIndirect (&iconInfo) : nullptr;
        if (mask != nullptr) DeleteObject (mask);
        DeleteObject (bitmap);
        return result;
    }
    void timerCallback() override
    {
        if (! ready || taskbar == nullptr || ! IsWindow (hwnd)) return;
        const auto value = state();
        const auto colour = RememberUI::colour (Pal::Accent);
        const auto size = GetSystemMetrics (SM_CXSMICON);
        if (icons[0] == nullptr || size != iconSize || colour != iconColour)
        {
            for (auto& icon : icons) { if (icon != nullptr) DestroyIcon (icon); icon = nullptr; }
            iconSize = size; iconColour = colour;
            const char* kinds[] = { "previous", "play", "pause", "next" };
            for (size_t i = 0; i < icons.size(); ++i) icons[i] = makeIcon (kinds[i], size, colour);
            dirty = true;
        }
        if (added && ! dirty && value.playing == last.playing && value.canPlay == last.canPlay
            && value.hasQueue == last.hasQueue) return;
        THUMBBUTTON buttons[3] {};
        const wchar_t* tips[] = { L"Previous", value.playing ? L"Pause" : L"Play", L"Next" };
        for (int i = 0; i < 3; ++i)
        {
            buttons[i].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
            buttons[i].iId = previousId + (UINT) i;
            buttons[i].hIcon = icons[i == 0 ? 0 : i == 2 ? 3 : value.playing ? 2 : 1];
            wcscpy_s (buttons[i].szTip, tips[i]);
            buttons[i].dwFlags = (i == 1 ? value.canPlay : value.hasQueue) ? THBF_ENABLED : THBF_DISABLED;
        }
        const auto result = added ? taskbar->ThumbBarUpdateButtons (hwnd, 3, buttons)
                                  : taskbar->ThumbBarAddButtons (hwnd, 3, buttons);
        if (SUCCEEDED (result)) { added = true; dirty = false; last = value; }
    }
    HWND hwnd = nullptr;
    ITaskbarList3* taskbar = nullptr;
    HRESULT apartment = E_FAIL;
    bool hooked = false, ready = false, added = false, dirty = true;
    int iconSize = 0;
    juce::Colour iconColour;
    std::array<HICON, 4> icons {};
    State last;
    std::function<State()> state;
    std::function<void(int)> action;
};
#endif
