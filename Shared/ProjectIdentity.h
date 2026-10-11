#pragma once
#include <JuceHeader.h>
#include <functional>
#include <vector>
#include <unordered_map>
#include <unordered_set>

// Names are presentation only. A preview belongs to a stable ID and a verified
// DAW file, never to whichever file happens to share its display name.
struct HostProject
{
    bool available = false; // false while a host window is temporarily unavailable
    juce::String name;
    juce::String path;
    juce::String daw;
};

namespace ProjectIdentity
{
    // One inventory per search generation; IDs and hashes are calculated at
    // most once per candidate. Missing/ambiguous results are cached too.
    class CandidateIndex
    {
    public:
        bool rebuild (const juce::Array<juce::File>& roots, const std::function<bool()>& cancelled = {});
        void setCancellation (std::function<bool()> value) { cancelled = std::move (value); }
        juce::String resolve (const juce::String& name, const juce::String& daw,
                              const juce::String& previousPath = {}, const juce::String& previousFileId = {},
                              const juce::String& previousFingerprint = {});
        size_t getCandidateCount() const noexcept { return candidates.size(); }
        const juce::String& getLastError() const noexcept { return lastError; }
        bool wasLastResolutionAmbiguous() const noexcept { return lastAmbiguous; }
    private:
        struct Candidate { juce::File file; juce::String id, hash; bool idRead = false, hashRead = false; juce::String extension; };
        std::vector<Candidate> candidates;
        std::unordered_map<std::string, std::vector<size_t>> names, ids, hashes;
        std::unordered_set<std::string> hashedExtensions;
        bool idsIndexed = false;
        struct Resolution { juce::String path; bool ambiguous = false; };
        std::unordered_map<std::string, Resolution> resolutions;
        bool lastAmbiguous = false;
        juce::String lastError;
        std::function<bool()> cancelled;
    };
    HostProject parseTitle (const juce::String& title, const juce::String& daw);
    HostProject detectHostProject();
    juce::String fileId (const juce::File& file);
    juce::String fingerprint (const juce::File& file, const std::function<bool()>& cancelled = {});
    juce::String resolve (const juce::String& name, const juce::String& daw,
                          const juce::Array<juce::File>& roots,
                          const juce::String& previousPath = {},
                          const juce::String& previousFileId = {},
                          const juce::String& previousFingerprint = {});
    juce::Array<juce::File> searchRoots();
}
