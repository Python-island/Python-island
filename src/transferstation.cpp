#include "transferstation.h"
#include "logging.h"

#ifdef _WIN32
// ============================================================
//  Windows implementation — ANSI APIs
// ============================================================
#include <windows.h>
#include <shlwapi.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>

#pragma comment(lib, "shlwapi.lib")

TransferStation& TransferStation::Instance() {
    static TransferStation instance;
    return instance;
}

void TransferStation::AddFile(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(mutex);

    FileInfo fileInfo;
    fileInfo.path = filePath;
    fileInfo.name = GetFileName(filePath);
    fileInfo.size = GetFileSize(filePath);
    fileInfo.added_time = std::chrono::system_clock::now();

    files.push_back(fileInfo);
    totalSize += fileInfo.size;
}

void TransferStation::RemoveFile(size_t index) {
    std::lock_guard<std::mutex> lock(mutex);

    if (index < files.size()) {
        totalSize -= files[index].size;
        files.erase(files.begin() + index);
    }
}

void TransferStation::Clear() {
    std::lock_guard<std::mutex> lock(mutex);

    files.clear();
    totalSize = 0;
}

std::vector<FileInfo> TransferStation::GetFiles() const {
    std::lock_guard<std::mutex> lock(mutex);
    return files;
}

size_t TransferStation::GetFileCount() const {
    std::lock_guard<std::mutex> lock(mutex);
    return files.size();
}

uint64_t TransferStation::GetTotalSize() const {
    std::lock_guard<std::mutex> lock(mutex);
    return totalSize;
}

bool TransferStation::OpenFile(size_t index) const {
    std::lock_guard<std::mutex> lock(mutex);

    if (index >= files.size()) return false;

    const std::string& filePath = files[index].path;
    HINSTANCE result = ShellExecuteA(nullptr, "open", filePath.c_str(),
                                      nullptr, nullptr, SW_SHOWNORMAL);
    return (INT_PTR)result > 32;
}

bool TransferStation::TransferCopyFile(size_t index, const std::string& destination) const {
    std::lock_guard<std::mutex> lock(mutex);

    if (index >= files.size()) return false;

    const std::string& sourcePath = files[index].path;
    std::string destPath = destination + "\\" + files[index].name;

    return ::CopyFileA(sourcePath.c_str(), destPath.c_str(), FALSE) != 0;
}

bool TransferStation::TransferMoveFile(size_t index, const std::string& destination) {
    std::lock_guard<std::mutex> lock(mutex);

    if (index >= files.size()) return false;

    const std::string& sourcePath = files[index].path;
    std::string destPath = destination + "\\" + files[index].name;

    if (::MoveFileA(sourcePath.c_str(), destPath.c_str())) {
        files[index].path = destPath;
        return true;
    }

    return false;
}

bool TransferStation::TransferDeleteFile(size_t index) {
    std::lock_guard<std::mutex> lock(mutex);

    if (index >= files.size()) return false;

    const std::string& filePath = files[index].path;
    if (::DeleteFileA(filePath.c_str())) {
        totalSize -= files[index].size;
        files.erase(files.begin() + index);
        return true;
    }

    return false;
}

FilePreviewInfo TransferStation::GetFilePreviewInfo(size_t index) const {
    std::lock_guard<std::mutex> lock(mutex);

    FilePreviewInfo info;

    if (index >= files.size()) return info;

    const FileInfo& file = files[index];
    const std::string& filePath = file.path;

    info.file_extension = GetFileExtension(filePath);
    info.is_image = IsImageFile(filePath);
    info.is_text  = IsTextFile(filePath);

    if (info.is_image) {
        info.file_type = "Image";
    } else if (info.is_text) {
        info.file_type = "Text";
    } else {
        info.file_type = "Other";
    }

    if (info.is_text) {
        info.text_content = ReadTextFile(filePath);
    }

    WIN32_FILE_ATTRIBUTE_DATA fileAttr;
    if (GetFileAttributesExA(filePath.c_str(), GetFileExInfoStandard, &fileAttr)) {
        info.creation_time     = GetFileTimeString(fileAttr.ftCreationTime);
        info.last_modified_time = GetFileTimeString(fileAttr.ftLastWriteTime);
        info.last_access_time   = GetFileTimeString(fileAttr.ftLastAccessTime);
    }

    return info;
}

std::string TransferStation::ReadTextFile(const std::string& filePath) const {
    std::string content;

    try {
        std::ifstream file(filePath.c_str(), std::ios::in | std::ios::binary);
        if (!file.is_open()) return content;

        std::stringstream buffer;
        buffer << file.rdbuf();
        content = buffer.str();

        const size_t MAX_PREVIEW_SIZE = 1024 * 1024; // 1 MB
        if (content.size() > MAX_PREVIEW_SIZE) {
            content = content.substr(0, MAX_PREVIEW_SIZE) + "\n... (truncated)";
        }

        file.close();
    } catch (...) {
        // Ignore read errors
    }

    return content;
}

