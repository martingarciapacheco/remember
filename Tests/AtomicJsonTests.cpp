#include "AtomicJsonTests.h"
#include "../Shared/LibraryStore.h"
#include "../Shared/AtomicJsonWriter.h"
#include "../Shared/ProjectIdentity.h"
#include <iostream>
#include <stdexcept>
#if JUCE_WINDOWS
 #include <windows.h>
 #include <aclapi.h>
#endif

namespace
{
void check (bool value, const char* reason) { if (! value) throw std::runtime_error (reason); }
juce::var json (const juce::String& name)
{
    juce::var value (new juce::DynamicObject());
    value.getDynamicObject()->setProperty ("projectName", name);
    value.getDynamicObject()->setProperty ("futureField", "preserved");
    return value;
}
void checkNoTemporaries (const juce::File& folder)
{
    for (const auto& file : folder.findChildFiles (juce::File::findFiles, false, ".r?-*.tmp"))
        throw std::runtime_error ("JSON temporary leaked: " + file.getFullPathName().toStdString());
}
#if JUCE_WINDOWS
// Only fixtures beneath this executable's generated working root may be
// changed. Restore the exact original DACL and inheritance state on all exits.
class WriteAccessGate
{
public:
    WriteAccessGate (juce::File file, const juce::File& root, DWORD denied) : target (std::move (file))
    {
        check (root.isAChildOf (juce::File::getCurrentWorkingDirectory()) && target.isAChildOf (root),
               "Write permission fixture is outside its temporary root");
        DWORD needed = 0;
        GetFileSecurityW (target.getFullPathName().toWideCharPointer(), DACL_SECURITY_INFORMATION, nullptr, 0, &needed);
        check (needed != 0, "Cannot size write fixture security");
        original.resize (needed);
        check (GetFileSecurityW (target.getFullPathName().toWideCharPointer(), DACL_SECURITY_INFORMATION,
                                original.data(), needed, &needed) != FALSE, "Cannot preserve write fixture security");
        SECURITY_DESCRIPTOR_CONTROL control {}; DWORD revision = 0;
        check (GetSecurityDescriptorControl (original.data(), &control, &revision) != FALSE, "Cannot preserve write fixture inheritance");
        restoreInformation = DACL_SECURITY_INFORMATION | ((control & SE_DACL_PROTECTED) ? PROTECTED_DACL_SECURITY_INFORMATION
                                                                                      : UNPROTECTED_DACL_SECURITY_INFORMATION);
        PACL previous = nullptr; BOOL present = FALSE, defaulted = FALSE;
        check (GetSecurityDescriptorDacl (original.data(), &present, &previous, &defaulted) != FALSE && present,
               "Write fixture has no readable DACL");
        HANDLE token = nullptr;
        check (OpenProcessToken (GetCurrentProcess(), TOKEN_QUERY, &token) != FALSE, "Cannot obtain write fixture user");
        struct Close { HANDLE value; ~Close() { CloseHandle (value); } } close { token };
        GetTokenInformation (token, TokenUser, nullptr, 0, &needed);
        std::vector<unsigned char> user (needed);
        check (GetTokenInformation (token, TokenUser, user.data(), needed, &needed) != FALSE, "Cannot read write fixture SID");
        EXPLICIT_ACCESS_W denial {};
        denial.grfAccessPermissions = denied; denial.grfAccessMode = DENY_ACCESS; denial.grfInheritance = NO_INHERITANCE;
        denial.Trustee.TrusteeForm = TRUSTEE_IS_SID; denial.Trustee.TrusteeType = TRUSTEE_IS_USER;
        denial.Trustee.ptstrName = reinterpret_cast<LPWSTR> (reinterpret_cast<TOKEN_USER*> (user.data())->User.Sid);
        PACL altered = nullptr;
        check (SetEntriesInAclW (1, &denial, previous, &altered) == ERROR_SUCCESS, "Cannot construct denied-write DACL");
        struct Free { PACL value; ~Free() { LocalFree (value); } } free { altered };
        SECURITY_DESCRIPTOR descriptor {};
        check (InitializeSecurityDescriptor (&descriptor, SECURITY_DESCRIPTOR_REVISION) != FALSE
               && SetSecurityDescriptorDacl (&descriptor, TRUE, altered, FALSE) != FALSE
               && SetFileSecurityW (target.getFullPathName().toWideCharPointer(),
                                    DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, &descriptor) != FALSE,
               "Cannot deny writes on fixture");
        changed = true;
    }
    ~WriteAccessGate() { if (! restore()) std::cerr << "Cannot restore fixture DACL: " << target.getFullPathName() << '\n'; }
    bool restore()
    {
        if (! changed) return true;
        if (! SetFileSecurityW (target.getFullPathName().toWideCharPointer(), restoreInformation, original.data())) return false;
        changed = false; return true;
    }
private:
    juce::File target;
    std::vector<unsigned char> original;
    SECURITY_INFORMATION restoreInformation = DACL_SECURITY_INFORMATION;
    bool changed = false;
};
juce::String extendedPath (const juce::File& file) { return "\\\\?\\" + file.getFullPathName(); }
juce::String readExtended (const juce::File& file)
{
    const auto handle = CreateFileW (extendedPath (file).toWideCharPointer(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check (handle != INVALID_HANDLE_VALUE, "Cannot read long-path fixture through native extended path");
    struct Close { HANDLE value; ~Close() { CloseHandle (value); } } close { handle };
    char bytes[4096] {}; DWORD read = 0;
    check (ReadFile (handle, bytes, sizeof (bytes), &read, nullptr) != FALSE && read < sizeof (bytes), "Cannot read long-path fixture");
    return juce::String::fromUTF8 (bytes, (int) read);
}
#endif
}

void testAtomicJson (const juce::File& fixtureRoot)
{
    check (fixtureRoot.isAChildOf (juce::File::getCurrentWorkingDirectory()), "Atomic JSON fixture root is not isolated");
    const auto root = fixtureRoot.getChildFile ("atomic-json");
    check (LibraryStore::ensureDirectory (root).wasOk(), "Cannot create atomic JSON fixture");
    const auto oldText = juce::JSON::toString (json (juce::String::fromUTF8 ("Anterior: canción 🎹")));
    const auto next = json (juce::String::fromUTF8 ("Nueva: grabación 🎵"));
    const auto target = root.getChildFile ("config.json");
    for (bool existing : { true, false })
        for (int stage = 0; stage < 3; ++stage)
        {
            check (target.deleteFile(), "Cannot reset injected JSON fixture");
            if (existing) check (target.replaceWithText (oldText), "Cannot seed original JSON bytes");
            const auto oldId = existing ? ProjectIdentity::fileId (target) : juce::String{};
            juce::File temporary;
            const auto failed = LibraryStore::detail::writeJsonWithTemporaryWriter (target, next,
                [&] (const juce::File& file, const juce::String& text, bool& created)
                {
                    temporary = file;
                    juce::FileOutputStream stream (file, 0);
                    if (! stream.openedOk()) return stream.getStatus();
                    created = true;
                    // Exercise cleanup after a real partial/full temporary,
                    // then inject the backend failure before publication.
                    const auto count = stage == 2 ? text.getNumBytesAsUTF8() : (size_t) 11;
                    check (stream.write (text.toRawUTF8(), count), "Cannot write injected temporary prefix");
                    stream.flush(); check (stream.getStatus().wasOk(), "Injected fixture could not flush its bytes");
                    return juce::Result::fail (stage == 0 ? "Cannot write JSON temporary: simulated Win32 112 (disk full). Free space, then retry."
                        : stage == 1 ? "Short JSON write: simulated 11 bytes. Check disk access, then retry."
                                     : "Cannot flush JSON temporary: simulated device error. Restore disk access, then retry.");
                });
            check (failed.failed() && failed.getErrorMessage().contains (target.getFullPathName())
                   && failed.getErrorMessage().contains ("retry"), "Injected write failure lost target/path/action");
            check (existing ? target.loadFileAsString() == oldText && ProjectIdentity::fileId (target) == oldId : ! target.exists(),
                   "Failed partial write/flush published or replaced JSON");
            check (! temporary.exists(), "Failed JSON write leaked its incomplete temporary");
        }
    check (LibraryStore::writeJsonAtomically (target, next).wasOk()
           && target.loadFileAsString() == juce::JSON::toString (next), "Real writer did not preserve exact Unicode JSON bytes");
    auto second = next.clone(); second.getDynamicObject()->setProperty ("payload", juce::String::repeatedString ("á🎵", 40000));
    check (LibraryStore::writeJsonAtomically (target, second).wasOk()
           && target.loadFileAsString() == juce::JSON::toString (second), "Chunked native JSON write truncated UTF8 bytes");
    checkNoTemporaries (root);
    const auto collision = root.getChildFile ("blocked-parent");
    check (collision.replaceWithText ("keep parent collision"), "Cannot seed parent collision");
    const auto parentFailure = LibraryStore::writeJsonAtomically (collision.getChildFile ("config.json"), next);
    check (parentFailure.failed() && parentFailure.getErrorMessage().contains ("Cannot create JSON parent")
           && parentFailure.getErrorMessage().contains (collision.getFullPathName())
           && collision.loadFileAsString() == "keep parent collision", "Parent creation failure lost context or changed the collision");
    std::cout << "PASS: six injected partial write/short write/flush failures preserve exact prior bytes and file identity or absent target; temporary cleanup, Unicode and chunked native writes\n";

   #if JUCE_WINDOWS
    check (target.replaceWithText (oldText), "Cannot seed native write fixture");
    {
        WriteAccessGate gate (root, fixtureRoot, FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY);
        const auto failed = LibraryStore::writeJsonAtomically (target, next);
        check (failed.failed() && failed.getErrorMessage().contains ("Cannot create JSON temporary")
               && failed.getErrorMessage().contains (target.getFullPathName()) && failed.getErrorMessage().contains ("Win32 5")
               && failed.getErrorMessage().contains ("permissions") && target.loadFileAsString() == oldText,
               "Native denied temporary creation lost actionable error or changed the original");
        const auto missingParent = LibraryStore::writeJsonAtomically (root.getChildFile ("new-parent/config.json"), next);
        check (missingParent.failed() && missingParent.getErrorMessage().contains ("Cannot create JSON parent")
               && ! root.getChildFile ("new-parent").exists(), "Native denied directory creation was ignored");
        check (gate.restore(), "Cannot restore parent write permissions");
    }
    checkNoTemporaries (root);
    const auto metadata = root.getChildFile ("metadata.json"), backup = root.getChildFile (".metadata-backup.json");
    check (metadata.replaceWithText (oldText) && backup.replaceWithText (oldText), "Cannot seed denied backup fixture");
    {
        WriteAccessGate parentGate (root, fixtureRoot, FILE_DELETE_CHILD);
        WriteAccessGate gate (backup, fixtureRoot, FILE_WRITE_DATA | DELETE);
        const auto failed = LibraryStore::patchJson (metadata, next);
        std::cout << "DENIED_BACKUP result=" << failed.getErrorMessage() << " metadataPreserved="
                  << (metadata.loadFileAsString() == oldText) << " backupPreserved=" << (backup.loadFileAsString() == oldText) << '\n';
        check (failed.failed() && failed.getErrorMessage().contains (backup.getFullPathName())
               && failed.getErrorMessage().contains ("Cannot commit JSON") && failed.getErrorMessage().contains ("Win32 5")
               && metadata.loadFileAsString() == oldText && backup.loadFileAsString() == oldText,
               "Denied native backup replacement changed metadata or hid the failing phase/path");
        check (gate.restore(), "Cannot restore backup DACL");
        check (parentGate.restore(), "Cannot restore backup parent DACL");
    }
    check (LibraryStore::patchJson (metadata, next).wasOk(), "Retry after restoring backup permissions failed");
    {
        const auto prior = target.loadFileAsString();
        const auto handle = CreateFileW (target.getFullPathName().toWideCharPointer(), GENERIC_READ,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check (handle != INVALID_HANDLE_VALUE, "Cannot hold native JSON reader");
        struct Close { HANDLE value; ~Close() { CloseHandle (value); } } close { handle };
        const auto failed = LibraryStore::writeJsonAtomically (target, next);
        check (failed.failed() && failed.getErrorMessage().contains ("Cannot commit JSON")
               && failed.getErrorMessage().contains ("Win32 32") && failed.getErrorMessage().contains ("Release it")
               && target.loadFileAsString() == prior, "Native sharing failure replaced JSON or lost its release/retry action");
    }
    check (LibraryStore::writeJsonAtomically (target, next).wasOk(), "JSON retry after releasing native reader failed");
    {
        struct RestoreReadOnly { juce::File file; ~RestoreReadOnly() { file.setReadOnly (false); } } restore { target };
        check (target.setReadOnly (true), "Cannot protect native JSON target");
        const auto failed = LibraryStore::writeJsonAtomically (target, second);
        check (failed.failed() && failed.getErrorMessage().contains ("Win32 5")
               && failed.getErrorMessage().contains ("read-only") && target.loadFileAsString() == juce::JSON::toString (next),
               "Native read-only replacement changed JSON or lost protection diagnostics");
    }
    checkNoTemporaries (root);
    std::cout << "PASS: native denied parent/temp/backup writes, restored original DACL, read-only and sharing errors preserve JSON; retries succeed\n";
    {
        auto parent = root.getChildFile ("long-path");
        check (parent.createDirectory().wasOk(), "Cannot create long-path base");
        while (parent.getFullPathName().length() < 200)
        {
            parent = parent.getChildFile ("path-part-" + juce::String::repeatedString ("p", 17));
            check (parent.createDirectory().wasOk(), "Cannot create bounded long-path parent");
        }
        const auto nameLength = 272 - parent.getFullPathName().length() - 1;
        check (nameLength > 12 && nameLength < 128, "Long-path fixture component size is invalid");
        const auto longTarget = parent.getChildFile (juce::String::repeatedString ("n", nameLength - 5) + ".json");
        check (longTarget.isAChildOf (fixtureRoot), "Long-path target escaped its fixture");
        struct CleanupLongFile
        {
            juce::File file;
            ~CleanupLongFile() { DeleteFileW (extendedPath (file).toWideCharPointer()); }
        } cleanup { longTarget };
        const auto handle = CreateFileW (extendedPath (longTarget).toWideCharPointer(), GENERIC_WRITE, 0, nullptr,
                                        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        check (handle != INVALID_HANDLE_VALUE, "Cannot seed native extended-path original");
        DWORD written = 0;
        const bool seeded = WriteFile (handle, oldText.toRawUTF8(), (DWORD) oldText.getNumBytesAsUTF8(), &written, nullptr) != FALSE;
        CloseHandle (handle);
        check (seeded && written == oldText.getNumBytesAsUTF8(), "Cannot write long-path original bytes");
        const auto result = LibraryStore::writeJsonAtomically (longTarget, next);
        if (result.failed())
            check (readExtended (longTarget) == oldText && result.getErrorMessage().contains (longTarget.getFullPathName())
                   && result.getErrorMessage().contains ("shorter"), "Long-path failure lost original JSON or actionable shortening instruction");
        else
            check (readExtended (longTarget) == juce::JSON::toString (next), "Enabled long-path writer published incomplete JSON");
        checkNoTemporaries (parent);
        std::cout << "LONG_JSON targetCharacters=" << longTarget.getFullPathName().length() << " result="
                  << (result.wasOk() ? "supported by current process" : result.getErrorMessage()) << '\n';
    }
   #endif
}
