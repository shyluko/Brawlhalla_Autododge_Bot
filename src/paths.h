#pragma once

#include <filesystem>
#include <windows.h>

inline std::filesystem::path ExecutableDirectory() {
    char buffer[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return std::filesystem::current_path();
    return std::filesystem::path(buffer).parent_path();
}

inline std::filesystem::path LogsDirectory() {
    const auto dir = ExecutableDirectory() / "logs";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

inline std::filesystem::path ConfigDirectory() {
    const auto dir = ExecutableDirectory() / "config";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

inline void OpenFolderInExplorer(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
