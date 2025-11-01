#include "Utils.h"
#include <windows.h>
#include <wincrypt.h>

#include <vector>


std::string Utils::exec(const char* cmd) {
    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return "";

    STARTUPINFOA si = { sizeof(STARTUPINFOA) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi;
    if (!CreateProcessA(NULL, (LPSTR)cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return "";
    }

    CloseHandle(hWrite);

    char buffer[128];
    std::string result = "";
    DWORD bytesRead;
    while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        result += buffer;
    }

    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return result;
}

long long Utils::getFileSize(const std::string& filePath) {
    HANDLE hFile = CreateFileA(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return -1;
    }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(hFile, &size)) {
        CloseHandle(hFile);
        return -1;
    }
    CloseHandle(hFile);
    return size.QuadPart;
}

long long Utils::getDirectorySize(const std::string& dirPath) {
    long long totalSize = 0;
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA((dirPath + "\\*").c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return 0;
    do {
        std::string name = findData.cFileName;
        if (name != "." && name != "..") {
            std::string fullPath = dirPath + "\\" + name;
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                totalSize += getDirectorySize(fullPath);
            } else {
                long long fileSize = ((long long)findData.nFileSizeHigh << 32) | findData.nFileSizeLow;
                totalSize += fileSize;
            }
        }
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
    return totalSize;
}

std::string Utils::getExeDirectory() {
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    std::string path(buffer);
    size_t pos = path.find_last_of("\\/");
    if (pos != std::string::npos) {
        return path.substr(0, pos + 1);
    }
    return "";
}

std::wstring Utils::utf8_to_wstring(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), NULL, 0);
    if (size_needed <= 0) return std::wstring();
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &wstr[0], size_needed);
    return wstr;
}

std::string Utils::wstring_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    if (size_needed <= 0) return std::string();
    std::string str(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &str[0], size_needed, NULL, NULL);
    return str;
}

std::string Utils::ansi_to_utf8(const std::string& ansi) {
    if (ansi.empty()) return std::string();
    int size_needed = MultiByteToWideChar(CP_ACP, 0, ansi.c_str(), (int)ansi.size(), NULL, 0);
    if (size_needed <= 0) return std::string();
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_ACP, 0, ansi.c_str(), (int)ansi.size(), &wstr[0], size_needed);
    return wstring_to_utf8(wstr);
}

std::string Utils::calculateMD5(const std::string& filePath) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    HANDLE hFile = NULL;
    BYTE rgbHash[16];
    DWORD cbHash = 0;
    CHAR rgbDigits[] = "0123456789abcdef";
    std::string hashStr;

    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        return "";
    }

    if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return "";
    }

    hFile = CreateFileA(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return "";
    }

    BYTE rgbFile[1024];
    DWORD cbRead = 0;
    while (ReadFile(hFile, rgbFile, sizeof(rgbFile), &cbRead, NULL) && cbRead > 0) {
        if (!CryptHashData(hHash, rgbFile, cbRead, 0)) {
            CloseHandle(hFile);
            CryptDestroyHash(hHash);
            CryptReleaseContext(hProv, 0);
            return "";
        }
    }

    CloseHandle(hFile);

    cbHash = 16;
    if (CryptGetHashParam(hHash, HP_HASHVAL, rgbHash, &cbHash, 0)) {
        for (DWORD i = 0; i < cbHash; i++) {
            hashStr += rgbDigits[rgbHash[i] >> 4];
            hashStr += rgbDigits[rgbHash[i] & 0xf];
        }
    }

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    return hashStr;
}