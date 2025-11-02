#pragma once
#include <string>
#include <functional>

/**
 * @brief Encapsulates WIM mounting and unmounting operations using DISM.
 *
 * Follows Single Responsibility Principle - handles only WIM image mounting/unmounting.
 * Thread-safe for sequential operations on the same mount directory.
 */
class WimMounter {
public:
    /**
     * @brief Structure containing WIM image information
     */
    struct WimImageInfo {
        int         index;
        std::string name;
        std::string description;
        bool        isSetupImage;
        long long   size; // Size in bytes
    };

    /**
     * @brief Callback for progress updates during mount operations
     * @param percent Progress percentage (0-100)
     * @param message Status message
     */
    using ProgressCallback = std::function<void(int percent, const std::string &message)>;

    WimMounter();
    ~WimMounter();

    /**
     * @brief Gets information about all images in a WIM file
     * @param wimPath Full path to the WIM file
     * @return Vector of WimImageInfo structures
     */
    std::vector<WimImageInfo> getWimImageInfo(const std::string &wimPath);

    /**
     * @brief Selects the best image index to mount (prefers Windows Setup)
     * @param wimPath Full path to the WIM file
     * @return Selected image index (1-based)
     */
    int selectBestImageIndex(const std::string &wimPath);

    /**
     * @brief Mounts a WIM image to a directory
     * @param wimPath Full path to the WIM file
     * @param mountDir Directory where the image will be mounted
     * @param imageIndex Image index to mount (1-based)
     * @param progressCallback Optional callback for progress updates
     * @return true if mount successful, false otherwise
     */
    bool mountWim(const std::string &wimPath, const std::string &mountDir, int imageIndex,
                  ProgressCallback progressCallback = nullptr);

    /**
     * @brief Unmounts a WIM image and commits or discards changes
     * @param mountDir Directory where the image is mounted
     * @param commit If true, saves changes; if false, discards them
     * @param progressCallback Optional callback for progress updates
     * @return true if unmount successful, false otherwise
     */
    bool unmountWim(const std::string &mountDir, bool commit = true, ProgressCallback progressCallback = nullptr);

    /**
     * @brief Cleans up mount directory if it exists
     * @param mountDir Directory to clean
     */
    void cleanupMountDirectory(const std::string &mountDir);

    /**
     * @brief Exports a specific index from a source WIM to a destination WIM
     * @param sourceWim Path to source WIM file (install.wim/install.esd)
     * @param sourceIndex Index to export from source (1-based)
     * @param destWim Path to destination WIM file (boot.wim)
     * @param destIndex Index where to add in destination (2 for boot.wim typically)
     * @param progressCallback Optional callback for progress updates
     * @return true if export successful, false otherwise
     */
    bool exportWimIndex(const std::string &sourceWim, int sourceIndex, const std::string &destWim, int destIndex,
                        ProgressCallback progressCallback = nullptr);

    /**
     * @brief Gets the last DISM error message
     * @return Error message string
     */
    std::string getLastError() const {
        return lastError_;
    }

private:
    std::string lastError_;
    std::string lastDismOutput_;

    /**
     * @brief Executes DISM command and captures output
     * @param command DISM command to execute
     * @param output Reference to store command output
     * @return Exit code from DISM
     */
    int executeDism(const std::string &command, std::string &output);

    /**
     * @brief Parses DISM /Get-WimInfo output
     * @param dismOutput Raw DISM output
     * @return Vector of parsed image info
     */
    std::vector<WimImageInfo> parseWimInfo(const std::string &dismOutput);

    /**
     * @brief Converts string to lowercase and removes accents
     * @param s Input string
     * @return Normalized string
     */
    std::string normalizeString(const std::string &s);
};
