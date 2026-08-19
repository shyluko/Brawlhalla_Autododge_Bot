#include "GameVersionDetector.h"

#include <cstdio>
#include <vector>
#include <psapi.h>

std::string GameVersionDetector::ReadFileVersion(const std::wstring& path) {
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size == 0) return "unknown";

    std::vector<std::byte> buffer(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, buffer.data())) return "unknown";

    VS_FIXEDFILEINFO* info = nullptr;
    UINT infoSize = 0;
    if (!VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<LPVOID*>(&info), &infoSize) || !info) {
        return "unknown";
    }

    char text[64]{};
    snprintf(text, sizeof(text), "%u.%u.%u.%u",
             HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
             HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
    return text;
}

std::string GameVersionDetector::ReadProductVersion(HANDLE process) {
    if (!process) return "unknown";
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameExW(process, nullptr, path, MAX_PATH)) return "unknown";
    return ReadFileVersion(path);
}