bool TransferStation::IsImageFile(const std::string& filePath) const {
    std::string ext = GetFileExtension(filePath);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    std::vector<std::string> imageExts = {
        "jpg", "jpeg", "png", "bmp", "gif", "tiff", "webp", "svg"
    };

    return std::find(imageExts.begin(), imageExts.end(), ext) != imageExts.end();
}

bool TransferStation::IsTextFile(const std::string& filePath) const {
    std::string ext = GetFileExtension(filePath);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    std::vector<std::string> textExts = {
        "txt", "text", "csv", "json", "xml", "html", "css", "js",
        "ts", "cpp", "h", "hpp", "c", "cs", "py", "java", "php",
        "md", "rtf", "log"
    };

    return std::find(textExts.begin(), textExts.end(), ext) != textExts.end();
}

// ---------------------------------------------------------
//  Internal helpers (Windows)
// ---------------------------------------------------------
uint64_t TransferStation::GetFileSize(const std::string& filePath) const {
    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (GetFileAttributesExA(filePath.c_str(), GetFileExInfoStandard, &fileInfo)) {
        return (static_cast<uint64_t>(fileInfo.nFileSizeHigh) << 32) | fileInfo.nFileSizeLow;
    }
    return 0;
}

std::string TransferStation::GetFileName(const std::string& filePath) const {
    // PathFindFileNameA from shlwapi
    LPCSTR p = PathFindFileNameA(filePath.c_str());
    return p ? std::string(p) : std::string();
}

