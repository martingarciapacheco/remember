#include "SettingsManager.h"
#include "../../Shared/LibraryStore.h"
#include "../../Shared/BackgroundRetirement.h"
#include "../../Shared/AppearanceSettings.h"
#include "../../Shared/ZoomSettings.h"

namespace
{
std::atomic<int> settingsJobs { 0 };
constexpr int maximumSettingsJobs = 32;
struct SettingsReservation
{
    bool held = false;
    ~SettingsReservation() { if (held) settingsJobs.fetch_sub (1); }
};
void populateSettingsRequest (SettingsManager::Request& request, const juce::File& config)
{
    request.snapshot = SettingsManager::readSnapshot (config);
    request.configured = request.snapshot.previews.isDirectory();
    if (config.existsAsFile() && request.snapshot.fingerprint.isEmpty())
        request.result = juce::Result::fail ("Cannot read Remember settings: " + config.getFullPathName() + ". Restore a valid configuration and retry");
}
class SettingsTask final : public juce::Thread, public BackgroundRetirement::Job
{
public:
    using Work = std::function<void (SettingsManager::Request&)>;
    SettingsTask (std::shared_ptr<SettingsManager::Request> request, Work action, std::unique_ptr<SettingsReservation> reservation)
        : juce::Thread ("RememberPluginSettings"), publication (std::move (request)), work (std::move (action)), budget (std::move (reservation)) {}
    bool start()
    {
        if (! module.valid() || ! startThread())
        {
            publication->result = juce::Result::fail ("Cannot start the settings worker. Retry after closing other plugin editors");
            publication->complete.store (true, std::memory_order_release); return false;
        }
        completion.capture ((uintptr_t) getThreadId()); begin.signal(); return true;
    }
    // A selected output-folder edit is accepted work. Finish it after closure;
    // no UI object is needed to complete or publish the transaction.
    void requestStop() override {}
    bool readyToRelease() const override { return completion.finished() && publication->complete.load (std::memory_order_acquire); }
    void run() override
    {
        // Retain the OS thread handle before even a fast read can return.
        begin.wait();
        try { work (*publication); }
        catch (const std::exception& error) { publication->result = juce::Result::fail ("Settings: " + juce::String (error.what())); }
        catch (...) { publication->result = juce::Result::fail ("Settings worker failed. Retry the folder selection"); }
        publication->complete.store (true, std::memory_order_release);
    }
private:
    BackgroundRetirement::ModuleReference module;
    BackgroundRetirement::NativeCompletion completion;
    juce::WaitableEvent begin;
    std::shared_ptr<SettingsManager::Request> publication;
    Work work;
    std::unique_ptr<SettingsReservation> budget;
};
std::shared_ptr<SettingsManager::Request> launchSettingsTask (SettingsTask::Work work)
{
    auto request = std::make_shared<SettingsManager::Request>();
    // Repeated prepares on inaccessible storage must not create unlimited
    // retained native workers. Reserve until OS completion, including closure.
    auto reservation = std::make_unique<SettingsReservation>();
    auto active = settingsJobs.load();
    for (;;)
    {
        if (active >= maximumSettingsJobs)
        {
            request->result = juce::Result::fail ("Settings workers are busy. Restore storage access, let pending reads finish and retry");
            request->complete.store (true, std::memory_order_release);
            return request;
        }
        if (settingsJobs.compare_exchange_weak (active, active + 1)) break;
    }
    reservation->held = true;
    auto task = std::make_shared<SettingsTask> (request, std::move (work), std::move (reservation));
    if (task->start()) BackgroundRetirement::retire (task);
    return request;
}
}

SettingsManager& SettingsManager::getInstance()
{
    static SettingsManager instance;
    return instance;
}
SettingsManager::SettingsManager() : configFile (LibraryStore::configFile()) {}

