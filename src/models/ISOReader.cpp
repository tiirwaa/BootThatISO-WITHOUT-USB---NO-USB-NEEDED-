#include "ISOReader.h"
#ifdef HAVE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif
#include <fstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include "../utils/Utils.h"

ISOReader::ISOReader() {}

ISOReader::~ISOReader() {}

std::vector<std::string> ISOReader::listFiles(const std::string &isoPath) {
    std::vector<std::string> files;
#ifndef HAVE_LIBARCHIVE
    // Fallback: return empty, or error
    return files;
#else
    struct archive *a = archive_read_new();
    archive_read_support_format_iso9660(a);
    archive_read_support_format_zip(a);
    archive_read_support_filter_all(a);

    if (archive_read_open_filename(a, isoPath.c_str(), 10240) != ARCHIVE_OK) {
        archive_read_free(a);
        return files;
    }

    struct archive_entry *entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char *path = archive_entry_pathname(entry);
        if (path) {
            files.push_back(path);
        }
        archive_read_data_skip(a);
    }

    archive_read_free(a);
#endif
    return files;
}

bool ISOReader::fileExists(const std::string &isoPath, const std::string &filePath) {
#ifndef HAVE_LIBARCHIVE
    return false;
#else
    struct archive *a = archive_read_new();
    archive_read_support_format_iso9660(a);
    archive_read_support_format_zip(a);
    archive_read_support_filter_all(a);

    if (archive_read_open_filename(a, isoPath.c_str(), 10240) != ARCHIVE_OK) {
        archive_read_free(a);
        return false;
    }

    struct archive_entry *entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char *path = archive_entry_pathname(entry);
        if (path && std::string(path) == filePath) {
            archive_read_free(a);
            return true;
        }
        archive_read_data_skip(a);
    }

    archive_read_free(a);
#endif
    return false;
}

bool ISOReader::extractFile(const std::string &isoPath, const std::string &filePathInISO, const std::string &destPath) {
#ifndef HAVE_LIBARCHIVE
    return false;
#else
    struct archive *a = archive_read_new();
    archive_read_support_format_iso9660(a);
    archive_read_support_format_zip(a);
    archive_read_support_filter_all(a);

    if (archive_read_open_filename(a, isoPath.c_str(), 10240) != ARCHIVE_OK) {
        archive_read_free(a);
        return false;
    }

    struct archive_entry *entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char *path = archive_entry_pathname(entry);
        if (path && std::string(path) == filePathInISO) {
            // Extract this file
            createDirectories(destPath);
            std::ofstream out(destPath, std::ios::binary);
            if (!out) {
                archive_read_free(a);
                return false;
            }

            const void *buff;
            size_t size;
            int64_t offset;
            while (archive_read_data_block(a, &buff, &size, &offset) == ARCHIVE_OK) {
                out.write(static_cast<const char*>(buff), size);
            }
            out.close();
            archive_read_free(a);
            return true;
        }
        archive_read_data_skip(a);
    }

    archive_read_free(a);
#endif
    return false;
}

bool ISOReader::extractFiles(const std::string &isoPath, const std::vector<std::string> &filesInISO, const std::string &destDir) {
#ifndef HAVE_LIBARCHIVE
    return false;
#else
    struct archive *a = archive_read_new();
    archive_read_support_format_iso9660(a);
    archive_read_support_format_zip(a);
    archive_read_support_filter_all(a);

    if (archive_read_open_filename(a, isoPath.c_str(), 10240) != ARCHIVE_OK) {
        archive_read_free(a);
        return false;
    }

    struct archive_entry *entry;
    std::vector<std::string> toExtract = filesInISO;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char *path = archive_entry_pathname(entry);
        if (path) {
            std::string filePath = path;
            auto it = std::find(toExtract.begin(), toExtract.end(), filePath);
            if (it != toExtract.end()) {
                // Extract
                std::string destPath = destDir + "/" + filePath;
                createDirectories(destPath);
                std::ofstream out(destPath, std::ios::binary);
                if (out) {
                    const void *buff;
                    size_t size;
                    int64_t offset;
                    while (archive_read_data_block(a, &buff, &size, &offset) == ARCHIVE_OK) {
                        out.write(static_cast<const char*>(buff), size);
                    }
                    out.close();
                }
                toExtract.erase(it);
                if (toExtract.empty()) break;
            } else {
                archive_read_data_skip(a);
            }
        } else {
            archive_read_data_skip(a);
        }
    }

    archive_read_free(a);
    return toExtract.empty();
