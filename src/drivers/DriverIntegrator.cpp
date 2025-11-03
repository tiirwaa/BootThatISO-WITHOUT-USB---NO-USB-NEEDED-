#include "DriverIntegrator.h"
#include "../utils/Utils.h"
#include "../services/ISOCopyManager.h"
#include <windows.h>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <fstream>

DriverIntegrator::DriverIntegrator() : stagedStorage_(0), stagedUsb_(0), stagedNetwork_(0), stagedCustom_(0) {}

DriverIntegrator::~DriverIntegrator() {}

int DriverIntegrator::executeDism(const std::string &command, std::string &output) {
    return Utils::execWithExitCode(command.c_str(), output);
}

bool DriverIntegrator::isStorageDriver(const std::string &dirNameLower) {
    const std::vector<std::string> storagePrefixes = {"storahci", "stornvme", "msahci", "iastor", "iaahci"};
    const std::vector<std::string> storageTokens   = {"nvme", "ahci",   "rst",    "vmd",      "raid",   "scsi",
                                                      "ide",  "iastor", "iaahci", "msahci",   "disk",   "storage",
                                                      "sata", "pciide", "atapi",  "intelide", "amdide", "viaide"};

    for (const auto &prefix : storagePrefixes) {
        if (dirNameLower.rfind(prefix, 0) == 0)
            return true;
    }

    for (const auto &token : storageTokens) {
        if (dirNameLower.find(token) != std::string::npos)
            return true;
    }

    return false;
}

bool DriverIntegrator::isUsbDriver(const std::string &dirNameLower) {
    const std::vector<std::string> usbPrefixes = {"usb", "xhci"};
    const std::vector<std::string> usbTokens   = {"iusb3", "usb3", "xhc", "xhci", "amdhub3", "amdxhc", "intelusb3"};

    for (const auto &prefix : usbPrefixes) {
        if (dirNameLower.rfind(prefix, 0) == 0)
            return true;
    }

    for (const auto &token : usbTokens) {
        if (dirNameLower.find(token) != std::string::npos)
            return true;
    }

    return false;
}

bool DriverIntegrator::directoryContainsNetworkInf(const std::string &dirPath) {
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dirPath, ec), end; it != end; it.increment(ec)) {
        if (ec)
            break;

        if (!it->is_regular_file())
            continue;

        std::string extension = it->path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (extension != ".inf")
            continue;

        std::ifstream infFile(it->path());
        if (!infFile.is_open())
            continue;

        std::ostringstream buffer;
        buffer << infFile.rdbuf();
        std::string content = buffer.str();

        // Normalize: lowercase and remove non-alphanumeric
        std::string normalized;
        normalized.reserve(content.size());
        for (char ch : content) {
            unsigned char uch   = static_cast<unsigned char>(ch);
            char          lower = static_cast<char>(std::tolower(uch));
            if ((lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9')) {
                normalized.push_back(lower);
            }
        }

        // Check for network class indicators
        if (normalized.find("classnet") != std::string::npos ||
            normalized.find("4d36e972e32511cebfc108002be10318") != std::string::npos) {
            return true;
        }
    }

    return false;
}

bool DriverIntegrator::isNetworkDriver(const std::string &dirNameLower, const std::string &dirPath) {
    const std::vector<std::string> networkPrefixes = {"net", "vwifi", "vwlan"};
    const std::vector<std::string> networkTokens   = {"wifi", "wlan", "wwan"};

    for (const auto &prefix : networkPrefixes) {
        if (dirNameLower.rfind(prefix, 0) == 0)
            return true;
    }

    for (const auto &token : networkTokens) {
        if (dirNameLower.find(token) != std::string::npos)
            return true;
    }

    return directoryContainsNetworkInf(dirPath);
}

