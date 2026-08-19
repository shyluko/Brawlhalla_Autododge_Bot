
#include "memory.h"
#include <windows.h>
#include <iostream>
#include <cstdio>
#include <vector>

// Read-only snapshot tool used to compare player entities with ground items,
// gadgets, and projectiles captured by the position hook.

Memory g_mem;

std::string hexAddr(uintptr_t v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%llX", (unsigned long long)v);
    return buf;
}

int main() {
    std::cout << "=== Brawlhalla entity diagnostics ===\n";
    std::cout << "Enter a match before running this tool. Press ENTER to snapshot the\n";
    std::cout << "captured entity array, then repeat with a weapon on the ground.\n\n";

    HWND hwnd_out;
    if (!g_mem.init("Brawlhalla", hwnd_out)) {
        std::cout << "error: Brawlhalla is not running\n";
        system("pause");
        return 1;
    }

    const char* pattern = "\xF2\x0F\x10\x8B\x40\x01\x00\x00";
    const char* mask    = "xxxxxxxx";
    uintptr_t target_addr = g_mem.find_pattern_safe(pattern, mask);
    if (!target_addr) {
        std::cout << "error: pattern not found; the game may have updated\n";
        system("pause");
        return 1;
    }

    uintptr_t cave_addr     = g_mem.allocate_memory_near(target_addr, 0x1000);
    if (!cave_addr) {
        std::cout << "error: failed to allocate memory near the hook\n";
        system("pause");
        return 1;
    }
    uintptr_t pointer_array = cave_addr + 0x100;
    uintptr_t index_store   = cave_addr + 0x80;
    uintptr_t return_addr   = target_addr + 8;

    std::vector<uint8_t> shellcode = {
        0x50, 0x51, 0x52,
        0x48, 0xB8, 0,0,0,0,0,0,0,0,
        0x8B, 0x08, 0x83, 0xF9, 0x32, 0x7C, 0x02, 0x31, 0xC9,
        0x48, 0xBA, 0,0,0,0,0,0,0,0,
        0x48, 0x89, 0x1C, 0xCA, 0xFF, 0xC1, 0x89, 0x08,
        0x5A, 0x59, 0x58,
        0xF2, 0x0F, 0x10, 0x8B, 0x40, 0x01, 0x00, 0x00,
        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,
        0,0,0,0,0,0,0,0
    };
    memcpy(&shellcode[5],  &index_store,   8);
    memcpy(&shellcode[24], &pointer_array, 8);
    memcpy(&shellcode[57], &return_addr,   8);
    if (!g_mem.write_bytes(cave_addr, shellcode)) {
        std::cout << "error: failed to write shellcode\n";
        system("pause");
        return 1;
    }

    std::vector<uint8_t> patch = { 0xE9, 0,0,0,0, 0x90, 0x90, 0x90 };
    DWORD relative_jmp = (DWORD)(cave_addr - target_addr - 5);
    memcpy(&patch[1], &relative_jmp, 4);
    if (!g_mem.write_bytes(target_addr, patch)) {
        std::cout << "error: failed to install the patch\n";
        system("pause");
        return 1;
    }

    std::cout << "Hook installed. Press ENTER for a snapshot (Ctrl+C to exit)...\n";

    while (true) {
        std::cin.get();

        std::vector<uintptr_t> dps;
        for (int i = 0; i < 50; i++) {
            uintptr_t dp = g_mem.read<uintptr_t>(pointer_array + i * 8);
            if (!dp) continue;
            bool dup = false;
            for (auto d : dps) if (d == dp) { dup = true; break; }
            if (!dup) dps.push_back(dp);
        }

        printf("\n=== %zu captured entities ===\n", dps.size());
        for (size_t i = 0; i < dps.size(); i++) {
            uintptr_t dp = dps[i];
            double px = g_mem.read<double>(dp + 0x140);
            double py = g_mem.read<double>(dp + 0x138);
            bool posValida = (px > -5000 && px < 5000 && py > -5000 && py < 5000);

            uint8_t is_attacking = g_mem.read<uint8_t>(dp + 0x050);
            int attack_id        = g_mem.read<int>(dp + 0x06C);
            uintptr_t entity_ptr = g_mem.read<uintptr_t>(dp + 0x0C0);
            uintptr_t weapon_ptr = g_mem.read<uintptr_t>(dp + 0x0C8);

            int facing = 0, airborne = 0, is_local_player = -999, is_stunned = 0, can_dodge = 0;
            int stocks = 0, remaining_options = 0, knocked_back = 0;
            bool is_dead = false, is_edging = false;
            double damage = 0;
            bool entityValida = (entity_ptr > 0x10000 && entity_ptr < 0x7FFFFFFFFFFF);
            if (entityValida) {
                facing          = g_mem.read<int>(entity_ptr + 0x110);
                airborne        = g_mem.read<int>(entity_ptr + 0x12C);
                is_stunned      = g_mem.read<int>(entity_ptr + 0x144);
                can_dodge       = g_mem.read<int>(entity_ptr + 0x1A4);
                is_local_player = g_mem.read<int>(entity_ptr + 0x250);
                damage          = g_mem.read<double>(entity_ptr + 0x5D8);
                is_dead          = g_mem.read<int>(entity_ptr + 0x25C) != 0;
                is_edging        = g_mem.read<int>(entity_ptr + 0x154) != 0;
                remaining_options = g_mem.read<int>(entity_ptr + 0x220);
                stocks           = g_mem.read<int>(entity_ptr + 0x2A4);
                knocked_back     = g_mem.read<int>(entity_ptr + 0x084);
            }

            int weapon_type_id = -999;
            bool weaponValida = (weapon_ptr > 0x10000 && weapon_ptr < 0x7FFFFFFFFFFF);
            if (weaponValida) {
                uintptr_t weapon_type_ptr = g_mem.read<uintptr_t>(weapon_ptr + 0x48);
                if (weapon_type_ptr > 0x10000 && weapon_type_ptr < 0x7FFFFFFFFFFF) {
                    weapon_type_id = g_mem.read<int>(weapon_type_ptr + 0xB4);
                }
            }

            printf("--- entity %zu: dp=%s valid_position=%d ---\n", i, hexAddr(dp).c_str(), posValida ? 1 : 0);
            printf("  pos=(%.1f, %.1f)\n", px, py);
            printf("  is_attacking=%d attack_id=%d\n", (int)is_attacking, attack_id);
            printf("  entity_ptr=%s (valido=%d) weapon_ptr=%s (valido=%d)\n",
                   hexAddr(entity_ptr).c_str(), entityValida ? 1 : 0,
                   hexAddr(weapon_ptr).c_str(), weaponValida ? 1 : 0);
            if (entityValida) {
                printf("  [post-patch candidates] facing=%d airborne=%d CanDodge=%d\n",
                       facing, airborne, can_dodge);
                printf("  [legacy fields to recheck] is_stunned=%d is_local_player=%d damage=%.2f\n",
                       is_stunned, is_local_player, damage);
                printf("  [forum theory] stocks=%d dead=%d edging=%d remaining=%d knocked_back=%d\n",
                       stocks, is_dead ? 1 : 0, is_edging ? 1 : 0, remaining_options, knocked_back);
            }
            if (weaponValida) {
                printf("  weapon_type_id=%d\n", weapon_type_id);
            }
        }
        std::cout << "\nPress ENTER for another snapshot...\n";
    }
    return 0;
}