#endif
}

bool ISOReader::extractAll(const std::string &isoPath, const std::string &destDir, const std::vector<std::string> &excludePatterns) {
#ifndef HAVE_LIBARCHIVE
    return false;
#else
    struct archive *a = archive_read_new();
    archive_read_support_format_iso9660(a);
    archive_read_support_format_zip(a);
    archive_read_support_filter_all(a);

    if (archive_read_open_filename(a, isoPath.c_str(), 10240) != ARCHIVE_OK) {
        archive_read_free(a);
        return false;
    }

    struct archive_entry *entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char *path = archive_entry_pathname(entry);
        if (path) {
            std::string filePath = path;
            bool exclude = false;
            for (const auto &pattern : excludePatterns) {
                if (filePath.find(pattern) != std::string::npos) {
                    exclude = true;
                    break;
                }
            }
            if (!exclude) {
                std::string destPath = destDir + "/" + filePath;
                if (archive_entry_filetype(entry) == AE_IFDIR) {
                    createDirectories(destPath);
                } else {
                    createDirectories(destPath);
                    std::ofstream out(destPath, std::ios::binary);
                    if (out) {
                        const void *buff;
                        size_t size;
                        int64_t offset;
                        while (archive_read_data_block(a, &buff, &size, &offset) == ARCHIVE_OK) {
                            out.write(static_cast<const char*>(buff), size);
                        }
                        out.close();
                    }
                }
            } else {
                archive_read_data_skip(a);
            }
        } else {
            archive_read_data_skip(a);
        }
    }

    archive_read_free(a);
#endif
    return true;
}

bool ISOReader::extractDirectory(const std::string &isoPath, const std::string &dirPathInISO, const std::string &destDir) {
#ifndef HAVE_LIBARCHIVE
    return false;
#else
    struct archive *a = archive_read_new();
    archive_read_support_format_iso9660(a);
    archive_read_support_format_zip(a);
    archive_read_support_filter_all(a);

    if (archive_read_open_filename(a, isoPath.c_str(), 10240) != ARCHIVE_OK) {
        archive_read_free(a);
        return false;
    }

    struct archive_entry *entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char *path = archive_entry_pathname(entry);
        if (path) {
            std::string filePath = path;
            if (filePath.find(dirPathInISO) == 0) {  // starts with dirPathInISO
                std::string relativePath = filePath.substr(dirPathInISO.length());
                if (!relativePath.empty() && relativePath[0] == '/') relativePath = relativePath.substr(1);
                std::string destPath = destDir + "/" + relativePath;
                if (archive_entry_filetype(entry) == AE_IFDIR) {
                    createDirectories(destPath);
                } else {
                    createDirectories(destPath);
                    std::ofstream out(destPath, std::ios::binary);
                    if (out) {
                        const void *buff;
                        size_t size;
                        int64_t offset;
                        while (archive_read_data_block(a, &buff, &size, &offset) == ARCHIVE_OK) {
                            out.write(static_cast<const char*>(buff), size);
                        }
                        out.close();
                    }
                }
            } else {
                archive_read_data_skip(a);
            }
        } else {
            archive_read_data_skip(a);
        }
    }

    archive_read_free(a);
#endif
    return true;
}

void ISOReader::createDirectories(const std::string &path) {
    std::filesystem::path p(path);
    std::filesystem::create_directories(p.parent_path());
}