bool DriverIntegrator::stageSystemDrivers(const std::string &stagingDir, DriverCategory categories,
                                          std::ofstream &logFile) {
    char windowsDir[MAX_PATH] = {0};
    UINT written              = GetWindowsDirectoryA(windowsDir, MAX_PATH);
    if (written == 0 || written >= MAX_PATH) {
        lastError_ = "Failed to resolve Windows directory";
        logFile << ISOCopyManager::getTimestamp() << "Error: " << lastError_ << std::endl;
        return false;
    }

    std::string systemRoot(windowsDir);
    if (!systemRoot.empty() && (systemRoot.back() == '\\' || systemRoot.back() == '/')) {
        systemRoot.pop_back();
    }

    std::string fileRepository = systemRoot + "\\System32\\DriverStore\\FileRepository";
    if (GetFileAttributesA(fileRepository.c_str()) == INVALID_FILE_ATTRIBUTES) {
        lastError_ = "DriverStore not found at " + fileRepository;
        logFile << ISOCopyManager::getTimestamp() << "Error: " << lastError_ << std::endl;
        return false;
    }

    bool                  copiedAny = false;
    std::error_code       ec;
    std::filesystem::path stagingRoot(stagingDir);
    std::filesystem::create_directories(stagingRoot, ec);
    if (ec) {
        lastError_ = "Failed to create staging directory: " + ec.message();
        return false;
    }

    std::filesystem::directory_iterator it(fileRepository, ec);
    if (ec) {
        lastError_ = "Unable to enumerate DriverStore: " + ec.message();
        logFile << ISOCopyManager::getTimestamp() << "Error: " << lastError_ << std::endl;
        return false;
    }

    for (auto end = std::filesystem::directory_iterator(); it != end; it.increment(ec)) {
        if (ec) {
            logFile << ISOCopyManager::getTimestamp() << "DriverStore enumeration error: " << ec.message() << std::endl;
            break;
        }

        const auto &entry = *it;
        if (!entry.is_directory())
            continue;

        std::string dirName      = entry.path().filename().string();
        std::string dirNameLower = dirName;
        std::transform(dirNameLower.begin(), dirNameLower.end(), dirNameLower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        bool storageMatch = (categories == DriverCategory::All || categories == DriverCategory::Storage) &&
                            isStorageDriver(dirNameLower);
        bool usbMatch =
            (categories == DriverCategory::All || categories == DriverCategory::Usb) && isUsbDriver(dirNameLower);
        bool networkMatch = (categories == DriverCategory::All || categories == DriverCategory::Network) &&
                            isNetworkDriver(dirNameLower, entry.path().string());

        if (!(storageMatch || usbMatch || networkMatch))
            continue;

        std::filesystem::path destination = stagingRoot / dirName;
        std::filesystem::create_directories(destination, ec);
        if (ec) {
            logFile << ISOCopyManager::getTimestamp() << "Failed to create staging directory for " << dirName << ": "
                    << ec.message() << std::endl;
            ec.clear();
            continue;
        }

        std::filesystem::copy(
            entry.path(), destination,
            std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            logFile << ISOCopyManager::getTimestamp() << "Failed to copy driver directory " << dirName << ": "
                    << ec.message() << std::endl;
            ec.clear();
            continue;
        }

        if (storageMatch)
            stagedStorage_++;
        if (usbMatch)
            stagedUsb_++;
        if (networkMatch)
            stagedNetwork_++;

        std::string cat = networkMatch ? "network" : (storageMatch ? "storage" : (usbMatch ? "usb" : "other"));
        logFile << ISOCopyManager::getTimestamp() << "Staged driver directory " << dirName << " (" << cat << ")"
                << std::endl;
        copiedAny = true;
    }

    if (copiedAny) {
        logFile << ISOCopyManager::getTimestamp() << "Staged driver directories: storage=" << stagedStorage_
                << ", usb=" << stagedUsb_ << ", network=" << stagedNetwork_ << std::endl;
    }

    return copiedAny;
}

bool DriverIntegrator::addDriversToImage(const std::string &mountDir, const std::string &stagingDir,
                                         std::ofstream &logFile, bool isCustomDrivers) {
    std::string dism = Utils::getDismPath();
    std::string command =
        "\"" + dism + "\" /Image:\"" + mountDir + "\" /Add-Driver /Driver:\"" + stagingDir + "\" /Recurse";

    std::string driverType = isCustomDrivers ? "CustomDrivers" : "system drivers";
    logFile << ISOCopyManager::getTimestamp() << "Adding " << driverType << " with command: " << command << std::endl;

    std::string output;
    int         exitCode = executeDism(command, output);

    logFile << ISOCopyManager::getTimestamp() << "DISM add-driver (" << driverType << ") output (code=" << exitCode
            << "): " << output << std::endl;

    if (exitCode != 0) {
        // Retry with /ForceUnsigned for unsigned drivers
        std::string commandForce = command + " /ForceUnsigned";
        logFile << ISOCopyManager::getTimestamp() << "Retrying with /ForceUnsigned: " << commandForce << std::endl;

        std::string output2;
        int         exitCode2 = executeDism(commandForce, output2);

        logFile << ISOCopyManager::getTimestamp() << "DISM add-driver (force unsigned) output (code=" << exitCode2
                << "): " << output2 << std::endl;

        if (exitCode2 != 0) {
            lastError_ = "DISM add-driver failed for " + driverType;
            return false;
        }
    }

    return true;
}

bool DriverIntegrator::integrateSystemDrivers(const std::string &mountDir, DriverCategory categories,
                                              std::ofstream &logFile, ProgressCallback progressCallback) {
    if (progressCallback)
        progressCallback("Preparando integración de controladores locales...");

    // Create temporary staging directory
    char tempPath[MAX_PATH] = {0};
    if (!GetTempPathA(MAX_PATH, tempPath)) {
        lastError_ = "GetTempPath failed";
        logFile << ISOCopyManager::getTimestamp() << "Error: " << lastError_ << std::endl;
        return false;
    }

    char tempDirTemplate[MAX_PATH] = {0};
    if (!GetTempFileNameA(tempPath, "drv", 0, tempDirTemplate)) {
        lastError_ = "GetTempFileName failed";
        logFile << ISOCopyManager::getTimestamp() << "Error: " << lastError_ << std::endl;
        return false;
    }

    DeleteFileA(tempDirTemplate);
    if (!CreateDirectoryA(tempDirTemplate, NULL)) {
        lastError_ = "Failed to create staging directory";
        logFile << ISOCopyManager::getTimestamp() << "Error: " << lastError_ << std::endl;
        return false;
    }

    std::string stagingDir(tempDirTemplate);

    if (progressCallback)
        progressCallback("Preparando controladores del sistema...");

    // Stage drivers
    bool staged = stageSystemDrivers(stagingDir, categories, logFile);
    if (!staged) {
        std::error_code ec;
        std::filesystem::remove_all(stagingDir, ec);
        return false;
    }

    if (progressCallback)
        progressCallback("Integrando controladores en la imagen WIM...");

    // Add drivers to image
    bool success = addDriversToImage(mountDir, stagingDir, logFile, false);

    // Cleanup staging directory
    std::error_code ec;
    std::filesystem::remove_all(stagingDir, ec);

    if (progressCallback) {
        if (success)
            progressCallback("Controladores del sistema integrados exitosamente");
        else
            progressCallback("Error al integrar controladores del sistema");
    }

    return success;
}

bool DriverIntegrator::integrateCustomDrivers(const std::string &mountDir, const std::string &customDriversSource,
                                              std::ofstream &logFile, ProgressCallback progressCallback) {
    if (customDriversSource.empty() || GetFileAttributesA(customDriversSource.c_str()) == INVALID_FILE_ATTRIBUTES) {
        lastError_ = "Custom drivers source not found: " + customDriversSource;
        logFile << ISOCopyManager::getTimestamp() << "Info: " << lastError_ << std::endl;
        return false; // Not an error, just no custom drivers to integrate
    }

    if (progressCallback)
        progressCallback("Integrando CustomDrivers...");

    logFile << ISOCopyManager::getTimestamp() << "Integrating CustomDrivers from " << customDriversSource << std::endl;

    bool success = addDriversToImage(mountDir, customDriversSource, logFile, true);

    if (success) {
        stagedCustom_++;
        logFile << ISOCopyManager::getTimestamp() << "CustomDrivers integrated successfully" << std::endl;
        if (progressCallback)
            progressCallback("CustomDrivers integrados exitosamente");
    } else {
        logFile << ISOCopyManager::getTimestamp() << "Failed to integrate CustomDrivers" << std::endl;
        if (progressCallback)
            progressCallback("Error al integrar CustomDrivers");
    }

    return success;
}

std::string DriverIntegrator::getIntegrationStats() const {
    std::ostringstream oss;
    oss << "Drivers integrados: ";
    oss << "storage=" << stagedStorage_ << ", ";
    oss << "usb=" << stagedUsb_ << ", ";
    oss << "network=" << stagedNetwork_ << ", ";
    oss << "custom=" << stagedCustom_;
    return oss.str();
}