std::string TransferStation::GetFileExtension(const std::string& filePath) const {
    LPCSTR p = PathFindExtensionA(filePath.c_str());
    if (!p) return std::string();

    std::string ext = p;
    if (!ext.empty() && ext[0] == '.') {
        ext = ext.substr(1);
    }
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

std::string TransferStation::GetFileTimeString(FILETIME fileTime) const {
    SYSTEMTIME systemTime;
    FileTimeToSystemTime(&fileTime, &systemTime);

    char timeStr[100];
    sprintf_s(timeStr, sizeof(timeStr),
              "%04d-%02d-%02d %02d:%02d:%02d",
              systemTime.wYear, systemTime.wMonth, systemTime.wDay,
              systemTime.wHour, systemTime.wMinute, systemTime.wSecond);

    return timeStr;
}

// ============================================================
#else  // -----------------------------------------------------
//  Linux implementation — std::filesystem + stat + xdg-open
// ============================================================
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

TransferStation& TransferStation::Instance() {
    static TransferStation instance;
    return instance;
}

void TransferStation::AddFile(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(mutex);

    FileInfo fileInfo;
    fileInfo.path = filePath;
    fileInfo.name = GetFileName(filePath);
    fileInfo.size = GetFileSize(filePath);
    fileInfo.added_time = std::chrono::system_clock::now();

    files.push_back(fileInfo);
    totalSize += fileInfo.size;
}

void TransferStation::RemoveFile(size_t index) {
    std::lock_guard<std::mutex> lock(mutex);

    if (index < files.size()) {
        totalSize -= files[index].size;
        files.erase(files.begin() + index);
    }
}

void TransferStation::Clear() {
    std::lock_guard<std::mutex> lock(mutex);

    files.clear();
    totalSize = 0;
}

std::vector<FileInfo> TransferStation::GetFiles() const {
    std::lock_guard<std::mutex> lock(mutex);
    return files;
}

size_t TransferStation::GetFileCount() const {
    std::lock_guard<std::mutex> lock(mutex);
    return files.size();
}

uint64_t TransferStation::GetTotalSize() const {
    std::lock_guard<std::mutex> lock(mutex);
    return totalSize;
}

bool TransferStation::OpenFile(size_t index) const {
    std::lock_guard<std::mutex> lock(mutex);

    if (index >= files.size()) return false;

    const std::string& filePath = files[index].path;

    // Use xdg-open to open the file with the default handler
    std::string cmd = "xdg-open \"" + filePath + "\" > /dev/null 2>&1 &";
    int ret = system(cmd.c_str());
    return ret == 0;
}

bool TransferStation::TransferCopyFile(size_t index, const std::string& destination) const {
    std::lock_guard<std::mutex> lock(mutex);

    if (index >= files.size()) return false;

    const std::string& sourcePath = files[index].path;
    std::string destPath = destination + "/" + files[index].name;

    // Simple copy via ifstream/ofstream
    std::ifstream src(sourcePath, std::ios::binary);
    if (!src.is_open()) return false;

    std::ofstream dst(destPath, std::ios::binary);
    if (!dst.is_open()) return false;

    dst << src.rdbuf();
    return dst.good();
}

bool TransferStation::TransferMoveFile(size_t index, const std::string& destination) {
    std::lock_guard<std::mutex> lock(mutex);

    if (index >= files.size()) return false;

    const std::string& sourcePath = files[index].path;
    std::string destPath = destination + "/" + files[index].name;

    // Use rename(2) which works for same-filesystem moves
    if (rename(sourcePath.c_str(), destPath.c_str()) == 0) {
        files[index].path = destPath;
        return true;
    }

    // Fallback: copy then remove
    if (TransferCopyFile(index, destination)) {
        unlink(sourcePath.c_str());
        files[index].path = destPath;
        return true;
    }

    return false;
}

bool TransferStation::TransferDeleteFile(size_t index) {
    std::lock_guard<std::mutex> lock(mutex);

    if (index >= files.size()) return false;

    const std::string& filePath = files[index].path;
    if (unlink(filePath.c_str()) == 0) {
        totalSize -= files[index].size;
        files.erase(files.begin() + index);
        return true;
    }

    return false;
}

FilePreviewInfo TransferStation::GetFilePreviewInfo(size_t index) const {
    std::lock_guard<std::mutex> lock(mutex);

    FilePreviewInfo info;

    if (index >= files.size()) return info;

    const FileInfo& file = files[index];
    const std::string& filePath = file.path;

    info.file_extension = GetFileExtension(filePath);
    info.is_image = IsImageFile(filePath);
    info.is_text  = IsTextFile(filePath);

    if (info.is_image) {
        info.file_type = "Image";
    } else if (info.is_text) {
        info.file_type = "Text";
    } else {
        info.file_type = "Other";
    }

    if (info.is_text) {
        info.text_content = ReadTextFile(filePath);
    }

    // Get file times via stat(2)
    struct stat st;
    if (stat(filePath.c_str(), &st) == 0) {
        char buf[64];

        // Creation time (birth time on Linux is st_ctim, but ctime = change time;
        // actual birth time would need statx.  Use mtime as best approximation.)
        struct tm* tm = localtime(&st.st_mtime);
        if (tm) {
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
            info.last_modified_time = buf;
            info.creation_time = buf; // same as mtime (no birth time easily available)
        }

        // Access time
        tm = localtime(&st.st_atime);
        if (tm) {
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
            info.last_access_time = buf;
        }
    }

    return info;
}

std::string TransferStation::ReadTextFile(const std::string& filePath) const {
    std::string content;

    try {
        std::ifstream file(filePath.c_str(), std::ios::in | std::ios::binary);
        if (!file.is_open()) return content;

        std::stringstream buffer;
        buffer << file.rdbuf();
        content = buffer.str();

        const size_t MAX_PREVIEW_SIZE = 1024 * 1024; // 1 MB
        if (content.size() > MAX_PREVIEW_SIZE) {
            content = content.substr(0, MAX_PREVIEW_SIZE) + "\n... (truncated)";
        }

        file.close();
    } catch (...) {
        // Ignore read errors
    }

    return content;
}

bool TransferStation::IsImageFile(const std::string& filePath) const {
    std::string ext = GetFileExtension(filePath);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    std::vector<std::string> imageExts = {
        "jpg", "jpeg", "png", "bmp", "gif", "tiff", "webp", "svg"
    };

    return std::find(imageExts.begin(), imageExts.end(), ext) != imageExts.end();
}

bool TransferStation::IsTextFile(const std::string& filePath) const {
    std::string ext = GetFileExtension(filePath);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    std::vector<std::string> textExts = {
        "txt", "text", "csv", "json", "xml", "html", "css", "js",
        "ts", "cpp", "h", "hpp", "c", "cs", "py", "java", "php",
        "md", "rtf", "log"
    };

    return std::find(textExts.begin(), textExts.end(), ext) != textExts.end();
}

// ---------------------------------------------------------
//  Internal helpers (Linux)
// ---------------------------------------------------------
uint64_t TransferStation::GetFileSize(const std::string& filePath) const {
    struct stat st;
    if (stat(filePath.c_str(), &st) == 0) {
        return static_cast<uint64_t>(st.st_size);
    }
    return 0;
}

std::string TransferStation::GetFileName(const std::string& filePath) const {
    size_t slash = filePath.find_last_of("/\\");
    if (slash != std::string::npos) {
        return filePath.substr(slash + 1);
    }
    return filePath;
}

std::string TransferStation::GetFileExtension(const std::string& filePath) const {
    std::string fname = GetFileName(filePath);
    size_t dot = fname.find_last_of('.');
    if (dot != std::string::npos) {
        std::string ext = fname.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext;
    }
    return std::string();
}

#endif
