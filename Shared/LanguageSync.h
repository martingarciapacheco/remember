#pragma once
#include "RememberLanguage.h"
#include "LibraryStore.h"
#include "BackgroundRetirement.h"

namespace RememberUI
{
// One bounded, UI-owned reader per module. Neither the host's locale nor audio
// state is changed. A malformed/unavailable config keeps the last good language.
class LanguageSync final : private juce::Timer
{
    struct Publication { std::atomic<int> value { -1 }; };
    class Reader final : public juce::Thread, public BackgroundRetirement::Job
    {
    public:
        explicit Reader (std::shared_ptr<Publication> p) : juce::Thread ("RememberLanguage"), publication (std::move (p)) {}
        bool start()
        {
            if (! module.valid() || ! startThread()) return false;
            completion.capture ((uintptr_t) getThreadId()); begin.signal(); return true;
        }
        void requestStop() override { signalThreadShouldExit(); notify(); }
        bool readyToRelease() const override { return completion.finished(); }
        void run() override
        {
            begin.wait();
            const auto file = LibraryStore::configFile();
            while (! threadShouldExit())
            {
                if (! file.exists()) publication->value.store ((int) Language::English);
                else if (file.getSize() <= 1024 * 1024)
                {
                    const auto config = juce::JSON::parse (file.loadFileAsString());
                    if (config.isObject()) publication->value.store ((int) languageFromConfig (config));
                }
                wait (750);
            }
        }
    private:
        BackgroundRetirement::ModuleReference module;
        BackgroundRetirement::NativeCompletion completion;
        juce::WaitableEvent begin;
        std::shared_ptr<Publication> publication;
    };
public:
    LanguageSync() : publication (std::make_shared<Publication>()), reader (std::make_shared<Reader> (publication))
    { if (reader->start()) startTimer (100); }
    ~LanguageSync() override
    {
        stopTimer(); reader->requestStop();
        BackgroundRetirement::retire (reader);
    }
private:
    void timerCallback() override
    {
        const auto value = publication->value.load();
        if (value < 0 || language.load() == (Language) value) return;
        setLanguage ((Language) value);
    }
    std::shared_ptr<Publication> publication;
    std::shared_ptr<Reader> reader;
};
}
