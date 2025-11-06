#include "EFIManager.h"
#include "../models/EventManager.h"
#include <windows.h>
#include <winnt.h>
#include <algorithm>
#include <sstream>

EFIManager::EFIManager() {}

std::optional<std::string> EFIManager::selectEFIBootFile(const std::string &espDriveLetter,
                                                         const std::string &strategyType, EventManager *eventManager) {
    auto candidates = getCandidates(espDriveLetter, strategyType);
    return evaluateCandidates(candidates, eventManager);
}

WORD EFIManager::getMachineType(const std::string &filePath) {
    HANDLE hFile =
        CreateFileA(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return 0;

    HANDLE hMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!hMapping) {
        CloseHandle(hFile);
        return 0;
    }

    LPVOID lpBase = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
    if (!lpBase) {
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return 0;
    }

    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)lpBase;
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        UnmapViewOfFile(lpBase);
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return 0;
    }

    PIMAGE_NT_HEADERS ntHeader = (PIMAGE_NT_HEADERS)((BYTE *)lpBase + dosHeader->e_lfanew);
    if (ntHeader->Signature != IMAGE_NT_SIGNATURE) {
        UnmapViewOfFile(lpBase);
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return 0;
    }

    WORD machine = ntHeader->FileHeader.Machine;
    UnmapViewOfFile(lpBase);
    CloseHandle(hMapping);
    CloseHandle(hFile);
    return machine;
}

std::vector<std::string> EFIManager::getCandidates(const std::string &espDriveLetter, const std::string &strategyType) {
    std::vector<std::string> candidates;
    if (strategyType == "extracted") {
        candidates = {espDriveLetter + "\\EFI\\BOOT\\BOOTX64.EFI", espDriveLetter + "\\EFI\\boot\\BOOTX64.EFI",
                      espDriveLetter + "\\EFI\\boot\\bootx64.efi",
                      espDriveLetter + "\\EFI\\Microsoft\\Boot\\bootmgfw.efi",
                      espDriveLetter + "\\EFI\\microsoft\\boot\\bootmgfw.efi"};
    } else if (strategyType == "ramdisk") {
        candidates = {espDriveLetter + "\\EFI\\Microsoft\\Boot\\bootmgfw.efi",
                      espDriveLetter + "\\EFI\\microsoft\\boot\\bootmgfw.efi",
                      espDriveLetter + "\\EFI\\BOOT\\BOOTX64.EFI", espDriveLetter + "\\EFI\\boot\\BOOTX64.EFI",
                      espDriveLetter + "\\EFI\\boot\\bootx64.efi"};
    } else if (strategyType == "linux") {
        // For Linux, prioritize GRUB EFI in /EFI/grub/ directory (compiled with -p /EFI/grub), then fall back to ISO
        // EFI files
        candidates = {espDriveLetter + "\\EFI\\grub\\grubx64.efi",  espDriveLetter + "\\EFI\\BOOT\\BOOTX64.EFI",
                      espDriveLetter + "\\EFI\\boot\\BOOTX64.EFI",  espDriveLetter + "\\EFI\\boot\\bootx64.efi",
                      espDriveLetter + "\\EFI\\boot\\BOOTIA32.EFI", espDriveLetter + "\\EFI\\boot\\bootia32.efi"};
    } else {
        candidates = {espDriveLetter + "\\EFI\\BOOT\\BOOTX64.EFI",
                      espDriveLetter + "\\EFI\\boot\\BOOTX64.EFI",
                      espDriveLetter + "\\EFI\\boot\\bootx64.efi",
                      espDriveLetter + "\\EFI\\boot\\BOOTIA32.EFI",
                      espDriveLetter + "\\EFI\\boot\\bootia32.efi",
                      espDriveLetter + "\\EFI\\Microsoft\\Boot\\bootmgr.efi",
                      espDriveLetter + "\\EFI\\Microsoft\\Boot\\bootmgfw.efi",
                      espDriveLetter + "\\EFI\\microsoft\\boot\\bootmgfw.efi"};
    }
    return candidates;
}

std::optional<std::string> EFIManager::evaluateCandidates(const std::vector<std::string> &candidates,
                                                          EventManager                   *eventManager) {
    int              bestIndex       = -1;
    int              bestScore       = -1;
    int              firstAmd64Index = -1;
    int              firstI386Index  = -1;
    std::vector<int> existingIndices;

    for (size_t i = 0; i < candidates.size(); ++i) {
        const std::string &c = candidates[i];
        if (GetFileAttributesA(c.c_str()) == INVALID_FILE_ATTRIBUTES)
            continue;
        existingIndices.push_back((int)i);
        WORD m     = getMachineType(c);
        int  score = 0;
        if (m == IMAGE_FILE_MACHINE_AMD64)
            score = 2;
        else if (m == IMAGE_FILE_MACHINE_I386)
            score = 1;
        else
            score = 0;
        if (firstAmd64Index == -1 && m == IMAGE_FILE_MACHINE_AMD64)
            firstAmd64Index = (int)i;
        if (firstI386Index == -1 && m == IMAGE_FILE_MACHINE_I386)
            firstI386Index = (int)i;
        if (eventManager) {
            std::ostringstream oss;
            oss << std::hex << std::uppercase << m;
            eventManager->notifyLogUpdate("Candidate EFI: " + c + ", machine=0x" + oss.str() +
                                          " score=" + std::to_string(score) + "\r\n");
        }
        if (score > bestScore) {
            bestScore = score;
            bestIndex = (int)i;
            if (bestScore == 2)
                break;
        }
    }

    // If both architectures available, ask user
    if (firstAmd64Index != -1 && firstI386Index != -1) {
        // For simplicity, prefer x64
        bestIndex = firstAmd64Index;
    }

    if (bestIndex != -1) {
        return candidates[bestIndex];
    } else if (!existingIndices.empty()) {
        return candidates[existingIndices[0]];
    }

    return std::nullopt;
}