#pragma once

namespace Subnautica2HeadTracking
{
    void Initialize(void* hModule);

    // Full cleanup. Joins worker threads, removes hooks, releases the log.
    // Safe only when other threads in the process are still live - i.e. a
    // manual FreeLibrary (DLL_PROCESS_DETACH with lpReserved == NULL).
    void Shutdown();

    // No-cleanup variant for the DLL_PROCESS_DETACH-during-process-exit
    // path (lpReserved != NULL). Other threads have been killed by the
    // kernel without unwinding; any mutex they held is still "locked",
    // and std::thread::join on a kernel-terminated thread can hang or
    // crash. Just write a final log line and let the OS reclaim state.
    void EmergencyShutdown();
}
