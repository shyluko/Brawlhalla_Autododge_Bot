#pragma once

#include <string>
#include <windows.h>

class GameVersionDetector {
public:
    static std::string ReadProductVersion(HANDLE process);
    static std::string ReadFileVersion(const std::wstring& path);
};
