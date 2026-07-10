#include "build_registry.h"

#include <array>

#include <cameraunlock/memory/pe_fingerprint.h>

#include "logging.h"

namespace Subnautica2HeadTracking::builds
{
    // Forward declarations of every known profile - one extern per build.
    //
    // Never-delete policy: when a game patch breaks the current Steam or GDK
    // build, the dev derives new RVAs and ADDS a NEW profile here without
    // removing the old one. Users on the un-patched build still match their
    // old profile by PE fingerprint; users on the new build match the new
    // one. Both work simultaneously. The naming convention is
    // `kStoreProfile_yyyymmdd` where the date is the build's release date
    // (or close approximation - the PE fingerprint is the authoritative key,
    // so the date is just for human readability).
    extern const BuildProfile kSteamProfile_20260710;
    extern const BuildProfile kSteamProfile_20260601;
    extern const BuildProfile kSteamProfile_20260522;
    extern const BuildProfile kGdkProfile_20260602;
    extern const BuildProfile kGdkProfile_20260524;

    namespace
    {
        // Registry order matters only for diagnostics: the first entry is the
        // "primary" profile used to label HostNewer/HostOlder when no profile
        // matches. List newest-first within each store so the most recent
        // build wins the diagnostic label, and Steam profiles before GDK
        // ones because Steam is the more common install. Add new entries to
        // the TOP of this array (after the diagnostic primary).
        constexpr std::array<const BuildProfile*, 5> kKnownProfiles = {
            &kSteamProfile_20260710,
            &kSteamProfile_20260601,
            &kSteamProfile_20260522,
            &kGdkProfile_20260602,
            &kGdkProfile_20260524,
        };

        const BuildProfile* g_active = nullptr;

        // A profile is "complete" iff its hook target RVA is non-zero. This
        // lets us register a placeholder profile (correct fingerprint, RVAs
        // still TBD) without risking accidental activation - the mod stays
        // dormant on that build until discovery fills the values in.
        bool ProfileIsComplete(const BuildProfile* p)
        {
            return p && p->Offsets.kGetPlayerViewPointRva != 0;
        }
    }

    MatchResult SelectProfile(HMODULE host)
    {
        PeFingerprint running{};
        if (!cameraunlock::memory::ReadPeFingerprint(host, running)) {
            Log::Line("build-check: failed to read PE header from host module");
            return MatchResult::ReadFailed;
        }

        Log::Line("build-check: running  ts=0x%08x size=0x%08x csum=0x%08x",
            running.TimeDateStamp, running.SizeOfImage, running.CheckSum);

        for (const BuildProfile* p : kKnownProfiles) {
            const bool complete = ProfileIsComplete(p);
            Log::Line("build-check: profile=%s ts=0x%08x size=0x%08x csum=0x%08x%s",
                p->Name, p->Fingerprint.TimeDateStamp,
                p->Fingerprint.SizeOfImage, p->Fingerprint.CheckSum,
                complete ? "" : " (incomplete - offsets TBD)");
            if (running.Matches(p->Fingerprint)) {
                if (!complete) {
                    Log::Line("build-check: fingerprint matches %s but its offsets are not yet derived - staying dormant", p->Name);
                    return MatchResult::HostDiffers;
                }
                g_active = p;
                Log::Line("build-check: matched profile %s", p->Name);
                return MatchResult::Matched;
            }
        }

        // No match. Classify against the primary profile so the log explains
        // direction ("patched newer", "older", or "tampered").
        switch (cameraunlock::memory::ClassifyMismatch(
                    running, kKnownProfiles.front()->Fingerprint)) {
            case cameraunlock::memory::FingerprintMismatch::Newer:
                return MatchResult::HostNewer;
            case cameraunlock::memory::FingerprintMismatch::Older:
                return MatchResult::HostOlder;
            case cameraunlock::memory::FingerprintMismatch::Differs:
            default:
                return MatchResult::HostDiffers;
        }
    }

    const BuildProfile& ActiveProfile()
    {
        return *g_active;
    }

    bool HasActiveProfile()
    {
        return g_active != nullptr;
    }
}
