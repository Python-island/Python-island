#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>   // for FILETIME compatibility
#endif

// ============================================================
//  Shared file metadata structs
// ============================================================
// NOTE: paths are stored as std::string in UTF-8 encoding on all
// platforms.  On Windows callers are responsible for converting to
// UTF-8 before passing paths in; on Linux the native filesystem
// encoding is already UTF-8.

struct FileInfo {
    std::string path;           // UTF-8 path
    std::string name;           // filename only
    uint64_t size;              // bytes
    std::chrono::system_clock::time_point added_time;
};

struct FilePreviewInfo {
    std::string file_type;
    std::string file_extension;
    std::string creation_time;
    std::string last_modified_time;
    std::string last_access_time;
    std::string text_content;
    bool is_image = false;
    bool is_text  = false;
};

// ============================================================
//  Transfer station manager
// ============================================================
class TransferStation {
public:
    static TransferStation& Instance();

    // Add a file to the station
    void AddFile(const std::string& filePath);

    // Remove file by index
    void RemoveFile(size_t index);

    // Clear all files
    void Clear();

    // Get file list (returns a copy)
    std::vector<FileInfo> GetFiles() const;

    // File count
    size_t GetFileCount() const;

    // Total size of all files
    uint64_t GetTotalSize() const;

    // Open file with default application
    bool OpenFile(size_t index) const;

    // Copy file to destination directory
    bool TransferCopyFile(size_t index, const std::string& destination) const;

    // Move file to destination directory
    bool TransferMoveFile(size_t index, const std::string& destination);

    // Delete file from disk
    bool TransferDeleteFile(size_t index);

    // Get detailed preview info
    FilePreviewInfo GetFilePreviewInfo(size_t index) const;

    // Read text file content (max 1 MB)
    std::string ReadTextFile(const std::string& filePath) const;

    // Check whether file is an image by extension
    bool IsImageFile(const std::string& filePath) const;

    // Check whether file is text by extension
    bool IsTextFile(const std::string& filePath) const;

private:
    TransferStation() = default;
    ~TransferStation() = default;
    TransferStation(const TransferStation&) = delete;
    TransferStation& operator=(const TransferStation&) = delete;

    mutable std::mutex mutex;
    std::vector<FileInfo> files;
    uint64_t totalSize = 0;

    // Internal helpers
    uint64_t GetFileSize(const std::string& filePath) const;
    std::string GetFileName(const std::string& filePath) const;
    std::string GetFileExtension(const std::string& filePath) const;

#ifdef _WIN32
    // On Windows, convert FILETIME to string format
    std::string GetFileTimeString(FILETIME fileTime) const;
#endif
};

// Global access macro
#define g_transferstation TransferStation::Instance()
