#include "logging.h"
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <windows.h>

namespace Subnautica2HeadTracking::Log
{
    namespace
    {
        // HANDLE rather than FILE* so EmergencyLine can write from inside an
        // exception handler without going through the CRT (locks, heap, TLS).
        // FILE_SHARE_READ matches the old _SH_DENYWR contract: external tools
        // can tail the log while we hold write access.
        HANDLE g_handle = INVALID_HANDLE_VALUE;
        std::mutex g_mutex;
        std::atomic<bool> g_open{false};

        void WriteTimestampedLocked(const char* msg, size_t len)
        {
            if (g_handle == INVALID_HANDLE_VALUE) return;
            SYSTEMTIME st;
            GetLocalTime(&st);
            char prefix[32];
            const int n = std::snprintf(prefix, sizeof(prefix),
                "[%02d:%02d:%02d.%03d] ",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
            DWORD written = 0;
            if (n > 0) WriteFile(g_handle, prefix, static_cast<DWORD>(n), &written, nullptr);
            WriteFile(g_handle, msg, static_cast<DWORD>(len), &written, nullptr);
            WriteFile(g_handle, "\r\n", 2, &written, nullptr);
            FlushFileBuffers(g_handle);
        }
    }

    void Open(const std::wstring& filename)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_handle != INVALID_HANDLE_VALUE) return;
        g_handle = CreateFileW(
            filename.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
            nullptr);
        g_open.store(g_handle != INVALID_HANDLE_VALUE,
                     std::memory_order_release);
    }

    void Close()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(g_handle);
            g_handle = INVALID_HANDLE_VALUE;
        }
        g_open.store(false, std::memory_order_release);
    }

    void Line(const char* fmt, ...)
    {
        if (!g_open.load(std::memory_order_acquire)) return;
        char buf[2048];
        va_list args;
        va_start(args, fmt);
        const int n = std::vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        if (n < 0) return;
        const size_t len = (static_cast<size_t>(n) >= sizeof(buf))
            ? sizeof(buf) - 1
            : static_cast<size_t>(n);
        std::lock_guard<std::mutex> lock(g_mutex);
        WriteTimestampedLocked(buf, len);
    }

    void EmergencyLine(const char* fmt, ...)
    {
        // Read the handle through the atomic flag - no mutex, because a
        // faulted thread may already hold it. Worst case during a Close()
        // race is a write to a closed handle, which WriteFile rejects
        // cleanly with no crash. Acceptable for crash-reporting.
        if (!g_open.load(std::memory_order_acquire)) return;
        const HANDLE h = g_handle;
        if (h == INVALID_HANDLE_VALUE) return;

        char buf[2048];
        va_list args;
        va_start(args, fmt);
        const int n = std::vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        if (n < 0) return;
        const size_t msg_len = (static_cast<size_t>(n) >= sizeof(buf))
            ? sizeof(buf) - 1
            : static_cast<size_t>(n);

        SYSTEMTIME st;
        GetLocalTime(&st);
        char prefix[32];
        const int p = std::snprintf(prefix, sizeof(prefix),
            "[%02d:%02d:%02d.%03d] ",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

        DWORD written = 0;
        if (p > 0) WriteFile(h, prefix, static_cast<DWORD>(p), &written, nullptr);
        WriteFile(h, buf, static_cast<DWORD>(msg_len), &written, nullptr);
        WriteFile(h, "\r\n", 2, &written, nullptr);
        FlushFileBuffers(h);
    }
}
