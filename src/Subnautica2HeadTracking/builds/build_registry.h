#pragma once
#include <windows.h>
#include "build_profile.h"

// Profile registry and selection. SelectProfile() fingerprints the host EXE
// (PE TimeDateStamp + SizeOfImage + CheckSum) against the known-profiles
// array and, on a match, records that profile as active so the rest of the
// code reads RVAs and field offsets without re-fingerprinting per call.

namespace Subnautica2HeadTracking
{
    namespace builds
    {
        // Outcome of a fingerprint match attempt. ReadFailed means we couldn't
        // even parse the PE header; the other three describe how the running
        // EXE differs from the primary (first-registered) profile so the user
        // log can hint at "patch newer/older" without surfacing all profiles.
        enum class MatchResult
        {
            Matched,        // Active profile is now set; mod can run.
            ReadFailed,     // Could not read the PE header.
            HostNewer,      // Running EXE TimeDateStamp > primary profile.
            HostOlder,      // Running EXE TimeDateStamp < primary profile.
            HostDiffers,    // Same timestamp, different size or checksum.
        };

        // Fingerprint the given module, look up a matching profile, and on
        // success install it as the active profile. Logs diagnostics either
        // way via Log::Line so the user log explains why the mod did or did
        // not engage.
        MatchResult SelectProfile(HMODULE host);

        const BuildProfile& ActiveProfile();
        bool                HasActiveProfile();
    }

    // Accessor for the active profile's offset table. Only valid once
    // SelectProfile() has returned Matched; nothing in the mod may touch game
    // memory before that. Inline so the lookup is a single load on hot paths.
    inline const OffsetTable& Offsets()
    {
        return builds::ActiveProfile().Offsets;
    }
}
