#pragma once

namespace Subnautica2HeadTracking::Crash
{
    // Installs a vectored exception handler (first-chance, filtered to fatal
    // codes that fault inside our own DLL) plus an unhandled exception filter
    // (last-chance, always logs). Both write to the existing log file via
    // Log::EmergencyLine and then return EXCEPTION_CONTINUE_SEARCH so the
    // game's / OS's normal crash flow still runs (WER dump, host's filter).
    //
    // Call once, as early as possible after Log::Open. Safe to call before
    // any hook is installed.
    void Install();
}
