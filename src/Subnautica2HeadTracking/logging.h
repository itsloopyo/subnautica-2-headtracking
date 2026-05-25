#pragma once
#include <string>

namespace Subnautica2HeadTracking::Log
{
    void Open(const std::wstring& filename);
    void Close();
    void Line(const char* fmt, ...);

    // Lock-free, exception-handler-safe write. Use ONLY from inside a
    // vectored / unhandled exception handler. Bypasses the normal mutex so
    // a thread holding the log lock when it faulted does not deadlock the
    // crash report, and uses WriteFile directly (no CRT, no heap).
    void EmergencyLine(const char* fmt, ...);
}
