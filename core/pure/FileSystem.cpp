#include "FileSystem.h"
#include <filesystem>
#include <fstream>
#include <iterator>

namespace ludo::core {
namespace fs = std::filesystem;

static fs::path nativePath(TextView text) { return fs::u8path(text.begin(), text.end()); }

bool NativeFileSystem::exists(TextView path) const { std::error_code ec; return fs::exists(nativePath(path), ec); }
bool NativeFileSystem::isDirectory(TextView path) const { std::error_code ec; return fs::is_directory(nativePath(path), ec); }

FileReadResult NativeFileSystem::readAll(TextView path) const {
    std::ifstream file(nativePath(path), std::ios::binary);
    if (!file) return {false, {}, "file.open.read"};
    Bytes bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (!file.good() && !file.eof()) return {false, {}, "file.read"};
    return {true, std::move(bytes), {}};
}

FileWriteResult NativeFileSystem::writeAll(TextView path, const Bytes& bytes) {
    std::ofstream file(nativePath(path), std::ios::binary | std::ios::trunc);
    if (!file) return {false, "file.open.write"};
    if (!bytes.empty()) file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    if (!file) return {false, "file.write"};
    return {true, {}};
}

FileWriteResult NativeFileSystem::createDirectories(TextView path) {
    std::error_code ec;
    fs::create_directories(nativePath(path), ec);
    return ec ? FileWriteResult{false, ec.message()} : FileWriteResult{true, {}};
}

} // namespace ludo::core