SettingsManager::Snapshot SettingsManager::readSnapshot() const
{
    return readSnapshot (configFile);
}
SettingsManager::Snapshot SettingsManager::readSnapshot (const juce::File& source)
{
    // JUCE's const path conversion caches UTF-16 inside the String object.
    // Independent readers must not perform that conversion on the same File.
    // Copy the value before native I/O, also for callers of the static API.
    const auto config = source;
    Snapshot result;
    // Hash the same text that supplies the fields. This also distinguishes
    // external edits that reuse dataRevision, length and modification time.
    const auto text = config.loadFileAsString();
    const auto json = juce::JSON::parse (text);
    if (json.getDynamicObject() == nullptr) return result;
    result.revision = (juce::int64) json.getProperty ("dataRevision", 0);
    result.modifiedMs = config.getLastModificationTime().toMilliseconds();
    result.fingerprint = juce::SHA256 (text.toRawUTF8(), text.getNumBytesAsUTF8()).toHexString();
    const auto path = json.getProperty ("previewsFolder", "").toString();
    if (juce::File::isAbsolutePath (path)) result.previews = juce::File (path);
    result.seconds = juce::jlimit (15, 120, (int) json.getProperty ("recordSeconds", 60));
    result.quality = juce::jlimit (0, 10, (int) json.getProperty ("oggQuality", 4));
    result.mono = (bool) json.getProperty ("recordMono", false);
    result.darkAppearance = AppearanceSettings::isDark (json);
    result.interfaceZoom = ZoomSettings::read (json, true);
    result.capture = CaptureOptions::fromJson (json);
    return result;
}
juce::File SettingsManager::getPreviewsFolder() const { return readSnapshot().previews; }
juce::Result SettingsManager::setPreviewsFolder (const juce::File& folder)
{
    juce::var patch (new juce::DynamicObject());
    patch.getDynamicObject()->setProperty ("previewsFolder", folder.getFullPathName());
    return LibraryStore::patchJson (configFile, patch, true);
}
bool SettingsManager::isConfigured() const { return getPreviewsFolder().isDirectory(); }
int SettingsManager::readRecordSeconds() const { return readSnapshot().seconds; }
int SettingsManager::readOggQuality() const { return readSnapshot().quality; }
bool SettingsManager::readRecordMono() const { return readSnapshot().mono; }

std::shared_ptr<SettingsManager::Request> SettingsManager::readSnapshotAsync() const
{
    const auto config = configFile;
    return launchSettingsTask ([config] (Request& request)
    {
        populateSettingsRequest (request, config);
    });
}
std::shared_ptr<SettingsManager::Request> SettingsManager::readSnapshotAsync (Reader reader)
{
    return launchSettingsTask ([reader = std::move (reader)] (Request& request)
    {
        request.snapshot = reader();
        request.configured = request.snapshot.previews.isDirectory();
        if (request.snapshot.fingerprint.isEmpty())
            request.result = juce::Result::fail ("Cannot read Remember settings: " + LibraryStore::configFile().getFullPathName()
                + ". Restore a valid configuration and prepare the plugin again");
        else if (! request.configured)
            request.result = juce::Result::fail ("Output folder is unavailable: " + request.snapshot.previews.getFullPathName()
                + ". Select an available output in Settings and prepare the plugin again");
    });
}
std::shared_ptr<SettingsManager::Request> SettingsManager::setPreviewsFolderAsync (const juce::File& folder) const
{
    const auto config = configFile;
    return launchSettingsTask ([config, folder] (Request& request)
    {
        if (! folder.isDirectory())
        { request.result = juce::Result::fail ("Output folder is unavailable: " + folder.getFullPathName()); return; }
        juce::var patch (new juce::DynamicObject()); patch.getDynamicObject()->setProperty ("previewsFolder", folder.getFullPathName());
        request.result = LibraryStore::patchJson (config, patch, true);
        if (request.result.wasOk())
        {
            populateSettingsRequest (request, config);
            if (request.result.wasOk() && request.snapshot.previews != folder)
                request.result = juce::Result::fail ("Output settings changed before confirmation. Select the folder again");
        }
    });
}

std::shared_ptr<SettingsManager::Request> SettingsManager::setZoomAsync (int percent) const
{
    const auto config = configFile;
    return launchSettingsTask ([config, percent] (Request& request)
    {
        request.result = ZoomSettings::save (percent, true);
        if (request.result.wasOk()) populateSettingsRequest (request, config);
    });
}

std::shared_ptr<SettingsManager::Request> SettingsManager::setAppearanceAsync (bool dark) const
{
    const auto config = configFile;
    return launchSettingsTask ([config, dark] (Request& request)
    {
        request.result = AppearanceSettings::save (dark);
        if (request.result.wasOk()) populateSettingsRequest (request, config);
    });
}

std::shared_ptr<SettingsManager::Request> SettingsManager::setCaptureOptionsAsync (CaptureOptions options) const
{
    const auto config = configFile;
    return launchSettingsTask ([config, options] (Request& request)
    {
        auto patch = options.patch();
        // The plugin edits its manual mode locally, never the creation default.
        patch.getDynamicObject()->removeProperty ("manualCapture");
        request.result = LibraryStore::patchJson (config, patch, true);
        if (request.result.wasOk()) populateSettingsRequest (request, config);
    });
}
