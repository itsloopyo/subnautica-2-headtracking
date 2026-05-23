#pragma once
#include <string>

namespace Subnautica2HeadTracking::Log
{
    void Open(const std::wstring& filename);
    void Close();
    void Line(const char* fmt, ...);
}
