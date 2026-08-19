#pragma once
#include <Windows.h>
#include <iostream>
#include <string>
#include <TlHelp32.h>
#include <codecvt>
#include <vector>
#include <cstdint>
#include <cstring>
#include <mutex>

using std::string;

static std::string to_utf8_string(const wchar_t* value) {
    if (!value) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, out.data(), needed, nullptr, nullptr);
    return out;
}

class Memory {
public:
    HANDLE hproccess = NULL;
    DWORD pID = 0;
    std::mutex mutex;

    ~Memory() {
        close();
    }

    void close() {
        if (hproccess) {
            CloseHandle(hproccess);
            hproccess = NULL;
        }
        pID = 0;
    }

    bool init(const string windowName, HWND& out) {
        HWND hwnd = FindWindowA(NULL, windowName.c_str());
        if (!hwnd) return false;
        out = hwnd;

        GetWindowThreadProcessId(hwnd, &pID);
        if (!pID) return false;

        hproccess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pID);
        if (!hproccess) return false;

        return true;
    }

    uintptr_t GetModuleBaseAddress(const char* modName) {
        uintptr_t dwModuleBaseAddress = 0;
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pID);

        if (hSnap != INVALID_HANDLE_VALUE) {
            MODULEENTRY32W modEntry;
            modEntry.dwSize = sizeof(modEntry);

            if (Module32FirstW(hSnap, &modEntry)) {
                do {
                    const std::string sModName = to_utf8_string(modEntry.szModule);

                    if (!_stricmp(sModName.c_str(), modName)) {
                        dwModuleBaseAddress = (uintptr_t)modEntry.modBaseAddr;
                        break;
                    }
                } while (Module32NextW(hSnap, &modEntry));
            }
        }
        CloseHandle(hSnap);
        return dwModuleBaseAddress;
    }

    uintptr_t FindPattern(uintptr_t start, uintptr_t size, const char* pattern, const char* mask) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!hproccess || !pattern || !mask) return 0;

        std::vector<uint8_t> buffer(size);
        ReadProcessMemory(hproccess, (LPCVOID)start, buffer.data(), size, nullptr);

        for (size_t i = 0; i + strlen(mask) <= size; i++) {
            bool match = true;
            for (size_t j = 0; j < strlen(mask); j++) {
                if (mask[j] == 'x' && buffer[i + j] != static_cast<uint8_t>(pattern[j])) {
                    match = false;
                    break;
                }
            }
            if (match) return start + i;
        }
        return 0;
    }

    // scans all committed memory safely in chunks to prevent access denied errors
    uintptr_t find_pattern_safe(const char* pattern, const char* mask) {
        SYSTEM_INFO sys_info;
        GetSystemInfo(&sys_info);
        uintptr_t current_addr = (uintptr_t)sys_info.lpMinimumApplicationAddress;
        uintptr_t max_addr = (uintptr_t)sys_info.lpMaximumApplicationAddress;

        MEMORY_BASIC_INFORMATION mbi;
        std::vector<uint8_t> buffer;

        size_t pattern_len = strlen(mask);

        while (current_addr < max_addr) {
            VirtualQueryEx(hproccess, (LPCVOID)current_addr, &mbi, sizeof(mbi));

            // only read memory if it is committed and readable
            if (mbi.State == MEM_COMMIT && (mbi.Protect == PAGE_EXECUTE_READ || mbi.Protect == PAGE_EXECUTE_READWRITE)) {
                buffer.resize(mbi.RegionSize);

                if (ReadProcessMemory(hproccess, mbi.BaseAddress, buffer.data(), mbi.RegionSize, nullptr)) {
                    for (size_t i = 0; i < mbi.RegionSize - pattern_len; i++) {
                        bool found = true;
                        for (size_t j = 0; j < pattern_len; j++) {
                            if (mask[j] == 'x' && buffer[i + j] != (uint8_t)pattern[j]) {
                                found = false;
                                break;
                            }
                        }
                        if (found) return (uintptr_t)mbi.BaseAddress + i;
                    }
                }
            }
            current_addr += mbi.RegionSize;
        }
        return 0;
    }

    // gets total size of module for pattern scanning
    uintptr_t get_module_size(const char* mod_name) {
        HANDLE h_snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pID);
        if (h_snap != INVALID_HANDLE_VALUE) {
            MODULEENTRY32W mod_entry;
            mod_entry.dwSize = sizeof(mod_entry);
            if (Module32FirstW(h_snap, &mod_entry)) {
                do {
                    const std::string s_mod_name = to_utf8_string(mod_entry.szModule);
                    if (!_stricmp(s_mod_name.c_str(), mod_name)) {
                        CloseHandle(h_snap);
                        return mod_entry.modBaseSize;
                    }
                } while (Module32NextW(h_snap, &mod_entry));
            }
        }
        CloseHandle(h_snap);
        return 0;
    }

    // allocates memory within relative jump range
    uintptr_t allocate_memory_near(uintptr_t target_addr, size_t size) {
        uintptr_t cave_addr = 0;
        for (uintptr_t i = target_addr - 0x70000000; i < target_addr + 0x70000000; i += 0x10000) {
            cave_addr = (uintptr_t)VirtualAllocEx(hproccess, (LPVOID)i, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (cave_addr) return cave_addr;
        }
        return 0;
    }

    bool free_memory(uintptr_t address) {
        return address != 0 && VirtualFreeEx(hproccess, reinterpret_cast<LPVOID>(address), 0, MEM_RELEASE) != FALSE;
    }

    // writes array of bytes for shellcode and patches
    bool write_bytes(uintptr_t address, const std::vector<uint8_t>& bytes) {
        return WriteProcessMemory(hproccess, (LPVOID)address, bytes.data(), bytes.size(), NULL);
    }

    template <typename T>
    T read(uintptr_t address) {
        T buffer{};
        std::lock_guard<std::mutex> lock(mutex);
        if (address == 0 || !hproccess) return buffer;
        SIZE_T bytesRead = 0;
        ReadProcessMemory(hproccess, (LPCVOID)address, &buffer, sizeof(T), &bytesRead);
        if (bytesRead != sizeof(T)) return T{};
        return buffer;
    }

    template <typename T>
    bool try_read(uintptr_t address, T& out) {
        std::lock_guard<std::mutex> lock(mutex);
        out = T{};
        if (address == 0 || !hproccess) return false;
        SIZE_T bytesRead = 0;
        const BOOL ok = ReadProcessMemory(hproccess, (LPCVOID)address, &out, sizeof(T), &bytesRead);
        return ok != FALSE && bytesRead == sizeof(T);
    }

    template <typename T>
    bool write(uintptr_t address, T value) {
        std::lock_guard<std::mutex> lock(mutex);
        if (address == 0 || !hproccess) return false;
        return WriteProcessMemory(hproccess, (LPVOID)address, &value, sizeof(T), NULL) != FALSE;
    }

};