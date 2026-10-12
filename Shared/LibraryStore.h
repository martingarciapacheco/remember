#pragma once
#include <JuceHeader.h>
#include <functional>

// All operations here belong on control/background threads, never an audio callback.
namespace LibraryStore
{
enum class OwnerState { Active, Orphaned, Unknown };
// A unique session token backed by a live kernel object on Windows. No
// filesystem, process-ID guessing or thread-affine mutex ownership is needed.
class OwnerLease
{
public:
    OwnerLease();
    ~OwnerLease();
    bool valid() const;
    juce::var proof() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    JUCE_DECLARE_NON_COPYABLE (OwnerLease)
};
OwnerState ownerState (const juce::var& proof);

using Mutation = std::function<juce::Result (juce::DynamicObject&)>;
juce::var readJson (const juce::File&);
// Idempotent across concurrent creators, including parents shared by entries.
// Never call from audio: directory checks/creation are filesystem operations.
juce::Result ensureDirectory (const juce::File&);
juce::Result writeJsonAtomically (const juce::File&, const juce::var&);
juce::Result updateJson (const juce::File&, const Mutation&, bool allowCreate = false);
juce::Result patchJson (const juce::File&, const juce::var& patch, bool allowCreate = false);
// Explicit configuration recovery on a worker. Preserve the exact original
// bytes before replacement; reject a changed snapshot under the writer lock.
juce::Result rebuildSettingsJson (const juce::File&, const juce::var& patch,
                                  const juce::String& expectedFingerprint, juce::File& originalBackup);
juce::File dataDirectory();
// Fixture bootstrap only: call before constructing settings, loggers or workers.
// Production uses the normal application data directory. External native
// validators may opt into a marked isolated REMEMBER_VALIDATION_PROFILE.
void setDataDirectoryForTests (const juce::File&);
juce::File configFile();
struct StorageUsage
{
    juce::int64 bytes = 0, pcmBytes = 0, previewBytes = 0, freeBytes = -1;
    bool cancelled = false;
};
// Includes hidden revision/history/pending folders. Call on a worker only.
StorageUsage measureStorage (const juce::File& root, const std::function<bool()>& cancelled = {});
juce::File audioFile (const juce::File& entryFolder, const juce::var& metadata, bool pcm);
juce::File audioFile (const juce::File& entryFolder, bool pcm);
bool isDeleted (const juce::File& entryFolder);
juce::Result deleteEntry (const juce::File& previewsRoot, const juce::File& entryFolder);
juce::Result restoreEntry (const juce::File& previewsRoot, const juce::File& trashFolder);
juce::Result purgeExpiredTrash (const juce::File& previewsRoot, juce::Time now = juce::Time::getCurrentTime());
juce::Result renameEntryWithoutReplacement (const juce::File& source, const juce::File& target);

struct AudioRevision
{
    juce::String id { juce::Uuid().toString() };
    juce::File directory, ogg, pcm;
    explicit AudioRevision (const juce::File& entryFolder);
    ~AudioRevision();
    AudioRevision (const AudioRevision&) = delete;
    AudioRevision& operator= (const AudioRevision&) = delete;
    // Uncommitted files are deliberately invisible to scanners/readers.
    juce::Result prepare();
    void discard() const;
    mutable bool published = false;
    // Set by successful commit; callers publish it through their own thread
    // handoff. This is the checksum already calculated for the audio receipt.
    mutable juce::String pcmFingerprint;
    std::unique_ptr<OwnerLease> owner;
};

// Optimistic audio revision check prevents a stale merge replacing a newer tail.
// Only metadata.json is replaced at commit: both audio files are immutable first.
enum class CommitStage { AudioVerified, ReceiptWritten, BackupWritten, ManifestWritten, RevisionMarked };
using CommitObserver = std::function<void (CommitStage)>;
juce::Result commitAudio (const juce::File& entryFolder, const AudioRevision&,
                          const juce::var& metadataPatch, const juce::String& expectedRevision,
                          const std::function<bool()>& cancelled = {}, const CommitObserver& observer = {});
// Integrity repair preserves current organisation/identity when available.
// Prepared but unpublished receipts are considered only if no published
// audio can be recovered and their writer is proven gone. Respects tombstones.
struct RecoveryChoice
{
    juce::String revision, receiptFingerprint, recordedAt, key;
    juce::File previewOgg;
    double durationSeconds = 0, bpm = 0;
    int channels = 0;
};
struct RecoveryResult
{
    juce::Result result = juce::Result::ok();
    bool repaired = false;
    juce::String revision;
    juce::String pcmFingerprint; // From the receipt that verifyAudio checked.
    juce::Array<RecoveryChoice> choices; // Verified alternatives; never auto-pick an ambiguous draft.
    juce::String selectionToken; // Frozen manifest presence/content, including a missing manifest.
};
RecoveryResult recoverEntry (const juce::File& entryFolder, const std::function<bool()>& cancelled = {});
// Explicit choice from a failed ambiguous recovery. Rechecks the frozen
// manifest/receipt and current owner; it cannot replace a now-valid manifest.
RecoveryResult recoverEntryRevision (const juce::File& entryFolder, const RecoveryChoice&,
                                     const juce::String& selectionToken, bool allowUnknownOwner = false,
                                     const std::function<bool()>& cancelled = {});
juce::Result claimPending (const juce::File& libraryRoot, const juce::File& entryFolder, const OwnerLease&);
juce::Result discardOwnedPending (const juce::File& libraryRoot, const juce::File& entryFolder, const OwnerLease&);
OwnerState pendingOwnerState (const juce::File& entryFolder, const juce::var& metadata);
enum class PromotionStage { Marked, Moved, Completed };
using PromotionObserver = std::function<void (PromotionStage)>;
// Explicit keep/association by the live writer. Requires the exact owner token
// and same UUID; cannot steal another session or replace a permanent entry.
juce::Result promoteOwnedPending (const juce::File& root, const juce::File& pending,
                                  const OwnerLease&, const juce::var& identityPatch,
                                  const PromotionObserver& observer = {});
juce::Result recoverPending (const juce::File& libraryRoot, const juce::File& entryFolder,
                             bool allowUnknownOwner = false, const PromotionObserver& observer = {});
// Completes a previously authorised promotion interrupted after the rename.
juce::Result finishPendingRecovery (const juce::File& libraryRoot, const juce::File& entryFolder);
void pruneAudioRevisions (const juce::File& entryFolder);
}
