#include "LibraryWatch.h"
#include "../../Shared/ProjectIdentity.h"
#include <array>
#include <cstddef>
#include <cstring>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

struct LibraryWatch::Watch
{
    juce::File directory;
    juce::String leaf;
    unsigned kind = None;
    bool recursive = false;
   #if JUCE_WINDOWS
    HANDLE handle = INVALID_HANDLE_VALUE;
    OVERLAPPED request {};
    alignas (DWORD) std::array<unsigned char, 32 * 1024> buffer {};
    bool pending = false;

    bool arm()
    {
        if (handle == INVALID_HANDLE_VALUE || request.hEvent == nullptr) return false;
        ResetEvent (request.hEvent);
        pending = ReadDirectoryChangesW (handle, buffer.data(), (DWORD) buffer.size(), recursive,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
            nullptr, &request, nullptr) != FALSE;
        if (! pending && GetLastError() == ERROR_IO_PENDING) pending = true;
        return pending;
    }
    void open()
    {
        handle = CreateFileW (directory.getFullPathName().toWideCharPointer(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (handle != INVALID_HANDLE_VALUE) request.hEvent = CreateEventW (nullptr, TRUE, FALSE, nullptr);
        arm();
    }
    unsigned takeChanges()
    {
        if (! pending || WaitForSingleObject (request.hEvent, 0) != WAIT_OBJECT_0) return None;
        DWORD bytes = 0;
        const auto succeeded = GetOverlappedResult (handle, &request, &bytes, FALSE) != FALSE;
        pending = false;
        unsigned result = None;
        // Zero bytes/failed notification means a lost inventory, requiring an
        // enumeration rather than trusting individual names from that buffer.
        if (! succeeded || bytes == 0 || bytes > buffer.size()) result = kind;
        else if (leaf.isEmpty()) result = kind;
        else
        {
            size_t offset = 0;
            constexpr auto header = offsetof (FILE_NOTIFY_INFORMATION, FileName);
            while (offset + header <= bytes)
            {
                const auto* item = reinterpret_cast<const FILE_NOTIFY_INFORMATION*> (buffer.data() + offset);
                if (item->FileNameLength % sizeof (WCHAR) != 0 || item->FileNameLength > bytes - offset - header)
                { result = kind; break; }
                const juce::String name (item->FileName, (int) (item->FileNameLength / sizeof (WCHAR)));
                if (name.equalsIgnoreCase (leaf)) result = kind;
                if (item->NextEntryOffset == 0) break;
                if (item->NextEntryOffset % alignof (DWORD) != 0 || item->NextEntryOffset < header
                    || item->NextEntryOffset > bytes - offset)
                { result = kind; break; }
                offset += item->NextEntryOffset;
            }
        }
        // Re-arm only successful requests. A removed folder can otherwise
        // generate an endless failure loop; configure/polling will retry it.
        if (succeeded) arm();
        return result;
    }
    ~Watch()
    {
        if (pending)
        {
            CancelIoEx (handle, &request);
            DWORD ignored = 0;
            GetOverlappedResult (handle, &request, &ignored, TRUE);
        }
        if (handle != INVALID_HANDLE_VALUE) CloseHandle (handle);
        if (request.hEvent != nullptr) CloseHandle (request.hEvent);
    }
   #else
    void open() {}
    unsigned takeChanges() { return None; }
    bool pending = false;
   #endif
};

LibraryWatch::LibraryWatch() = default;
LibraryWatch::~LibraryWatch() = default;
void LibraryWatch::clear() { watches.clear(); signature.clear(); complete = false; }

void LibraryWatch::configure (const juce::File& config, const juce::File& previews,
                              const juce::Array<juce::File>& projectRoots)
{
    struct Target { juce::File directory; juce::String leaf; unsigned kind; bool recursive; };
    std::vector<Target> targets;
    targets.push_back ({ config.getParentDirectory(), config.getFileName(), Library, false });
    if (previews != juce::File{})
    {
        targets.push_back ({ previews, {}, Library, true });
        if (previews.getParentDirectory() != previews)
            targets.push_back ({ previews.getParentDirectory(), previews.getFileName(), Library, false });
        for (const auto& root : projectRoots)
            targets.push_back ({ root, {}, Candidates, true });
    }
    juce::String nextSignature;
    for (const auto& target : targets)
        nextSignature += target.directory.getFullPathName() + "\n" + ProjectIdentity::fileId (target.directory)
            + "\n" + target.leaf + "\n" + juce::String ((int) target.kind) + "\n";
    if (nextSignature == signature && hasCompleteCoverage()) return;
    watches.clear();
    signature = std::move (nextSignature);
    // Bound kernel buffers/handles independently of user-supplied root count.
    // Unwatched roots still receive the polling inventory.
    constexpr size_t maxWatches = 16;
    complete = targets.size() <= maxWatches;
    for (size_t i = 0; i < targets.size() && i < maxWatches; ++i)
    {
        auto watch = std::make_unique<Watch>();
        watch->directory = targets[i].directory; watch->leaf = targets[i].leaf;
        watch->kind = targets[i].kind; watch->recursive = targets[i].recursive;
        watch->open(); complete = complete && watch->pending;
        watches.push_back (std::move (watch));
    }
}

unsigned LibraryWatch::takeChanges()
{
    unsigned result = None;
    for (auto& watch : watches) result |= watch->takeChanges();
    return result;
}

bool LibraryWatch::hasCompleteCoverage() const
{
    if (! complete) return false;
    for (const auto& watch : watches) if (! watch->pending) return false;
    return true;
}
