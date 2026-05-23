#include "logging.h"
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <share.h>
#include <windows.h>

namespace Subnautica2HeadTracking::Log
{
    static FILE* g_log = nullptr;
    static std::mutex g_mutex;

    void Open(const std::wstring& filename)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_log) return;
        // CRT _wfopen opens with no sharing on Windows, which locks the file
        // for read while the game runs. Use _wfsopen with _SH_DENYWR so other
        // processes can read (e.g. tail / editor) while we keep write access.
        g_log = _wfsopen(filename.c_str(), L"w", _SH_DENYWR);
    }

    void Close()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_log) {
            std::fclose(g_log);
            g_log = nullptr;
        }
    }

    void Line(const char* fmt, ...)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_log) return;

        SYSTEMTIME st;
        GetLocalTime(&st);
        std::fprintf(g_log, "[%02d:%02d:%02d.%03d] ",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

        va_list args;
        va_start(args, fmt);
        std::vfprintf(g_log, fmt, args);
        va_end(args);

        std::fputc('\n', g_log);
        std::fflush(g_log);
    }
}
